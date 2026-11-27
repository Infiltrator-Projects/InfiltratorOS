// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_BUILD_H
#define INFILTRATOROS_NATIVE_WORKER_BUILD_H
#include "release.h"
#include <map>
namespace infiltrator_native::worker {
struct Job {
    Entry entry;
    Asset asset;
    fs::path run, folder, package;
};

Strings build_only_arguments(const Entry &entry, const fs::path &folder, const std::string &slots);
void build_packages(std::vector<Job> &jobs, const fs::path &statuses, std::map<std::string, std::string> &failures);
Strings application_build_dependencies(const Entry &e);
int smart_queue(const std::vector<Entry> &entries);

} // namespace infiltrator_native::worker
#endif
