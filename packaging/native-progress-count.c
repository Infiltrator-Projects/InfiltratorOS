/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native-progress-count.h"
#include "installer-status.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The four native application workers are launched separately by postinst.
 * All other workers are named by this run's sealed provider manifests. */
enum { NATIVE_APP_JOBS = 4, TEMPORAL_JOBS = 54, BASE_FS_JOBS = 5, OPTIONAL_FS_JOBS = 18 };

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool append_job(NativeProgressJobs *jobs, const char *slug)
{
    if (jobs->count >= NATIVE_PROGRESS_MAX_JOBS) return false;
    for (int index = 0; index < jobs->count; ++index)
        if (strcmp(jobs->slugs[index], slug) == 0) return false;
    const size_t length = strlen(slug);
    if (length == 0 || length >= NATIVE_PROGRESS_SLUG_MAX) return false;
    memcpy(jobs->slugs[jobs->count++], slug, length + 1U);
    return true;
}

static int count_manifest(const char *status_directory, const char *name,
                          bool temporal, int maximum, NativeProgressJobs *jobs)
{
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/../%s", status_directory, name) < 0 ||
        strlen(status_directory) + strlen(name) + sizeof("/../") >= sizeof(path))
        return -1;
    FILE *stream = fopen(path, "r");
    if (!stream) return -1;
    int count = 0;
    char line[NATIVE_PROGRESS_SLUG_MAX];
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
        if (++count > maximum || !append_job(jobs, line))
            goto invalid;
    }
    if (ferror(stream)) goto invalid;
    (void)fclose(stream);
    return count;
invalid:
    (void)fclose(stream);
    return -1;
}

int native_progress_load_jobs(const char *status_directory, NativeProgressJobs *jobs)
{
    if (!status_directory || !*status_directory || !jobs) return -1;
    memset(jobs, 0, sizeof(*jobs));
    static const char *const apps[] = {
        "system-monitor", "calculator", "files", "defragmenter"
    };
    for (int index = 0; index < NATIVE_APP_JOBS; ++index)
        if (!append_job(jobs, apps[index])) return -1;
    const int temporal = count_manifest(status_directory, "providers.list", true,
                                         TEMPORAL_JOBS, jobs);
    const int baseline = count_manifest(status_directory, "filesystems.list", false,
                                         BASE_FS_JOBS, jobs);
    const int additional = count_manifest(status_directory, "filesystems-optional.list",
                                           false, OPTIONAL_FS_JOBS, jobs);
    if (temporal != TEMPORAL_JOBS || baseline != BASE_FS_JOBS ||
        additional < 0 || additional > OPTIONAL_FS_JOBS) return -1;
    return jobs->count;
}

int native_progress_total_jobs(const char *status_directory)
{
    NativeProgressJobs jobs;
    return native_progress_load_jobs(status_directory, &jobs);
}

const char *native_progress_run_status(const char *status_directory)
{
    NativeProgressJobs jobs;
    if (native_progress_load_jobs(status_directory, &jobs) < 0)
        return "running";
    bool failed = false;
    for (int index = 0; index < jobs.count; ++index) {
        char path[4096];
        InstallerStatus status;
        if (snprintf(path, sizeof(path), "%s/%s-install.status", status_directory,
                     jobs.slugs[index]) >= (int)sizeof(path) ||
            !installer_status_read(path, &status))
            return "running";
        if (strcmp(status.state, "failed") == 0)
            failed = true;
        else if (strcmp(status.state, "complete") != 0)
            return "running";
    }
    return failed ? "failed" : "complete";
}
