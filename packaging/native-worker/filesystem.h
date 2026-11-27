// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_FILESYSTEM_H
#define INFILTRATOROS_NATIVE_WORKER_FILESYSTEM_H
#include "release.h"
namespace infiltrator_native::worker {
bool exports_common_filesystem_api(const fs::path &path);
int validate_common_child(const fs::path &path, const std::string &id);
bool validate_common_descriptor(const fs::path &path, const std::string &id);
bool provider_current(const Entry &entry, const Asset &asset);

} // namespace infiltrator_native::worker
#endif
