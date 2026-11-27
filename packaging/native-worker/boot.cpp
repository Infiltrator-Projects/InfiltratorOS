// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "boot.h"
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
namespace infiltrator_native::worker {
// The independent BOOT RUN owns all ESP writes and NVRAM changes.
// InfiltratorOS only verifies its fixed destination and selected boot option.
fs::path installed_boot_image() {
    fs::path found;
    for (const char *mount : {"/boot/efi", "/efi", "/boot"}) {
        if (output({"/usr/bin/findmnt", "-rnM", mount, "-t", "vfat", "-o", "FSTYPE"}) != "vfat")
            continue;
        const fs::path directory = fs::path(mount) / "EFI/InfiltratorOS/BOOT";
        if (fs::is_symlink(directory) || fs::is_symlink(directory.parent_path()) ||
            fs::is_symlink(directory.parent_path().parent_path()))
            continue;
        const auto image = directory / "BOOTX64.EFI";
        if (!regular(image) || !regular(directory / "boot.conf")) continue;
        if (!found.empty()) return {}; // Never accept two plausible BOOT installations.
        found = image;
    }
    return found;
}
// Firmware must launch trusted Shim when Secure Boot is enabled. BOOT's
// signed second stage is still BOOTX64.EFI in the fixed directory.
// Match the firmware device path to the mounted ESP, not just its filename.
bool firmware_partition_matches(const std::string &line, const std::string &partuuid) {
    if (partuuid.empty()) return false;
    const auto lower = lowercase(line), expected = lowercase(partuuid);
    const auto start = lower.find("hd(");
    if (start == std::string::npos) return false;
    const auto end = lower.find(')', start);
    if (end == std::string::npos) return false;
    std::istringstream input(lower.substr(start + 3, end - start - 3));
    std::string partition, scheme, signature, offset, size, extra;
    if (!std::getline(input, partition, ',') || !std::getline(input, scheme, ',') ||
        !std::getline(input, signature, ',') || !std::getline(input, offset, ',') ||
        !std::getline(input, size, ',') || std::getline(input, extra, ',')) return false;
    if (partition.empty() || partition.find_first_not_of("0123456789") != std::string::npos)
        return false;
    if (scheme == "gpt")
        return std::regex_match(expected, std::regex("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) &&
               signature == expected;
    if (scheme != "mbr" || !std::regex_match(expected, std::regex("[0-9a-f]{8}-[0-9a-f]{2}")) ||
        !std::regex_match(signature, std::regex("0x[0-9a-f]{1,8}"))) return false;
    try {
        return std::stoul(partition) == std::stoul(expected.substr(9), nullptr, 16) &&
            std::stoul(signature, nullptr, 16) == std::stoul(expected.substr(0, 8), nullptr, 16);
    } catch (const std::exception &) { return false; }
}
static std::string esp_partition_identity(const fs::path &image) {
    const auto mount = image.parent_path().parent_path().parent_path().parent_path();
    const auto source = output({"/usr/bin/findmnt", "-rnM", mount.string(), "-t", "vfat", "-o", "SOURCE"});
    if (source.rfind("/dev/", 0) != 0 || source.find('\n') != std::string::npos)
        return {};
    return output({"/usr/bin/lsblk", "--noheadings", "--nodeps", "--output", "PARTUUID", "--", source});
}
bool boot_registered(const std::string &firmware, bool allow_shim,
                            const std::string &partuuid) {
    std::string first, matching;
    unsigned int count = 0;
    for (const auto &line : lines(firmware)) {
        const auto lower = lowercase(line);
        if (lower.rfind("bootorder:", 0) == 0) {
            const auto order = trim(lower.substr(10));
            first = order.substr(0, order.find(','));
        }
        const bool direct = lower.find("file(\\efi\\infiltratoros\\boot\\bootx64.efi)") != std::string::npos;
        const bool shim = allow_shim &&
            (lower.find("file(\\efi\\infiltratoros\\boot\\shimx64.efi)") != std::string::npos ||
             lower.find("file(\\efi\\infiltratoros\\boot\\shimx64.efi.signed)") != std::string::npos);
        if (lower.size() < 9 || lower.rfind("boot", 0) != 0 ||
            (!direct && !shim))
            continue;
        if (line[8] != '*' || (!partuuid.empty() && !firmware_partition_matches(line, partuuid)))
            return false;
        const auto candidate = lower.substr(4, 4);
        if (candidate.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
        matching = candidate;
        ++count;
    }
    return count == 1 && first == matching;
}
// Read the UEFI SecureBoot variable, whose first four bytes are efivarfs
// attributes, followed by the 0/1 data byte. Unknown values stay restrictive.
// efivarfs is a non-seekable firmware interface, not an ordinary disk file.
// Consume the four-byte attributes sequentially before reading the flag.
bool secure_boot_flag(std::istream &stream, bool attributes) {
    if (attributes) stream.ignore(4);
    char flag = 0;
    return bool(stream.get(flag)) && flag == 1;
}
static int secure_boot_state() {
    const fs::path modern =
        "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c";
    const fs::path legacy =
        "/sys/firmware/efi/vars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c/data";
    const fs::path path = fs::exists(modern) ? modern : legacy;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return -1;
    if (secure_boot_flag(stream, path == modern)) return 1;
    // Reopen the nonseekable variable to distinguish disabled from invalid.
    std::ifstream disabled(path, std::ios::binary);
    if (path == modern) disabled.ignore(4);
    char flag = 1;
    return disabled.get(flag) && flag == 0 ? 0 : -1;
}
// UEFI utilities are installed under /usr/bin on modern Debian/Ubuntu but
// under /usr/sbin on some other systems. Resolve only standard absolute
// locations: never misreport a missing executable as a missing firmware entry.
fs::path efibootmgr_executable(const std::vector<fs::path> &candidates) {
    for (const auto &candidate : candidates)
        if (access(candidate.c_str(), X_OK) == 0 && fs::is_regular_file(candidate))
            return candidate;
    return {};
}
// Verify and attest the EFI executable set separately from editable boot.conf,
// logs and firmware variables. This is file-deployment evidence, not a claim
// that UEFI can mount every filesystem or that a future V2 bridge is complete.
std::string boot_efi_inventory(const fs::path &image) {
    std::map<std::string, std::string> hashes;
    const auto directory = image.parent_path();
    auto add = [&](const fs::path &file, bool executable) {
        if (!regular(file) || fs::file_size(file) == 0)
            throw std::runtime_error("Missing or unsafe BOOT EFI file: " + file.string());
        if (executable) {
            std::ifstream input(file, std::ios::binary);
            char magic[2]{};
            if (!input.read(magic, 2) || magic[0] != 'M' || magic[1] != 'Z')
                throw std::runtime_error("Invalid BOOT EFI executable header: " + file.string());
        }
        hashes.emplace(file.lexically_relative(directory).generic_string(), digest(file));
    };
    add(image, true);
    for (const auto &file : fs::directory_iterator(directory)) {
        const auto name = lowercase(file.path().filename().string());
        if (file.path() != image &&
            (lowercase(file.path().extension().string()) == ".efi" || name == "shimx64.efi.signed"))
            add(file.path(), true);
    }
    const auto drivers = directory / "drivers_x64";
    if (fs::is_symlink(drivers) || !fs::is_directory(drivers))
        throw std::runtime_error("BOOT EFI driver directory is missing or unsafe");
    for (const auto &file : fs::directory_iterator(drivers)) {
        const auto extension = lowercase(file.path().extension().string());
        if (extension == ".efi" || extension == ".so") add(file.path(), extension == ".efi");
    }
    // The present BOOT native RUN builds and requests all six EFI drivers.
    // This contract must be revised in tandem with BOOT's future V2 transition.
    for (const auto *name : {"ext2", "ext4", "reiserfs", "iso9660", "hfs", "btrfs"})
        if (!hashes.count(std::string("drivers_x64/") + name + "_x64.efi"))
            throw std::runtime_error(std::string("BOOT EFI driver missing: ") + name);
    std::string inventory;
    for (const auto &[name, hash] : hashes) inventory += hash + "  " + name + "\n";
    return inventory;
}
static bool efi_signature_present(const fs::path &image) {
    if (!regular(image)) return false;
    std::string report;
    if (execute({"/usr/bin/sbverify", "--list", image.string()}, &report) != 0) return false;
    const auto lower = lowercase(report);
    return lower.find("signature 1") != std::string::npos &&
           lower.find("no signature table") == std::string::npos;
}

bool native_boot_current(const Asset &asset, bool evidence, std::string *reason) {
    auto fail = [&](const char *message) {
        if (reason) *reason = message;
        return false;
    };
    const auto image = installed_boot_image();
    if (image.empty()) return fail("BOOTX64.EFI or boot.conf is missing from the fixed ESP path");
    const auto manager = efibootmgr_executable();
    if (manager.empty())
        return fail("Cannot verify BOOT firmware: efibootmgr is missing from standard system paths");
    std::string firmware;
    const int rc = execute({manager.string(), "-v"}, &firmware);
    if (rc != 0)
        return fail("Cannot verify BOOT firmware: efibootmgr -v failed");
    const int secure = secure_boot_state();
    if (secure < 0) return fail("Cannot establish UEFI Secure Boot state");
    bool allow_shim = false;
    if (secure == 1) {
        for (const char *name : {"shimx64.efi", "shimx64.efi.signed"})
            if (regular(image.parent_path() / name)) allow_shim = true;
    }
    const auto esp = esp_partition_identity(image);
    if (esp.empty()) return fail("Cannot establish the mounted ESP partition identity");
    if (!boot_registered(firmware, allow_shim, esp))
        return fail("BOOT's fixed EFI entry is absent, duplicated, inactive, not first, or points at another ESP");
    if (secure == 1) {
        // Presence of a parseable signature is not proof of firmware DB,
        // DBX or MOK trust. Never modify firmware or assert a boot test.
        if (!efi_signature_present(image))
            return fail("Secure Boot BOOTX64.EFI signature cannot be established");
        const auto lower_firmware = lowercase(firmware);
        for (const char *name : {"shimx64.efi", "shimx64.efi.signed"}) {
            const std::string path = "file(\\efi\\infiltratoros\\boot\\" + std::string(name) + ")";
            if (lower_firmware.find(path) != std::string::npos &&
                !efi_signature_present(image.parent_path() / name))
                return fail("Secure Boot Shim signature cannot be established");
        }
    }
    std::string inventory;
    try { inventory = boot_efi_inventory(image); }
    catch (const std::exception &error) { if (reason) *reason = error.what(); return false; }
    if (!evidence) return true;
    const auto attestation = ROOT / "native-verified-boot.json";
    if (!regular(attestation)) return fail("BOOT native installation attestation is absent");
    try {
        Document proof = parse(read_file(attestation));
        if (str(proof.ptr, "version") != asset.version ||
            str(proof.ptr, "installer_sha256") != asset.sha ||
            str(proof.ptr, "program_sha256") != digest(image) ||
            str(proof.ptr, "efi_inventory") != inventory)
            return fail("BOOT release attestation differs from its installed EFI image");
    } catch (...) {
        return fail("BOOT native installation attestation cannot be verified");
    }
    return true;
}

} // namespace infiltrator_native::worker
