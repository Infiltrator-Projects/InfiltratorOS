/* SPDX-License-Identifier: GPL-3.0-or-later
 * Count the exact providers launched for one native installation run.
 * Returns -1 until all three manifests are available and well-formed.
 */
#ifndef INFILTRATOR_NATIVE_PROGRESS_COUNT_H
#define INFILTRATOR_NATIVE_PROGRESS_COUNT_H
int native_progress_total_jobs(const char *status_directory);
#endif
