// SPDX-License-Identifier: GPL-3.0-or-later
#include "install.h"
#include "build.h"
#include "dependencies.h"
#include "filesystem.h"
#include "package.h"
#include "verification.h"
#include "../native-build-queue.h"
#include <algorithm>
#include <memory>
#include <sstream>
namespace infiltrator_native::worker {
static int run_selected_builds(const std::vector<Entry> &chosen, const fs::path &statuses) {
    std::vector<std::unique_ptr<Lock>> locks;
    std::set<std::string> slugs;
    for (const auto &entry : chosen)
        if (!slugs.insert(entry.slug()).second || !infiltrator_native::queue_candidate(entry.slug()))
            throw std::runtime_error("Invalid or duplicate build selection");
    for (const auto &slug : slugs)
        locks.emplace_back(std::make_unique<Lock>(ROOT / (slug + ".lock")));
    Lock global(ROOT / "native-install.lock");
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    setenv("TMPDIR", statuses.c_str(), 1);
    TempDir scratch(statuses, "smart-build-");
    std::vector<Job> jobs;
    std::set<std::string> completed;
    std::map<std::string, std::string> failures;
    try {
        for (const auto &e : chosen) {
            Status status(statuses, e.slug());
            try {
                const bool app = e.kind == "application";
                const bool temporal = e.kind == "calendar" || e.kind == "clock";
                status.write("queued", 2, "Smart build queued", "Awaiting native package lock");
                Document meta = release(e);
                Asset asset = release_asset(e, meta.ptr, temporal, !app && !temporal);
                if (app ? application_current(e, asset, true) : provider_current(e, asset)) {
                    status.write("complete", 100, "Already installed", "Native package verified");
                    completed.insert(e.slug());
                    continue;
                }
                prepare_build_dependencies(e, status);
                Job job{e, asset, scratch.path / (e.slug() + ".run"), scratch.path / e.slug(), {}};
                status.write("queued", 15, "Native build prepared", "Awaiting resource admission");
                fs::create_directory(job.folder);
                jobs.push_back(job);
            } catch (const std::exception &error) {
                failures[e.slug()] = error.what();
                status.write("failed", 100, "Native preparation failed", error.what());
            }
        }
        build_packages(jobs, statuses, failures);
        for (auto &job : jobs) {
            if (failures.count(job.entry.slug())) continue;
            Status status(statuses, job.entry.slug());
            try {
                status.write("running", 85, "Installing native package", "Serial package transaction");
                const bool app = job.entry.kind == "application";
                // Use the same lock-aware transaction for every queued DEB.
                replace_deb_with_recovery(job.entry, job.package, statuses,
                    [&] { verify_install(job.entry, job.asset, app); });
                status.write("complete", 100, "Native installation complete", "Verified installed package");
                completed.insert(job.entry.slug());
            } catch (const std::exception &error) {
                failures[job.entry.slug()] = error.what();
                status.write("failed", 100, "Native package installation failed", error.what());
            }
        }
        if (!failures.empty()) {
            std::ostringstream summary;
            summary << failures.size() << " independent native job(s) failed";
            for (const auto &[slug, error] : failures)
                summary << "\n" << slug << ": " << error;
            throw std::runtime_error(summary.str());
        }
    } catch (const std::exception &error) {
        for (const auto &e : chosen)
            if (!completed.count(e.slug()) && !failures.count(e.slug()))
                Status(statuses, e.slug()).write("failed", 100, "Smart queue interrupted", error.what());
        throw;
    }
    return 0;
}

int smart_queue(const std::vector<Entry> &entries) {
    if (geteuid() != 0) throw std::runtime_error("Smart queue requires root");
    fs::path stage = stage_dir();
    if (stage == ROOT || environment("INFILTRATOROS_RELEASE_DIR").empty())
        throw std::runtime_error("Smart queue requires a native run snapshot");
    const auto selection_order = lines(read_file(stage / "selected.list"));
    std::set<std::string> selected(selection_order.begin(), selection_order.end());
    if (selection_order.size() != selected.size()) throw std::runtime_error("Duplicate selected component");
    std::vector<Entry> chosen;
    for (const auto &slug : selection_order)
        if (infiltrator_native::queue_candidate(slug)) {
            auto it = std::find_if(entries.begin(), entries.end(),
                                   [&](const Entry &e) { return e.slug() == slug; });
            if (it == entries.end()) throw std::runtime_error("Unknown smart queue candidate");
            chosen.push_back(*it);
        }
    if (chosen.empty()) throw std::runtime_error("No selected smart build candidates");
    return run_selected_builds(chosen, stage / "status");
}
// BOOT alone is a direct installer: it owns the EFI/NVRAM transaction.
// All ordinary applications and providers, even a one-job invocation,
// enter the same isolated build and Debian recovery pipeline.
static int install_boot(const Entry &entry, const fs::path &statuses) {
    Status status(statuses, entry.slug());
    Lock owner(ROOT / (entry.slug() + ".lock"));
    Lock global(ROOT / "native-install.lock");
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    setenv("TMPDIR", statuses.c_str(), 1);
    try {
        status.write("running", 5, "Checking native BOOT release", entry.repo);
        const auto meta = release(entry);
        const auto asset = release_asset(entry, meta.ptr, false, false);
        if (application_current(entry, asset, true)) {
            status.write("complete", 100, "Already installed", "Native EFI inventory verified");
            return 0;
        }
        required({"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check"});
        TempDir directory(statuses, "native-boot-");
        const auto run = directory.path / "native-installer.run";
        status.write("running", 20, "Downloading verified BOOT RUN", entry.repo);
        download(asset, run);
        status.write("running", 35, "Executing independent BOOT RUN", entry.repo);
        required({run.string()}, statuses / "boot-native.log");
        verify_install(entry, asset, true);
        status.write("complete", 100, "Installed", "Native EFI inventory verified");
    } catch (const std::exception &error) {
        status.write("failed", 100, "Native BOOT installation failed", error.what());
        throw;
    }
    return 0;
}
int individual(const Entry &entry) {
    if (geteuid() != 0) throw std::runtime_error("Native installation requires root");
    fs::create_directories(ROOT);
    const auto stage = stage_dir();
    const auto statuses = stage == ROOT ? stage : stage / "status";
    fs::create_directories(statuses);
    if (entry.id == "boot" && entry.kind == "application") return install_boot(entry, statuses);
    return run_selected_builds({entry}, statuses);
}

} // namespace infiltrator_native::worker
