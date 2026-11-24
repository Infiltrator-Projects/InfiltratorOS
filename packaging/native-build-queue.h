// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_BUILD_QUEUE_H
#define INFILTRATOROS_NATIVE_BUILD_QUEUE_H

#include <array>
#include <string_view>

// Admission follows the signed run catalogue. The former pilot set remains
// a representative resource-scheduler test fixture.
namespace infiltrator_native {
inline constexpr std::array<std::string_view, 16> build_queue{{
    "filesystem-fat12", "filesystem-fat16", "filesystem-fat32", "filesystem-exfat",
    "filesystem-ofs", "filesystem-ntfs", "filesystem-ffs", "filesystem-hfs",
    "files", "clock-chinese-ke", "calendar-byzantine", "clock-babylonian-hours",
    "calendar-indian", "calendar-swedish-historical", "calendar-gregorian",
    "calendar-islamic-umalqura"
}};
inline constexpr std::size_t max_parallel_builds = 16;
inline bool queue_candidate(std::string_view name) {
    return name == "files" || name.substr(0, 11) == "filesystem-" ||
           name.substr(0, 9) == "calendar-" || name.substr(0, 6) == "clock-";
}
} // namespace infiltrator_native

#endif
