/* Selected native installer version must match the installed Debian package,
 * including the product's exact native version suffix policy. */
#define _POSIX_C_SOURCE 200809L
#include "../packaging/verify-native-package.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Native identity failed at %d: %s\n", __LINE__, #x); return EXIT_FAILURE; } } while (0)
int main(void)
{
    const char *files = "https://github.com/Infiltrator-Projects/Files/releases/download/v1.2.3/files-1.2.3-native-installer.run";
    const char *good = "install ok installed\n1.2.3+native1\n";
    CHECK(native_package_identity_matches(files, "files-", good, "+native1"));
    CHECK(!native_package_identity_matches(files, "files-", "install ok installed\n1.2.2+native1\n", "+native1"));
    CHECK(!native_package_identity_matches(files, "files-", "deinstall ok config-files\n1.2.3+native1\n", "+native1"));
    CHECK(!native_package_identity_matches(files, "files-", "install ok installed\n1.2.3\n", "+native1"));
    CHECK(!native_package_identity_matches(files, "system-monitor-", good, "+native1"));
    CHECK(!native_package_identity_matches("files-1.2.3-native-installer.run", "files-", good, "+native1"));
    CHECK(!native_package_identity_matches("https://example.com/files-1.2.3+bad-native-installer.run", "files-", good, "+native1"));
    CHECK(!native_package_identity_matches("https://example.com/files-1..3-native-installer.run", "files-", good, "+native1"));
    CHECK(!native_package_matches_release(files, "files-", "infiltrator-nonexistent-package", "+native1"));
    const char *monitor = "https://github.com/Infiltrator-Projects/System-Monitor/releases/download/v1.0.241/system-monitor-1.0.241-native-installer.run";
    const char *monitor_version = "install ok installed\n1.0.241\n";
    CHECK(native_package_identity_matches(monitor, "system-monitor-", monitor_version, ""));
    CHECK(!native_package_identity_matches(monitor, "system-monitor-", "install ok installed\n1.0.241+native1\n", ""));
    CHECK(!native_package_identity_matches(monitor, "system-monitor-", monitor_version, "+native1"));
    puts("Native release version/status contract passed for Files and System Monitor");
    return EXIT_SUCCESS;
}
