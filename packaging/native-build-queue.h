// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_BUILD_QUEUE_H
#define INFILTRATOROS_NATIVE_BUILD_QUEUE_H

#include <array>
#include <string_view>

// The signed run catalogue supplies jobs; resource admission supplies concurrency.
namespace infiltrator_native {
// One registry owns the application identities used by release discovery,
// the dispatcher and the build queue. BOOT is the initial, direct handoff after component selection.
struct ApplicationContract {
    std::string_view slug, repository, package, resource;
    bool queued;
};
inline constexpr std::array<ApplicationContract, 6> application_contracts{{
    {"system-monitor", "System-Monitor", "system-monitor", "/Programs/system-monitor", true},
    {"calculator", "Calculator", "calculator", "/Programs/calculator", true},
    {"calendar", "Calendar", "calendar", "/usr/libexec/calendar-about", true},
    {"files", "Files", "files", "/Programs/files", true},
    {"defragmenter", "Defragmenter", "defragmenter", "/Programs/defragmenter", true},
    {"boot", "Boot", "infiltrator-boot", "/boot/efi/EFI/InfiltratorOS/BOOT/BOOTX64.EFI", false}
}};
inline constexpr std::size_t queued_application_count() {
    std::size_t count = 0;
    for (const auto &entry : application_contracts) if (entry.queued) ++count;
    return count;
}
inline constexpr auto queued_applications = [] {
    std::array<std::string_view, queued_application_count()> result{};
    std::size_t index = 0;
    for (const auto &entry : application_contracts)
        if (entry.queued) result[index++] = entry.slug;
    return result;
}();
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
