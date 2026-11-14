// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_NATIVE_SECURE_BOOT_H
#define INFILTRATOR_NATIVE_SECURE_BOOT_H
#include <istream>

static bool secure_boot_disabled(std::istream &variable) {
    // efivarfs files cannot be seeked. Read the four attribute bytes and the
    // state byte in one sequential operation, rejecting missing/truncated data.
    char bytes[5]{};
    return static_cast<bool>(variable.read(bytes, sizeof(bytes))) && bytes[4] == 0;
}
#endif
