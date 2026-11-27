// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_PACKAGE_H
#define INFILTRATOROS_NATIVE_WORKER_PACKAGE_H
#include "release.h"
#include <functional>
namespace infiltrator_native::worker {
Strings package_install_command(const fs::path &package, bool calendar = false);
fs::path native_package_from(const fs::path &folder, const Entry &entry, const Asset &asset);
void verify_native_payload(const fs::path &package, const Entry &entry, const Asset &asset, const fs::path &scratch);
void replace_deb_with_recovery(const Entry &entry, const fs::path &package, const fs::path &recovery, const std::function<void()> &verify);

} // namespace infiltrator_native::worker
#endif
