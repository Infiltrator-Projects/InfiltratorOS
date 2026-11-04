/* Regression: selected native installer version must match an installed
 * Debian package, never merely an executable or a BUILD-INFO text string. */
#define _POSIX_C_SOURCE 200809L
#include "../packaging/verify-native-package.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Native identity: %s failed at %d\n", #x, __LINE__); return EXIT_FAILURE; } } while (0)
int main(void)
{
    const char *url = "https://github.com/Infiltrator-Projects/Files/releases/download/v1.2.3/files-1.2.3-native-installer.run";
    const char *ok = "install ok installed\n1.2.3+native1\n";
    CHECK(native_package_identity_matches(url, "files-", ok));
    CHECK(!native_package_identity_matches(url, "files-", "install ok installed\n1.2.2+native1\n"));
    CHECK(!native_package_identity_matches(url, "files-", "deinstall ok config-files\n1.2.3+native1\n"));
    CHECK(!native_package_identity_matches(url, "system-monitor-", ok));
    CHECK(!native_package_identity_matches("https://example.com/files-1.2.3-native-installer.run",
                                          "system-monitor-", ok));
    CHECK(!native_package_identity_matches("files-1.2.3-native-installer.run",
                                          "files-", ok));
    CHECK(!native_package_identity_matches("https://example.com/files-1.2.3+bad-native-installer.run",
                                          "files-", ok));
    CHECK(!native_package_identity_matches("https://example.com/files-1..3-native-installer.run",
                                          "files-", ok));
    CHECK(!native_package_matches_release(url, "files-", "infiltrator-nonexistent-package"));
    puts("Native application version and installed-status contract passed");
    return EXIT_SUCCESS;
}
