// SPDX-License-Identifier: GPL-3.0-or-later
#include "dependencies.h"
namespace infiltrator_native::worker {
Strings application_build_dependencies(const Entry &e) {
    if (e.id == "system-monitor")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "pkg-config", "libgtk-3-dev"};
    if (e.id == "calculator")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "debhelper", "libgtk-4-dev",
                "pkg-config", "xvfb", "xauth", "tar", "gzip", "ca-certificates", "curl",
                "fontconfig", "python3", "python3-reportlab"};
    if (e.id == "calendar")
        return {"build-essential", "binutils", "dpkg-dev", "debhelper", "desktop-file-utils",
                "gettext", "gir1.2-glib-2.0-dev", "gjs", "libglib2.0-dev", "libgtk-3-dev",
                "libicu-dev", "nodejs", "pkg-config", "xz-utils"};
    if (e.id == "defragmenter")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "python3",
                "gir1.2-gtk-3.0", "libgtk-3-dev", "policykit-1", "ca-certificates",
                "desktop-file-utils", "udisks2", "util-linux", "libsqlite3-dev", "libssl-dev"};
    if (e.id == "files")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "ninja-build", "pkg-config",
                "xvfb", "xauth", "dbus-daemon", "libgtk-4-dev", "libarchive-dev"};
    throw std::runtime_error("No application build dependency contract");
}
static Strings missing_packages(const Strings &packages) {
    Strings missing;
    for (const auto &pkg : packages)
        if (pkg_query(pkg, std::string("$") + "{Status}") != "install ok installed")
            missing.push_back(pkg);
    return missing;
}
static Strings build_dependency_packages(const Entry &e) {
    if (e.kind == "application") return application_build_dependencies(e);
    Strings packages{"build-essential", "binutils", "dpkg-dev"};
    if (e.kind == "calendar" || e.kind == "clock")
        packages.insert(packages.end(), {"pkg-config", "jq", "ghostscript", "gettext", "git",
                                         "libglib2.0-dev", "libicu-dev"});
    if (e.slug().rfind("filesystem-", 0) == 0)
        packages.insert(packages.end(), {"cmake", "git", "pkg-config", "libssl-dev", "libsqlite3-dev"});
    if (e.id == "exfat")
        packages.insert(packages.end(), {"exfatprogs", "dosfstools"});
    if (e.id == "btrfs") packages.push_back("btrfs-progs");
    if (e.id == "xfs") packages.push_back("xfsprogs");
    if (e.id == "ext2" || e.id == "ext3" || e.id == "ext4")
        packages.push_back("e2fsprogs");
    if (e.id == "fat12" || e.id == "fat16" || e.id == "fat32")
        packages.push_back("dosfstools");
    if (e.id == "minix") packages.push_back("util-linux");
    if (e.id == "ntfs")
        packages.insert(packages.end(), {"autoconf", "automake", "libtool", "libgcrypt20-dev"});
    return packages;
}
static bool can_build(const Entry &e) {
    if (!missing_packages(build_dependency_packages(e)).empty()) return false;
    if (e.kind == "application") {
        if (e.id == "files")
            return execute({"pkg-config", "--exists", "gtk4 >= 4.10", "libarchive"}) == 0;
        if (e.id == "system-monitor")
            return execute({"pkg-config", "--exists", "gtk+-3.0 >= 3.22"}) == 0;
        if (e.id == "calculator")
            return execute({"pkg-config", "--exists", "gtk4"}) == 0;
        if (e.id == "calendar")
            return execute({"/usr/bin/dpkg-checkbuilddeps", "-d",
                "debhelper-compat (= 13), desktop-file-utils, gettext, gir1.2-glib-2.0-dev, gjs, libglib2.0-dev, libgtk-3-dev, libicu-dev, nodejs, pkg-config, xz-utils", "-c", "", "/dev/null"}) == 0;
        return true;
    }
    if ((e.kind == "calendar" || e.kind == "clock") &&
        execute({"pkg-config", "--exists", "glib-2.0", "icu-i18n"}) != 0)
        return false;
    if (e.slug().rfind("filesystem-", 0) == 0 &&
        execute({"/usr/bin/pkg-config", "--exists", "openssl", "sqlite3"}) != 0)
        return false;
    if (e.id == "ntfs") {
        const std::string status = std::string("$") + "{Status}";
        if (pkg_query("libntfs-3g89t64", status) != "install ok installed" &&
            pkg_query("libntfs-3g89", status) != "install ok installed")
            return false;
    }
    return true;
}
// Dependency transactions run before any admitted build can start.
void prepare_build_dependencies(const Entry &entry, Status &status) {
    if (can_build(entry)) return;
    const auto missing = missing_packages(build_dependency_packages(entry));
    if (!missing.empty()) {
        status.write("running", 8, "Preparing build prerequisites",
                     "Serial APT transaction before parallel compilation");
        Strings command{"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1",
            "install", "-y", "--no-remove", "--no-install-recommends"};
        command.insert(command.end(), missing.begin(), missing.end());
        required(command, status.root / (entry.slug() + "-dependencies.log"));
    }
    if (!can_build(entry))
        throw std::runtime_error("Native prerequisites unavailable; refusing unscheduled build");
}

} // namespace infiltrator_native::worker
