// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_BUILD_RESOURCES_H
#define INFILTRATOROS_NATIVE_BUILD_RESOURCES_H

#include <algorithm>
#include <cerrno>
#include <climits>
#include <chrono>
#include <limits>
#include <map>
#include <thread>
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
    bool io_congested = false;
    std::uint64_t capacity_mib = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t disk_mib = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t disk_capacity_mib = std::numeric_limits<std::uint64_t>::max();
    std::optional<std::vector<int>> idle_cpus = std::nullopt;
};

inline std::vector<int> allowed_build_cpus() {
    // Linux can expose CPU identifiers beyond the fixed cpu_set_t size.
    // Grow the mask when the kernel reports it is too small.
    for (int count = CPU_SETSIZE;;) {
        const auto bytes = CPU_ALLOC_SIZE(count);
        cpu_set_t *allowed = CPU_ALLOC(count);
        if (!allowed) return {};
        CPU_ZERO_S(bytes, allowed);
        const int result = sched_getaffinity(0, bytes, allowed);
        const int error = errno;
        std::vector<int> cpus;
        if (result == 0) {
            try {
                for (int cpu = 0; cpu < count; ++cpu)
                    if (CPU_ISSET_S(cpu, bytes, allowed)) cpus.push_back(cpu);
            } catch (...) {
                CPU_FREE(allowed);
                throw;
            }
        }
        CPU_FREE(allowed);
        if (result == 0) return cpus;
        if (error != EINVAL || count > INT_MAX / 2) return {};
        count *= 2;
    }
}

// PSI measures elapsed time lost to I/O stalls rather than nominal disk speed.
// Unavailable PSI leaves the existing CPU and RAM admission policy intact.
inline bool io_pressure_high(const std::filesystem::path &path) {
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);) {
        if (line.rfind("some ", 0) != 0) continue;
        const auto position = line.find("avg10=");
        if (position == std::string::npos) return false;
        std::istringstream value(line.substr(position + 6));
        double percent = 0;
        return bool(value >> percent) && percent >= 20.0;
    }
    return false;
}
// Sample interval CPU activity rather than treating affinity as idleness.
using CpuTicks = std::map<int, std::pair<std::uint64_t, std::uint64_t>>;
inline CpuTicks cpu_ticks(const std::filesystem::path &path) {
    CpuTicks result;
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);) {
        if (line.rfind("cpu", 0) != 0) break;
        std::istringstream fields(line);
        std::string name;
        std::uint64_t user, nice, system, idle, wait, irq, soft, steal;
        if (!(fields >> name >> user >> nice >> system >> idle >> wait >> irq >> soft >> steal) ||
            name.size() <= 3 || name.find_first_not_of("0123456789", 3) != std::string::npos) continue;
        try {
            result.emplace(std::stoi(name.substr(3)), std::make_pair(
                user + nice + system + idle + wait + irq + soft + steal, idle));
        } catch (const std::exception &) { continue; }
    }
    return result;
}
inline std::optional<std::vector<int>> idle_build_cpus(const std::vector<int> &allowed,
        const CpuTicks &before, const CpuTicks &after) {
    std::vector<int> idle;
    bool measured = false;
    for (int cpu : allowed) {
        const auto a = before.find(cpu), b = after.find(cpu);
        if (a == before.end() || b == after.end() || b->second.first <= a->second.first ||
            b->second.second < a->second.second) continue;
        measured = true;
        const auto total = b->second.first - a->second.first;
        if (b->second.second - a->second.second >= total / 4 + (total % 4 != 0))
            idle.push_back(cpu);
    }
    if (!measured) return std::nullopt;
    return idle;
}
inline std::uint64_t disk_headroom(const std::filesystem::path &p) {
    std::error_code ec;
    const auto space = std::filesystem::space(p, ec);
    const auto available = ec ? 0 : space.available / (1024 * 1024);
    return available > 512 ? available - 512 : 0;
}
inline void sample_build_activity(BuildResources &resources,
        const std::filesystem::path &scratch, const std::filesystem::path &stat = "/proc/stat") {
    auto before = cpu_ticks(stat);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    resources.idle_cpus = idle_build_cpus(resources.cpus, before, cpu_ticks(stat));
    // Some released installers use /tmp even when TMPDIR is set.
    resources.disk_mib = std::min(disk_headroom(scratch), disk_headroom("/tmp"));
}
inline BuildResources build_resources(
    std::vector<int> cpus, const std::filesystem::path &meminfo = "/proc/meminfo",
    const std::filesystem::path &membership = "/proc/self/cgroup",
    const std::filesystem::path &cgroup_root = "/sys/fs/cgroup",
    const std::filesystem::path &io_pressure = "/proc/pressure/io") {
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
    return {std::move(cpus), cpu_budget, available > headroom ? available - headroom : 0,
            io_pressure_high(io_pressure), capacity > headroom ? capacity - headroom : 0};
}

enum class BuildKind { module, exfat, files };

struct BuildReservation {
    std::vector<int> cpus;
    std::uint64_t memory_mib;
    std::uint64_t disk_mib = 0;
};

inline std::optional<BuildReservation> reserve_build(
    const BuildResources &resources, const std::vector<BuildReservation> &active,
    BuildKind kind, std::size_t pending) {
    if (!pending || resources.io_congested) return std::nullopt;
    auto free_cpus = resources.cpus;
    auto remaining_peak = resources.capacity_mib;
    auto remaining_disk = resources.disk_capacity_mib;
    std::size_t occupied = 0;
    for (const auto &build : active) {
        occupied += build.cpus.size();
        for (int cpu : build.cpus)
            free_cpus.erase(std::remove(free_cpus.begin(), free_cpus.end(), cpu), free_cpus.end());
        remaining_peak -= std::min(remaining_peak, build.memory_mib);
        remaining_disk -= std::min(remaining_disk, build.disk_mib);
    }
    if (resources.idle_cpus)
        free_cpus.erase(std::remove_if(free_cpus.begin(), free_cpus.end(), [&](int cpu) {
            return std::find(resources.idle_cpus->begin(), resources.idle_cpus->end(), cpu) ==
                resources.idle_cpus->end();
        }), free_cpus.end());
    if (occupied >= resources.cpu_budget || free_cpus.empty()) return std::nullopt;
    // Live availability already accounts for resident memory. Independently
    // limit aggregate estimated peaks rather than subtracting them twice.
    const auto free_memory = std::min(resources.memory_mib, remaining_peak);
    const auto free_disk = std::min(resources.disk_mib, remaining_disk);
    const auto slots = std::min(resources.cpu_budget - occupied, free_cpus.size());
    std::size_t cores = slots > pending ? slots - pending + 1 : 1;
    auto memory_estimate = [kind](std::size_t n) -> std::uint64_t {
        if (kind == BuildKind::files) return 1536 + 512 * n;
        if (kind == BuildKind::exfat) return 1024 + 512 * n;
        return 512 + 256 * n;
    };
    const std::uint64_t disk_estimate = kind == BuildKind::files ? 2048 :
                                      kind == BuildKind::exfat ? 1024 : 512;
    while (cores > 1 && memory_estimate(cores) > free_memory) --cores;
    const auto estimate = memory_estimate(cores);
    if (estimate > free_memory || disk_estimate > free_disk) return std::nullopt;
    free_cpus.resize(cores);
    return BuildReservation{std::move(free_cpus), estimate, disk_estimate};
}

} // namespace infiltrator_native
#endif
