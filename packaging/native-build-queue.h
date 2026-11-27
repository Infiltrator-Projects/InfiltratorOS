// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_BUILD_QUEUE_H
#define INFILTRATOROS_NATIVE_BUILD_QUEUE_H

#include <array>
#include <string_view>

// The signed run catalogue supplies jobs; resource admission supplies concurrency.
namespace infiltrator_native {
inline constexpr std::array<std::string_view, 5> queued_applications{{
    "system-monitor", "calculator", "calendar", "files", "defragmenter"
}};
inline bool queue_application(std::string_view name) {
    for (const auto app : queued_applications) if (name == app) return true;
    return false;
}
inline bool queue_candidate(std::string_view name) {
    return queue_application(name) || name.substr(0, 11) == "filesystem-" ||
           name.substr(0, 9) == "calendar-" || name.substr(0, 6) == "clock-";
}
} // namespace infiltrator_native

#endif
