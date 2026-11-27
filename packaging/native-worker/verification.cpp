// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "verification.h"
#include "boot.h"
#include "filesystem.h"
namespace infiltrator_native::worker {
Strings required_paths(const std::string &slug) {
    if (slug == "boot") return {};
    if (slug == "system-monitor") return {"/usr/share/doc/system-monitor/BUILD-INFO"};
    if (slug == "calculator")
        return {"/System/Resources/Documentation/Calculator.pdf",
                "/usr/share/applications/net.ssmith.calculator.desktop",
                "/usr/share/icons/hicolor/scalable/apps/calculator.svg",
                "/usr/share/metainfo/calculator.metainfo.xml"};
    if (slug == "calendar")
        return {"/usr/share/cinnamon/applets/calendar@the-infiltratr/metadata.json",
                "/usr/lib/x86_64-linux-gnu/libcalendar.so.0.0.0",
                "/usr/lib/x86_64-linux-gnu/girepository-1.0/Calendar-1.0.typelib",
                "/System/Resources/Documentation/Calendar.pdf"};
    if (slug == "files") return {"/usr/share/applications/files.desktop"};
    return {"/Programs/defragmenter-test-media",
            "/System/Commands/Defragmenter/defragmenter-operation-engine",
            "/System/Commands/Defragmenter/defragmenter-mapper",
            "/System/Resources/Documentation/Defragmenter.pdf"};
}
static bool application_proof(const Entry &entry) {
    // The published Calculator RUN validates its own PGO training.
    // Require a package-owned manual as well as the executable and OS
    // attestation; independent package-owned PGO evidence is still needed.
    if (entry.id == "calculator") {
        const auto manual = "/System/Resources/Documentation/Calculator.pdf";
        return regular(manual) &&
               output({"/usr/bin/dpkg-query", "-S", manual}) ==
                   std::string("calculator: ") + manual;
    }
    if (entry.id == "calendar") {
        const fs::path proof = "/usr/share/doc/calendar/BUILD-INFO";
        if (!regular(proof)) return false;
        const auto data = read_file(proof);
        return data.find("Build mode: local hardware-native PGO-trained ") != std::string::npos &&
               data.find("-march=native") != std::string::npos &&
               data.find("-fprofile-use") != std::string::npos;
    }
    if (entry.id == "defragmenter") {
        auto fields = tabs(pkg_query("defragmenter", std::string("$") +
            "{Status}\t$" + "{Version}\t$" + "{X-Defragmenter-Build}"));
        return fields.size() == 3 && fields[2] == "native";
    }
    fs::path proof = entry.id == "system-monitor" ?
        "/usr/share/doc/system-monitor/BUILD-INFO" :
        "/System/Resources/Documentation/Files-BUILD-INFO";
    if (entry.id == "files" && !regular(proof))
        proof = "/System/Resources/Documentation/Files/BUILD-INFO";
    if (!regular(proof)) return false;
    auto data = read_file(proof);
    return data.find("Profile: aggressive") != std::string::npos &&
           data.find("PGO: trained two-pass profile") != std::string::npos &&
           data.find("-march=native") != std::string::npos;
}
// Query only mandatory Debian fields. dpkg-query returns an empty custom field
// for Calculator/System Monitor/Files, and the existing tabs()/trim() helper
// legitimately drops its trailing separator. Demanding three fields caused a
// fully installed application to be falsely marked as failed.
bool application_package_matches(const Entry &entry, const Asset &asset,
                                        const std::string &package_record) {
    const auto fields = tabs(package_record);
    if (fields.size() != 2 || fields[0] != "install ok installed") return false;
    if (entry.id == "calendar") {
        const std::string prefix = asset.version + "+nativepgo";
        if (fields[1].rfind(prefix, 0) != 0) return false;
        const auto revision = fields[1].substr(prefix.size());
        return !revision.empty() && revision[0] != '0' &&
               revision.find_first_not_of("0123456789") == std::string::npos;
    }
    const std::string suffix =
        entry.id == "files" || entry.id == "defragmenter" ? "+native1" : "";
    return fields[1] == asset.version + suffix;
}
bool application_current(const Entry &entry, const Asset &asset,
                                bool evidence, std::string *reason) {
    auto fail = [&](const std::string &detail) {
        if (reason) *reason = detail;
        return false;
    };
    if (entry.id == "boot") return native_boot_current(asset, evidence, reason);
    const std::string identity = pkg_query(entry.package, std::string("$") +
        "{Status}\t$" + "{Version}");
    if (!application_package_matches(entry, asset, identity))
        return fail("Installed Debian package identity/version differs from verified release");
    if (!regular(entry.resource) || (entry.id != "boot" && access(entry.resource.c_str(), X_OK) != 0))
        return fail("Installed application executable is absent or not executable: " +
                    entry.resource);
    if (output({"/usr/bin/dpkg-query", "-S", entry.resource}) !=
        entry.package + ": " + entry.resource)
        return fail("Installed executable is not owned by the expected Debian package");
    if (!application_proof(entry))
        return fail("Independent native optimisation evidence is missing or invalid");
    for (const auto &p : required_paths(entry.id)) {
        if (!regular(p)) return fail("Expected application resource is missing: " + p);
        if (entry.id == "calendar" && output({"/usr/bin/dpkg-query", "-S", p}) !=
                                      entry.package + ": " + p)
            return fail("Expected Calendar resource is not owned by its package: " + p);
    }
    if (evidence) {
        fs::path file = ROOT / ("native-verified-" + entry.id + ".json");
        if (!regular(file)) return fail("InfiltratorOS native attestation is absent");
        try {
            Document proof = parse(read_file(file));
            if (str(proof.ptr, "version") != asset.version ||
                str(proof.ptr, "installer_sha256") != asset.sha ||
                str(proof.ptr, "program_sha256") != digest(entry.resource))
                return fail("Native attestation does not match the installed executable");
        } catch (...) { return fail("Native attestation cannot be verified"); }
    }
    std::string verification;
    if (execute({"/usr/bin/dpkg", "--verify", entry.package},
                &verification) != 0 ||
        !trim(verification).empty())
        return fail("dpkg verification rejected an installed application file");
    return true;
}
void verify_install(const Entry &entry, const Asset &asset, bool application_mode) {
    if (!application_mode) {
        if (!provider_current(entry, asset))
            throw std::runtime_error("Independent provider did not install its verified native SO and PDF"
                                     " with the required module export");
        return;
    }
    std::string verification_failure;
    if (!application_current(entry, asset, false, &verification_failure))
        throw std::runtime_error("Independent application installation contract failed: " +
                                 verification_failure);
    json_object *proof = json_object_new_object();
    json_object_object_add(proof, "version", json_object_new_string(asset.version.c_str()));
    json_object_object_add(proof, "installer_sha256", json_object_new_string(asset.sha.c_str()));
    std::string value = digest(entry.id == "boot" ? installed_boot_image() : fs::path(entry.resource));
    json_object_object_add(proof, "program_sha256", json_object_new_string(value.c_str()));
    if (entry.id == "boot")
        json_object_object_add(proof, "efi_inventory",
                               json_object_new_string(boot_efi_inventory(installed_boot_image()).c_str()));
    std::string content = json_object_to_json_string_ext(proof, JSON_C_TO_STRING_PLAIN);
    json_object_put(proof);
    TempDir staging(ROOT, "verified-native-");
    fs::path record = staging.path / "attestation";
    save(record, content);
    fs::rename(record, ROOT / ("native-verified-" + entry.id + ".json"));
}

} // namespace infiltrator_native::worker
