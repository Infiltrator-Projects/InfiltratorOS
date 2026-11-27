// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "build.h"
#include "package.h"
#include "verification.h"
#include "filesystem.h"
#include "../native-build-queue.h"
#include "../native-build-resources.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
namespace infiltrator_native::worker {
Strings build_only_arguments(const Entry &entry, const fs::path &folder,
                                    const std::string &slots) {
    if (entry.kind == "application") {
        if (entry.id == "calendar")
            return {"--build-only", "--output-dir", folder.string()};
        if (!infiltrator_native::queue_application(entry.id))
            throw std::runtime_error("Application has no parallel build contract");
        Strings args{"--system-package-mode"};
        if (entry.id == "system-monitor" || entry.id == "calculator")
            args.insert(args.end(), {"--jobs", slots});
        args.insert(args.end(), {"--build-only", "--output", folder.string()});
        return args;
    }
    Strings args{"--profile", "aggressive"};
    if (entry.kind == "calendar" || entry.kind == "clock")
        args.insert(args.end(), {"--jobs", slots});
    args.insert(args.end(), {"--system-package-mode", "--build-only", "--output", folder.string()});
    return args;
}
static void build(Job &job, const fs::path &logs, const std::vector<int> &cpus) {
    // Fetch only admitted jobs, rather than retaining all RUNs before builds.
    if (!job.asset.url.empty()) {
        Status(logs, job.entry.slug()).write("running", 20, "Downloading native RUN", "SHA-256 release verification");
        download(job.asset, job.run);
    }
    const auto slots = std::to_string(std::max<std::size_t>(1, cpus.size()));
    Strings arguments;
    if (!cpus.empty()) {
        std::string mask;
        for (int cpu : cpus) {
            if (!mask.empty()) mask += ',';
            mask += std::to_string(cpu);
        }
        arguments = {"/usr/bin/taskset", "--cpu-list", mask};
    }
    const auto private_home = job.folder / "home";
    const auto private_tmp = job.folder / "tmp";
    const auto private_runtime = job.folder / "runtime";
    fs::create_directory(private_home);
    fs::create_directory(private_tmp);
    fs::create_directory(private_runtime);
    if (chmod(private_runtime.c_str(), 0700) != 0)
        throw std::runtime_error("Cannot protect private XDG runtime");
    arguments.insert(arguments.end(), {"/usr/bin/env", "-u", "DISPLAY", "-u", "WAYLAND_DISPLAY",
                      "-u", "DBUS_SESSION_BUS_ADDRESS", "-u", "XAUTHORITY", "MAKEFLAGS=-j" + slots,
                      "CMAKE_BUILD_PARALLEL_LEVEL=" + slots,
                      "DEB_BUILD_OPTIONS=parallel=" + slots,
                      "HOME=" + private_home.string(), "TMPDIR=" + private_tmp.string(),
                      "XDG_RUNTIME_DIR=" + private_runtime.string(),
                      "XDG_CACHE_HOME=" + (private_home / ".cache").string(),
                      "XDG_CONFIG_HOME=" + (private_home / ".config").string(),
                      "XDG_DATA_HOME=" + (private_home / ".local/share").string()});
    if (job.entry.id == "calculator") {
        // Common's font helper supports a private staging root. GUI training
        // reads already provisioned system fonts without modifying them.
        const auto font_root = job.folder / "font-root";
        fs::create_directory(font_root);
        arguments.push_back("INFILTRATR_NATIVE_ROOT=" + font_root.string());
        arguments.push_back("INFILTRATR_EXTENDED_FONT_SOURCE_DIR=/System/Resources/Fonts");
    }
    // Released Calendar and Defragmenter scripts query getconf for job counts.
    // Scope the reported count to this build's allocation, not the host.
    // The private directory is root-owned and removed with the run.
    {
        const auto bin = job.folder / "scheduler-bin";
        fs::create_directory(bin);
        const auto shim = bin / "getconf";
        save(shim, "#!/bin/sh\nif [ \"$#\" -eq 1 ] && [ \"$1\" = _NPROCESSORS_ONLN ]; then\n"
                   "  printf '%s\\n' " + slots + "\nelse\n  exec /usr/bin/getconf \"$@\"\nfi\n");
        if (chmod(shim.c_str(), 0700) != 0)
            throw std::runtime_error("Cannot set private build CPU-count helper executable");
        const auto apt_guard = bin / "apt-get";
        save(apt_guard, "#!/bin/sh\necho 'APT is forbidden during native build-only execution' >&2\nexit 125\n");
        if (chmod(apt_guard.c_str(), 0700) != 0)
            throw std::runtime_error("Cannot install private package-manager guard");
        arguments.push_back("PATH=" + bin.string() + ":" +
            environment("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"));
    }
    if (!job.asset.url.empty()) {
        arguments.push_back(fs::read_symlink("/proc/self/exe").string());
        arguments.push_back("--isolated-build");
        arguments.push_back(job.folder.string());
    }
    arguments.push_back(job.run.string());
    const auto options = build_only_arguments(job.entry, job.folder, slots);
    arguments.insert(arguments.end(), options.begin(), options.end());
    if (execute(arguments, nullptr, logs / (job.entry.slug() + "-build.log")) != 0)
        throw std::runtime_error("Native build failed: " + job.entry.slug());
    job.package = native_package_from(job.folder, job.entry, job.asset);
    if (!job.asset.url.empty()) {
        verify_native_payload(job.package, job.entry, job.asset, job.folder);
        fs::remove(job.run);
    }
}
void build_packages(std::vector<Job> &jobs, const fs::path &statuses,
                           std::map<std::string, std::string> &failures) {
    using namespace infiltrator_native;
    std::ofstream timings(statuses / "smart-build-timings.log");
    if (!timings) throw std::runtime_error("Cannot open native build timing log");
    timings << "Build admission uses available CPUs and RAM. Selected builds: " << jobs.size() << '\n';
    const auto started = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    };
    struct Active {
        std::size_t job;
        BuildReservation resources;
        std::future<void> result;
    };
    std::vector<Active> active;
    std::size_t next = 0;
    std::optional<std::chrono::steady_clock::time_point> blocked;
    std::uint64_t disk_budget = 0, memory_budget = 0;
    bool have_disk_budget = false;
    while (next < jobs.size() || !active.empty()) {
        for (auto it = active.begin(); it != active.end();) {
            if (it->result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            const auto slug = jobs[it->job].entry.slug();
            try {
                it->result.get();
                timings << elapsed() << "ms finished " << slug << std::endl;
            } catch (const std::exception &error) {
                failures[slug] = error.what();
                Status(statuses, slug).write("failed", 100, "Native build failed", error.what());
                timings << elapsed() << "ms failed " << slug << ": " << error.what() << std::endl;
            }
            it = active.erase(it);
        }
        if (next < jobs.size()) {
            auto resources = build_resources(allowed_build_cpus());
            sample_build_activity(resources, statuses);
            if (!have_disk_budget || active.empty()) {
                disk_budget = resources.disk_mib;
                memory_budget = resources.memory_mib;
                have_disk_budget = true;
            }
            resources.disk_capacity_mib = disk_budget;
            resources.capacity_mib = std::min(resources.capacity_mib, memory_budget);
            std::vector<BuildReservation> reservations;
            for (const auto &job : active) reservations.push_back(job.resources);
            std::optional<BuildReservation> reservation;
            std::size_t candidate = next;
            // A temporarily blocked large job must not strand capacity for
            // another independent build. Only pending entries are reordered.
            for (; candidate < jobs.size(); ++candidate) {
                const auto &entry = jobs[candidate].entry;
                const auto kind = entry.kind == "application" ? BuildKind::files :
                    entry.id == "exfat" ? BuildKind::exfat : BuildKind::module;
                reservation = reserve_build(resources, reservations, kind, jobs.size() - next);
                if (reservation) break;
            }
            if (reservation) {
                if (candidate != next) std::swap(jobs[next], jobs[candidate]);
                const auto &entry = jobs[next].entry;
                blocked.reset();
                const auto job = next++;
                const auto count = std::to_string(active.size() + 1);
                const auto slots = std::max<std::size_t>(1, reservation->cpus.size());
                Status(statuses, entry.slug()).write("running", 35, "Compiling native component",
                    count + " builds active, " + std::to_string(slots) + " CPU slots for this build");
                timings << elapsed() << "ms started " << entry.slug() << "; active=" << count
                        << "; cpu_slots=" << slots << '/' << resources.cpu_budget
                        << "; reserved_mib=" << reservation->memory_mib
                        << "; memory_budget_mib=" << resources.memory_mib
                         << "; disk_budget_mib=" << resources.disk_mib << std::endl;
                auto result = std::async(std::launch::async, [&, job, cpus = reservation->cpus] {
                    build(jobs[job], statuses, cpus);
                });
                active.push_back({job, std::move(*reservation), std::move(result)});
                continue;
            }
        }
        if (active.empty() && next < jobs.size()) {
            const auto now = std::chrono::steady_clock::now();
            if (!blocked) blocked = now;
            Status(statuses, jobs[next].entry.slug()).write("queued", 10,
                "Waiting for build resources", "CPU, memory or temporary storage unavailable");
            if (now - *blocked >= std::chrono::seconds(60)) {
                const auto slug = jobs[next++].entry.slug();
                failures[slug] = "Insufficient build resources after bounded wait";
                Status(statuses, slug).write("failed", 100, "Native build not admitted", failures[slug]);
                blocked.reset();
                continue;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            blocked.reset();
            if (!active.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

} // namespace infiltrator_native::worker
