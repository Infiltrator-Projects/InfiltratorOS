#define _POSIX_C_SOURCE 200809L
#include "../packaging/native-release-version.h"
#include "../packaging/native-package-version.h"
#include <unistd.h>
int main(void)
{
    char version[128];
    const char *prefix = "https://github.com/Infiltrator-Projects/Files/releases/download/";
    if (!native_release_version_from_url(
            "https://github.com/Infiltrator-Projects/Files/releases/download/v1.2.3/files-1.2.3-native-installer.run",
            prefix, "files-", "-native-installer.run", version, sizeof(version)) ||
        strcmp(version, "1.2.3") != 0) return 1;
    if (native_release_version_from_url(
            "https://github.com/Infiltrator-Projects/Files/releases/download/v1.2.3/files-1.2.4-native-installer.run",
            prefix, "files-", "-native-installer.run", version, sizeof(version))) return 2;
    if (native_release_version_from_url(
            "https://github.com/Infiltrator-Projects/Files/releases/download/v1.2.3/other-1.2.3-native-installer.run",
            prefix, "files-", "-native-installer.run", version, sizeof(version))) return 3;
    char path[] = "/tmp/infiltrator-native-version-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 4;
    FILE *out = fdopen(fd, "w");
    if (!out) return 5;
    fputs("Package: files\nStatus: install ok installed\nVersion: 1.2.2+native1\n\n"
          "Package: unrelated\nStatus: install ok installed\nVersion: 1.2.3+native1\n\n",
          out);
    if (fclose(out) != 0) return 6;
    int result = native_package_version_matches(path, "files", "1.2.3+native1") ? 7 : 0;
    if (!native_package_version_matches(path, "files", "1.2.2+native1")) result = 8;
    if (native_package_version_matches(path, "not-installed", "1.2.2+native1")) result = 9;
    /* System Monitor's native installer retains the unsuffixed Debian version,
     * unlike Files. Reject a fabricated +native1 requirement for that product. */
    char monitor_version[128];
    const char *monitor_prefix =
        "https://github.com/Infiltrator-Projects/System-Monitor/releases/download/";
    if (!native_release_version_from_url(
            "https://github.com/Infiltrator-Projects/System-Monitor/releases/download/v1.0.241/system-monitor-1.0.241-native-installer.run",
            monitor_prefix, "system-monitor-", "-native-installer.run",
            monitor_version, sizeof(monitor_version)) ||
        strcmp(monitor_version, "1.0.241") != 0) result = 10;
    unlink(path);
    return result;
}
