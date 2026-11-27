// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_VERIFICATION_H
#define INFILTRATOROS_NATIVE_WORKER_VERIFICATION_H
#include "release.h"
namespace infiltrator_native::worker {
Strings required_paths(const std::string &slug);
bool application_package_matches(const Entry &entry, const Asset &asset, const std::string &package_record);
bool application_current(const Entry &entry, const Asset &asset, bool evidence, std::string *reason = nullptr);
void verify_install(const Entry &entry, const Asset &asset, bool application_mode);

} // namespace infiltrator_native::worker
#endif
