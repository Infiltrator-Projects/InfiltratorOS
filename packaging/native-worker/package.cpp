// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "package.h"
#include "verification.h"
#include "filesystem.h"
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
namespace infiltrator_native::worker {
// Providers own native compilation; the orchestrator owns package transactions.
// APT waits for the actual Debian package-manager locks. Do not delete lock
// files or use a check-then-dpkg sequence which races external package tools.
Strings package_install_command(const fs::path &package, bool calendar) {
    Strings command{"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "install",
                    "-y", "--no-remove", "--reinstall"};
    // Preserve Calendar's independent historical version-reset contract.
    if (calendar) command.push_back("--allow-downgrades");
    command.push_back(package.string());
    return command;
}
fs::path native_package_from(const fs::path &folder, const Entry &entry,
                                    const Asset &asset) {
    std::vector<fs::path> packages;
    for (const auto &item : fs::directory_iterator(folder))
        if (item.path().extension() == ".deb") packages.push_back(item.path());
    if (packages.size() != 1 || !regular(packages.front()))
        throw std::runtime_error("Native build produced no unique regular DEB");
    const auto package = packages.front();
    const std::map<std::string, std::string> expected{
        {"Package", entry.package},
        {"Architecture", "amd64"}};
    for (const auto &[name, value] : expected)
        if (output({"/usr/bin/dpkg-deb", "-f", package.string(), name}) != value)
            throw std::runtime_error("Invalid component-built DEB identity");
    const auto version = output({"/usr/bin/dpkg-deb", "-f", package.string(), "Version"});
    if (entry.kind == "application"
            ? !application_package_matches(entry, asset, "install ok installed\t" + version)
            : version != asset.version + "+native1")
        throw std::runtime_error("Invalid component-built DEB version");
    return package;
}
// Validate the package before changing the installed system. The independent
// release owns compilation, while the OS owns the package transaction.
static fs::path staged_resource(const fs::path &root, const std::string &absolute) {
    const auto path = root / fs::path(absolute).relative_path();
    if (!regular(path)) throw std::runtime_error("Native DEB payload missing: " + absolute);
    const auto relative = fs::canonical(path).lexically_relative(fs::canonical(root));
    if (relative.empty() || *relative.begin() == "..")
        throw std::runtime_error("Native DEB resource escapes staging: " + absolute);
    return path;
}
void verify_native_payload(const fs::path &package, const Entry &entry, const Asset &asset,
                                  const fs::path &scratch) {
    TempDir inspection(scratch, "verify-payload-");
    required({"/usr/bin/dpkg-deb", "-x", package.string(), inspection.path.string()});
    const auto resource = staged_resource(inspection.path, entry.resource);
    if (entry.kind == "application") {
        if (access(resource.c_str(), X_OK) != 0)
            throw std::runtime_error("Staged native application is not executable");
        for (const auto &path : required_paths(entry.id))
            (void)staged_resource(inspection.path, path);
        return;
    }
    const std::map<std::string, std::string> expected{
        {"X-Infiltrator-Native-Profile", "aggressive"},
        {"X-Infiltrator-PGO", "trained-two-pass"},
        {"X-Infiltrator-Source-Revision", asset.source_revision},
        {"X-Infiltrator-Resource-SHA256", digest(resource)}};
    for (const auto &[field_name, value] : expected)
        if (value.empty() || output({"/usr/bin/dpkg-deb", "-f", package.string(), field_name}) != value)
            throw std::runtime_error("Unqualified staged provider metadata: " + field_name);
    const auto manual = staged_resource(inspection.path,
        "/System/Resources/Documentation/" + entry.repo + ".pdf");
    std::ifstream pdf(manual, std::ios::binary);
    char magic[5];
    if (!pdf.read(magic, 5) || std::memcmp(magic, "%PDF-", 5) != 0)
        throw std::runtime_error("Staged provider manual is not a PDF");
    if (entry.slug().rfind("filesystem-", 0) == 0 && !validate_common_descriptor(resource, entry.id))
        throw std::runtime_error("Staged provider failed the canonical Common V2 descriptor contract");
}
// Debian package replacement recovery. Repack the currently installed
// package before touching it, so existing locally compiled versions do not
// depend on generic downloads. This is package-file recovery, not a rollback
// of arbitrary maintainer-script effects or the whole operating system.
struct PreviousPackage {
    fs::path folder, package;
    std::string version, architecture, sha;
};
static std::optional<PreviousPackage> capture_previous_package(const Entry &entry,
                                                               const fs::path &recovery) {
    const std::string format = std::string("$") + "{Status}\t$" + "{Version}\t$" + "{Architecture}";
    const auto identity = tabs(pkg_query(entry.package, format));
    if (identity.empty() || identity[0].rfind("deinstall ", 0) == 0 ||
        identity[0].rfind("purge ", 0) == 0) return std::nullopt;
    if (identity.size() != 3 || identity[0] != "install ok installed")
        throw std::runtime_error("Existing package is not in a recoverable installed state: " + entry.package);
    TempDir snapshot(recovery, "rollback-" + entry.slug() + "-");
    required({"/bin/sh", "-c", "cd -- \"$1\" && exec /usr/bin/dpkg-repack \"$2\"",
              "native-rollback", snapshot.path.string(), entry.package},
             recovery / (entry.slug() + "-rollback-capture.log"));
    std::vector<fs::path> packages;
    for (const auto &item : fs::directory_iterator(snapshot.path))
        if (item.path().extension() == ".deb" && regular(item.path())) packages.push_back(item.path());
    if (packages.size() != 1 ||
        output({"/usr/bin/dpkg-deb", "-f", packages.front().string(), "Package"}) != entry.package ||
        output({"/usr/bin/dpkg-deb", "-f", packages.front().string(), "Version"}) != identity[1] ||
        output({"/usr/bin/dpkg-deb", "-f", packages.front().string(), "Architecture"}) != identity[2] ||
        tabs(pkg_query(entry.package, format)) != identity)
        throw std::runtime_error("Cannot verify previous package recovery archive");
    PreviousPackage previous{snapshot.path, packages.front(), identity[1], identity[2], digest(packages.front())};
    save(snapshot.path / "SHA256", previous.sha + "  " + previous.package.filename().string() + "\n");
    snapshot.path.clear(); // Keep the archive until install or recovery is verified.
    return previous;
}
void replace_deb_with_recovery(const Entry &entry, const fs::path &package,
                                      const fs::path &recovery,
                                      const std::function<void()> &verify) {
    auto previous = capture_previous_package(entry, recovery);
    auto discard = [&] {
        if (previous) {
            std::error_code ec;
            fs::remove_all(previous->folder, ec);
        }
    };
    try {
        required(package_install_command(package, entry.id == "calendar"),
                 recovery / (entry.slug() + "-package.log"));
        verify();
    } catch (const std::exception &error) {
        const std::string cause = error.what();
        if (!previous)
            throw std::runtime_error(cause + "; no previous installed package to restore");
        try {
            if (digest(previous->package) != previous->sha)
                throw std::runtime_error("Previous package archive digest changed");
            required(package_install_command(previous->package, true),
                     recovery / (entry.slug() + "-rollback-restore.log"));
            const auto installed = tabs(pkg_query(entry.package, std::string("$") +
                "{Status}\t$" + "{Version}\t$" + "{Architecture}"));
            if (installed != Strings{"install ok installed", previous->version, previous->architecture})
                throw std::runtime_error("Restored package identity/version differs");
            std::string verification;
            if (execute({"/usr/bin/dpkg", "--verify", entry.package}, &verification) != 0 ||
                !trim(verification).empty())
                throw std::runtime_error("Restored package failed dpkg file verification");
        } catch (const std::exception &restore_error) {
            throw std::runtime_error(cause + "; previous package recovery FAILED: " + restore_error.what() +
                                     "; recovery DEB retained at " + previous->package.string());
        }
        discard();
        throw std::runtime_error(cause + "; previous package restored and verified");
    }
    discard();
}

} // namespace infiltrator_native::worker
