// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_BUILD_RESOURCES_H
#define INFILTRATOROS_NATIVE_BUILD_RESOURCES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sched.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace infiltrator_native {

struct BuildResources {
    std::vector<int> cpus;
    std::size_t cpu_budget;
    std::uint64_t memory_mib;
};

inline std::vector<int> allowed_build_cpus() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) return {};
    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
        if (CPU_ISSET(cpu, &allowed)) cpus.push_back(cpu);
    return cpus;
}

inline BuildResources build_resources(
    std::vector<int> cpus, const std::filesystem::path &meminfo = "/proc/meminfo",
    const std::filesystem::path &membership = "/proc/self/cgroup",
    const std::filesystem::path &cgroup_root = "/sys/fs/cgroup") {
    std::uint64_t capacity = 0, available = 0;
    std::ifstream memory(meminfo);
    for (std::string line; std::getline(memory, line);) {
        std::istringstream field(line);
        std::string key;
        std::uint64_t kib = 0;
        if (!(field >> key >> kib)) continue;
        if (key == "MemTotal:") capacity = kib / 1024;
        if (key == "MemAvailable:") available = kib / 1024;
    }

    // The process's affinity reflects cpusets. A cgroup CPU-time quota can
    // still be smaller, and a parent can restrict both CPU and memory.
    std::size_t cpu_budget = std::max<std::size_t>(1, cpus.size());
    auto group = cgroup_root;
    std::ifstream cgroups(membership);
    for (std::string line; std::getline(cgroups, line);) {
        if (line.rfind("0::/", 0) != 0) continue;
        auto relative = std::filesystem::path(line.substr(3)).lexically_normal().relative_path();
        if (!relative.empty()) group /= relative;
        break;
    }
    for (;;) {
        std::uint64_t quota = 0, period = 0;
        std::ifstream cpu(group / "cpu.max");
        if (cpu >> quota >> period && period != 0)
            cpu_budget = std::min<std::uint64_t>(cpu_budget, std::max<std::uint64_t>(1, quota / period));

        std::uint64_t limit = 0, used = 0;
        std::ifstream memory_limit(group / "memory.max");
        if (memory_limit >> limit) {
            capacity = std::min(capacity, limit / (1024 * 1024));
            std::ifstream memory_usage(group / "memory.current");
            available = (memory_usage >> used) && used < limit
                ? std::min(available, (limit - used) / (1024 * 1024)) : 0;
        }
        if (group == cgroup_root || group == group.parent_path()) break;
        group = group.parent_path();
    }

    // Leave headroom for the desktop, the installer and transient allocations.
    const auto headroom = std::clamp<std::uint64_t>(capacity / 8, 256, 2048);
    return {std::move(cpus), cpu_budget, available > headroom ? available - headroom : 0};
}

enum class BuildKind { module, exfat, files };

struct BuildReservation {
    std::vector<int> cpus;
    std::uint64_t memory_mib;
};

inline std::optional<BuildReservation> reserve_build(
    const BuildResources &resources, const std::vector<BuildReservation> &active,
    BuildKind kind, std::size_t pending) {
    auto free_cpus = resources.cpus;
    auto free_memory = resources.memory_mib;
    std::size_t occupied = 0;
    for (const auto &build : active) {
        occupied += std::max<std::size_t>(1, build.cpus.size());
        for (int cpu : build.cpus)
            free_cpus.erase(std::remove(free_cpus.begin(), free_cpus.end(), cpu), free_cpus.end());
        // Live available RAM may already reflect some usage. Keep the full
        // estimate reserved because a compile can peak later during linking.
        free_memory -= std::min(free_memory, build.memory_mib);
    }
    if (occupied >= resources.cpu_budget || (free_cpus.empty() && !active.empty()))
        return std::nullopt;

    // Modules get a core each. Files can use more, and its Ninja build can
    // start extra compiler processes even within its assigned CPU affinity.
    const auto slots = std::min(resources.cpu_budget - occupied,
                                std::max<std::size_t>(1, free_cpus.size()));
    const auto spare_slots = slots > pending ? slots - pending + 1 : 1;
    std::size_t cores = kind == BuildKind::files
        ? std::min(spare_slots, std::max<std::size_t>(1, resources.cpu_budget / 4)) : 1;
    auto memory_estimate = [kind](std::size_t slots) -> std::uint64_t {
        if (kind == BuildKind::files) return 1536 + 512 * slots;
        return kind == BuildKind::exfat ? 1536 : 768;
    };
    while (cores > 1 && memory_estimate(cores) > free_memory) --cores;
    const auto estimate = memory_estimate(cores);
    if (estimate > free_memory && !active.empty()) return std::nullopt;
    // If resource data is missing or the machine is tight, still attempt one
    // build alone. Never start a second until the budget can accommodate it.
    free_cpus.resize(std::min(cores, free_cpus.size()));
    return BuildReservation{std::move(free_cpus), estimate};
}

} // namespace infiltrator_native
#endif
