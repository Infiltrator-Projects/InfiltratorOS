/* SPDX-License-Identifier: GPL-3.0-or-later
 * Count the exact providers launched for one native installation run.
 * Returns -1 until the selected-job manifest is available and well-formed.
 */
#ifndef INFILTRATOR_NATIVE_PROGRESS_COUNT_H
#define INFILTRATOR_NATIVE_PROGRESS_COUNT_H
enum { NATIVE_PROGRESS_MAX_JOBS = 83, NATIVE_PROGRESS_SLUG_MAX = 128 };

typedef struct {
    int count;
    char slugs[NATIVE_PROGRESS_MAX_JOBS][NATIVE_PROGRESS_SLUG_MAX];
} NativeProgressJobs;

int native_progress_load_jobs(const char *status_directory, NativeProgressJobs *jobs);
int native_progress_total_jobs(const char *status_directory);
const char *native_progress_run_status(const char *status_directory);
#endif
