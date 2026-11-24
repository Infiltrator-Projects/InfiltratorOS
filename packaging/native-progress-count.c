/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native-progress-count.h"
#include "installer-status.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Only the signed dispatcher decides which jobs exist in this installation.
 * The complete catalogues are for release verification, NOT progress counting. */
static bool starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool valid_slug(const char *slug)
{
    if (strcmp(slug, "system-monitor") == 0 ||
        strcmp(slug, "calculator") == 0 ||
        strcmp(slug, "calendar") == 0 ||
        strcmp(slug, "boot") == 0 ||
        strcmp(slug, "files") == 0 ||
        strcmp(slug, "defragmenter") == 0) return true;
    return starts_with(slug, "calendar-") || starts_with(slug, "clock-") ||
           starts_with(slug, "filesystem-");
}

/* The phase is a single atomically replaced token, not a simulated timer.
 * Malformed, unavailable or stale (different stage) data cannot advance it. */
NativeProgressPhase native_progress_phase(const char *status_directory)
{
    if (!status_directory || !*status_directory) return NATIVE_PHASE_UNKNOWN;
    char path[4096], token[64];
    if (snprintf(path, sizeof(path), "%s/../phase", status_directory) >=
        (int)sizeof(path)) return NATIVE_PHASE_UNKNOWN;
    FILE *stream = fopen(path, "r");
    if (!stream) return NATIVE_PHASE_UNKNOWN;
    bool valid = fgets(token, sizeof(token), stream) != NULL && fgetc(stream) == EOF &&
                 !ferror(stream);
    if (fclose(stream) != 0) valid = false;
    if (!valid) return NATIVE_PHASE_UNKNOWN;
    static const char *const names[] = {
        "", "fonts\n", "icons\n", "host\n", "releases\n", "components\n", "finalising\n"
    };
    for (unsigned int i = 1; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(token, names[i]) == 0) return (NativeProgressPhase)i;
    return NATIVE_PHASE_UNKNOWN;
}

int native_progress_load_jobs(const char *status_directory, NativeProgressJobs *jobs)
{
    if (!status_directory || !*status_directory || !jobs) return -1;
    memset(jobs, 0, sizeof(*jobs));
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/../selected.list", status_directory) >=
        (int)sizeof(path)) return -1;
    FILE *stream = fopen(path, "r");
    if (!stream) return -1;
    char line[NATIVE_PROGRESS_SLUG_MAX];
    while (fgets(line, sizeof(line), stream)) {
        size_t length = strlen(line);
        if (length < 2 || line[length - 1] != '\n') goto invalid;
        line[--length] = '\0';
        if (strspn(line, "abcdefghijklmnopqrstuvwxyz0123456789-") != length ||
            !valid_slug(line) || jobs->count == NATIVE_PROGRESS_MAX_JOBS) goto invalid;
        for (int index = 0; index < jobs->count; ++index)
            if (strcmp(line, jobs->slugs[index]) == 0) goto invalid;
        memcpy(jobs->slugs[jobs->count++], line, length + 1U);
    }
    if (ferror(stream)) goto invalid;
    (void)fclose(stream);
    return jobs->count;
invalid:
    (void)fclose(stream);
    return -1;
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
