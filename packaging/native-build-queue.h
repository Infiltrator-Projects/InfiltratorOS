// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_BUILD_QUEUE_H
#define INFILTRATOROS_NATIVE_BUILD_QUEUE_H

#include <array>
#include <string_view>

// One catalogue for both dispatcher admission and native worker execution.
namespace infiltrator_native {
inline constexpr std::array<std::string_view, 8> build_queue{{
    "filesystem-fat12", "filesystem-fat16", "filesystem-fat32", "filesystem-exfat",
    "filesystem-ofs", "filesystem-ntfs", "filesystem-ffs", "filesystem-hfs"
}};
inline bool queue_candidate(std::string_view name) {
    for (auto candidate : build_queue)
        if (candidate == name) return true;
    return false;
}
} // namespace infiltrator_native

#endif
