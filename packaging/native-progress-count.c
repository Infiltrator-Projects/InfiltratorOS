/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native-progress-count.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The four native application workers are launched separately by postinst.
 * All other workers are named by this run's sealed provider manifests. */
enum { NATIVE_APP_JOBS = 4, MAX_MANIFEST_JOBS = 256 };

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int count_manifest(const char *status_directory, const char *name,
                          bool temporal)
{
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/../%s", status_directory, name) < 0 ||
        strlen(status_directory) + strlen(name) + sizeof("/../") >= sizeof(path))
        return -1;
    FILE *stream = fopen(path, "r");
    if (!stream) return -1;
    int count = 0;
    char line[128];
    while (fgets(line, sizeof(line), stream)) {
        size_t length = strlen(line);
        if (length < 3 || line[length - 1] != '\n')
            goto invalid;
        line[--length] = '\0';
        if (strspn(line, "abcdefghijklmnopqrstuvwxyz0123456789-") != length)
            goto invalid;
        if (temporal ?
            !(starts_with(line, "calendar-") || starts_with(line, "clock-")) :
            !starts_with(line, "filesystem-"))
            goto invalid;
        if (++count > MAX_MANIFEST_JOBS)
            goto invalid;
    }
    if (ferror(stream)) goto invalid;
    (void)fclose(stream);
    return count;
invalid:
    (void)fclose(stream);
    return -1;
}

int native_progress_total_jobs(const char *status_directory)
{
    if (!status_directory || !*status_directory) return -1;
    const int temporal = count_manifest(status_directory, "providers.list", true);
    const int baseline = count_manifest(status_directory, "filesystems.list", false);
    const int additional = count_manifest(status_directory, "filesystems-optional.list", false);
    if (temporal < 0 || baseline < 0 || additional < 0) return -1;
    return NATIVE_APP_JOBS + temporal + baseline + additional;
}
