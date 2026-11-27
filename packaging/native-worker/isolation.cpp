// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "isolation.h"
#include "runtime.h"
#include <cstring>
#include <linux/capability.h>
#include <linux/mount.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
namespace infiltrator_native::worker {
// Isolate trusted component build-only code from accidental host writes.
// A private mount namespace is mandatory: no unrestricted build fallback.
// Read-only mounts protect dpkg, APT, /System, the ESP and all other host
// filesystems; only this job's scratch, private temp and shared-memory areas
// are writable. This is not a sandbox for arbitrary hostile root code.
static void isolate_mount_writes(const fs::path &folder) {
    if (geteuid() != 0 || !folder.is_absolute() || fs::canonical(folder) != folder ||
        folder == "/" || folder == "/tmp" || folder == "/var/tmp" ||
        folder.string().rfind("/tmp/", 0) == 0 || folder.string().rfind("/var/tmp/", 0) == 0)
        throw std::runtime_error("Unsafe native build isolation directory");
    struct stat st{};
    if (lstat(folder.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0 || (st.st_mode & 022))
        throw std::runtime_error("Native build isolation requires root-owned private scratch");
    const auto tmp = folder / "tmp", shm = folder / "shm";
    fs::create_directory(tmp);
    fs::create_directory(shm);
    if (chmod(tmp.c_str(), 01777) || chmod(shm.c_str(), 01777))
        throw std::runtime_error("Cannot establish private temporary directories");
    auto checked = [](int result, const char *operation) {
        if (result != 0) throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
    };
    checked(unshare(CLONE_NEWNS), "Cannot create isolated build mount namespace");
    checked(mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr),
            "Cannot privatise build mount propagation");
    checked(mount(folder.c_str(), folder.c_str(), nullptr, MS_BIND, nullptr), "Cannot bind private scratch");
    checked(mount(tmp.c_str(), "/tmp", nullptr, MS_BIND, nullptr), "Cannot isolate /tmp");
    std::vector<fs::path> writable{folder, "/tmp"};
    if (fs::is_directory("/var/tmp") && !fs::is_symlink("/var/tmp")) {
        checked(mount(tmp.c_str(), "/var/tmp", nullptr, MS_BIND, nullptr), "Cannot isolate /var/tmp");
        writable.emplace_back("/var/tmp");
    }
    if (fs::is_directory("/dev/shm") && !fs::is_symlink("/dev/shm")) {
        checked(mount(shm.c_str(), "/dev/shm", nullptr, MS_BIND, nullptr), "Cannot isolate /dev/shm");
        writable.emplace_back("/dev/shm");
    }
    struct mount_attr attrs{};
    attrs.attr_set = MOUNT_ATTR_RDONLY;
    checked(static_cast<int>(syscall(SYS_mount_setattr, AT_FDCWD, "/", AT_RECURSIVE, &attrs, sizeof(attrs))),
            "Cannot protect host mounts during build");
    attrs.attr_set = 0;
    attrs.attr_clr = MOUNT_ATTR_RDONLY;
    for (const auto &path : writable)
        checked(static_cast<int>(syscall(SYS_mount_setattr, AT_FDCWD, path.c_str(), 0, &attrs, sizeof(attrs))),
                "Cannot restore private scratch write access");
    // No mount capability is needed after the namespace is established.
    checked(prctl(PR_CAPBSET_DROP, CAP_SYS_ADMIN, 0, 0, 0), "Cannot drop build mount capability");
    struct __user_cap_header_struct header{};
    struct __user_cap_data_struct data[2]{};
    header.version = _LINUX_CAPABILITY_VERSION_3;
    checked(static_cast<int>(syscall(SYS_capget, &header, data)), "Cannot read build capabilities");
    constexpr auto bit = 1U << (CAP_SYS_ADMIN % 32);
    data[CAP_SYS_ADMIN / 32].effective &= ~bit;
    data[CAP_SYS_ADMIN / 32].permitted &= ~bit;
    data[CAP_SYS_ADMIN / 32].inheritable &= ~bit;
    checked(static_cast<int>(syscall(SYS_capset, &header, data)), "Cannot restrict build capabilities");
    checked(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0), "Cannot prevent build privilege elevation");
}
int isolated_build_entry(int argc, char **argv) {
    if (argc < 4 || argv[3][0] != '/') return 1;
    isolate_mount_writes(argv[2]);
    execv(argv[3], argv + 3);
    return 1;
}

} // namespace infiltrator_native::worker
