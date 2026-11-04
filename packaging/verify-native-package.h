/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independently verify a locally installed native Debian release.  An
 * installer exit code and a BUILD-INFO text match are not package identity.
 */
#ifndef INFILTRATOR_VERIFY_NATIVE_PACKAGE_H
#define INFILTRATOR_VERIFY_NATIVE_PACKAGE_H
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool native_package_identity_matches(const char *url,
                                             const char *asset_prefix,
                                             const char *dpkg_record,
                                             const char *native_suffix)
{
    if (!url || !asset_prefix || !dpkg_record || !native_suffix ||
        (strcmp(native_suffix, "") != 0 &&
         strcmp(native_suffix, "+native1") != 0)) return false;
    const char *name = strrchr(url, '/');
    if (!name) return false;
    ++name;
    const size_t prefix = strlen(asset_prefix);
    const char suffix[] = "-native-installer.run";
    const size_t length = strlen(name), tail = sizeof(suffix) - 1U;
    if (length <= prefix + tail || strncmp(name, asset_prefix, prefix) != 0 ||
        strcmp(name + length - tail, suffix) != 0) return false;
    const char *version = name + prefix;
    const size_t count = length - prefix - tail;
    if (count >= 128U || !isdigit((unsigned char)version[0]) ||
        !isdigit((unsigned char)version[count - 1U])) return false;
    int dots = 0;
    for (size_t n = 0U; n < count; ++n) {
        if (version[n] == '.') {
            if (n == 0U || !isdigit((unsigned char)version[n-1U]) ||
                n + 1U == count || !isdigit((unsigned char)version[n+1U]))
                return false;
            ++dots;
        } else if (!isdigit((unsigned char)version[n])) return false;
    }
    if (dots != 2) return false;
    char expected[256];
    const int written = snprintf(expected, sizeof(expected),
                                 "install ok installed\n%.*s%s\n",
                                 (int)count, version, native_suffix);
    return written > 0 && (size_t)written < sizeof(expected) &&
           strcmp(expected, dpkg_record) == 0;
}

static bool native_package_matches_release(const char *url,
                                           const char *asset_prefix,
                                           const char *package,
                                           const char *native_suffix)
{
    int channel[2];
    if (pipe(channel) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(channel[0]); close(channel[1]);
        return false;
    }
    if (pid == 0) {
        close(channel[0]);
        if (dup2(channel[1], STDOUT_FILENO) < 0) _exit(127);
        close(channel[1]);
        execl("/usr/bin/dpkg-query", "dpkg-query", "-W",
              "-f=${Status}\n${Version}\n", package, (char *)NULL);
        _exit(127);
    }
    close(channel[1]);
    char record[256] = {0};
    size_t used = 0U;
    bool valid = true;
    for (;;) {
        if (used >= sizeof(record) - 1U) { valid = false; break; }
        ssize_t n = read(channel[0], record + used, sizeof(record) - 1U - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) valid = false;
        if (n <= 0) break;
        used += (size_t)n;
    }
    close(channel[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); }
    while (waited < 0 && errno == EINTR);
    return valid && waited == pid && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0 &&
           native_package_identity_matches(url, asset_prefix, record,
                                           native_suffix);
}
#endif
