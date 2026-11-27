// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_BOOT_H
#define INFILTRATOROS_NATIVE_WORKER_BOOT_H
#include "release.h"
#include <istream>
namespace infiltrator_native::worker {
fs::path installed_boot_image();
bool firmware_partition_matches(const std::string &line, const std::string &partuuid);
bool boot_registered(const std::string &firmware, bool allow_shim = false, const std::string &partuuid = {});
bool secure_boot_flag(std::istream &stream, bool attributes);
fs::path efibootmgr_executable(const std::vector<fs::path> &candidates = {
    "/usr/bin/efibootmgr", "/usr/sbin/efibootmgr", "/bin/efibootmgr", "/sbin/efibootmgr"});
std::string boot_efi_inventory(const fs::path &image);
bool native_boot_current(const Asset &asset, bool evidence, std::string *reason);

} // namespace infiltrator_native::worker
#endif
