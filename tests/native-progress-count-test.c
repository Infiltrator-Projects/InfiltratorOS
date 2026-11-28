/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise the actual selected-job inventory, including partial installs.
 */
#define _POSIX_C_SOURCE 200809L
#include "../packaging/native-progress-count.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Native progress count test failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int write_status(const char *path, const char *state)
{
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    int written = fprintf(file, "state=%s\nprogress=100\nstage=Finished\ndetail=Test\n", state);
    int closed = fclose(file);
    return written < 0 || closed != 0 ? -1 : 0;
}

int main(void)
{
    char root[] = "/tmp/infiltrator-progress-test.XXXXXX";
    char stage[4096], status[4096], manifest[4096], path[4096];
    CHECK(mkdtemp(root) != NULL);
    CHECK(snprintf(stage, sizeof(stage), "%s/native-worker.test", root) < (int)sizeof(stage));
    CHECK(snprintf(status, sizeof(status), "%s/status", stage) < (int)sizeof(status));
    CHECK(snprintf(manifest, sizeof(manifest), "%s/selected.list", stage) < (int)sizeof(manifest));
    CHECK(mkdir(stage, 0700) == 0);
    CHECK(mkdir(status, 0700) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    CHECK(native_progress_phase(status) == NATIVE_PHASE_UNKNOWN);
    CHECK(snprintf(path, sizeof(path), "%s/phase", stage) < (int)sizeof(path));
    const char *phases[] = {"unpacking", "fonts", "icons", "host", "releases", "components", "finalising"};
    for (unsigned int i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        FILE *phase = fopen(path, "w");
        CHECK(phase != NULL);
        CHECK(fprintf(phase, "%s\n", phases[i]) > 0);
        CHECK(fclose(phase) == 0);
        CHECK(native_progress_phase(status) == (NativeProgressPhase)(i + 1));
    }
    FILE *bad_phase = fopen(path, "w");
    CHECK(bad_phase != NULL);
    CHECK(fputs("components\nextra\n", bad_phase) >= 0);
    CHECK(fclose(bad_phase) == 0);
    CHECK(native_progress_phase(status) == NATIVE_PHASE_UNKNOWN);
    CHECK(unlink(path) == 0);
    FILE *file = fopen(manifest, "w");
    CHECK(file != NULL);
    CHECK(fputs("system-monitor\ncalculator\ncalendar\nfiles\ndefragmenter\n", file) >= 0);
    for (int i = 0; i < 54; ++i) CHECK(fprintf(file, "calendar-test-%d\n", i) > 0);
    for (int i = 0; i < 23; ++i) CHECK(fprintf(file, "filesystem-test-%d\n", i) > 0);
    CHECK(fputs("filesystem-bfs\nfilesystem-efs\nfilesystem-adfs\nfilesystem-befs\nfilesystem-fosfat\nfilesystem-hpfs\nfilesystem-omfs\nfilesystem-qnx6\nfilesystem-reiserfs\nfilesystem-sysv\nfilesystem-udf\nfilesystem-bcachefs\nfilesystem-cpm\nfilesystem-f2fs\nfilesystem-gfs2\nfilesystem-jfs\nfilesystem-nilfs2\nfilesystem-ocfs2\nfilesystem-vmfs\nfilesystem-vmfs6\nfilesystem-qnx4\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == 103);
    NativeProgressJobs jobs;
    CHECK(native_progress_load_jobs(status, &jobs) == 103);
    CHECK(strcmp(jobs.slugs[0], "system-monitor") == 0);
    CHECK(strcmp(jobs.slugs[2], "calendar") == 0);
    CHECK(strcmp(jobs.slugs[81], "filesystem-test-22") == 0);
    CHECK(strcmp(jobs.slugs[82], "filesystem-bfs") == 0);
    CHECK(strcmp(jobs.slugs[83], "filesystem-efs") == 0);
    CHECK(strcmp(jobs.slugs[101], "filesystem-vmfs6") == 0);
    CHECK(strcmp(jobs.slugs[102], "filesystem-qnx4") == 0);
    native_progress_free_jobs(&jobs);
    file = fopen(manifest, "a");
    CHECK(file != NULL);
    CHECK(fputs("boot\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_load_jobs(status, &jobs) == 104);
    CHECK(strcmp(jobs.slugs[103], "boot") == 0);
    CHECK(strcmp(native_progress_run_status(status), "running") == 0);
    for (int i = 0; i < jobs.count; ++i) {
        CHECK(snprintf(path, sizeof(path), "%s/%s-install.status", status,
                       jobs.slugs[i]) < (int)sizeof(path));
        CHECK(write_status(path, "complete") == 0);
    }
    CHECK(strcmp(native_progress_run_status(status), "complete") == 0);
    CHECK(write_status(path, "failed") == 0);
    CHECK(strcmp(native_progress_run_status(status), "failed") == 0);
    /* Failure in one worker is not a terminal run while any other worker
     * is still running. The post-install monitor must continue holding FD 9. */
    CHECK(snprintf(path, sizeof(path), "%s/%s-install.status", status,
                   jobs.slugs[0]) < (int)sizeof(path));
    CHECK(write_status(path, "running") == 0);
    CHECK(strcmp(native_progress_run_status(status), "running") == 0);
    CHECK(write_status(path, "complete") == 0);
    CHECK(strcmp(native_progress_run_status(status), "failed") == 0);
    CHECK(snprintf(path, sizeof(path), "%s/%s-install.status", status,
                   jobs.slugs[jobs.count - 1]) < (int)sizeof(path));
    CHECK(write_status(path, "complete\nstate=failed") == 0);
    CHECK(strcmp(native_progress_run_status(status), "running") == 0);
    for (int i = 0; i < jobs.count; ++i) {
        CHECK(snprintf(path, sizeof(path), "%s/%s-install.status", status,
                       jobs.slugs[i]) < (int)sizeof(path));
        CHECK(unlink(path) == 0);
    }

    native_progress_free_jobs(&jobs);
    file = fopen(manifest, "w");
    CHECK(file != NULL);
    CHECK(fputs("calendar\nfilesystem-ofs\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == 2);
    CHECK(strcmp(native_progress_run_status(status), "running") == 0);
    CHECK(snprintf(path, sizeof(path), "%s/calendar-install.status", status) < (int)sizeof(path));
    CHECK(write_status(path, "complete") == 0);
    CHECK(snprintf(path, sizeof(path), "%s/filesystem-ofs-install.status", status) < (int)sizeof(path));
    CHECK(write_status(path, "complete") == 0);
    CHECK(strcmp(native_progress_run_status(status), "complete") == 0);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/calendar-install.status", status) < (int)sizeof(path));
    CHECK(unlink(path) == 0);

    file = fopen(manifest, "w");
    CHECK(file != NULL);
    CHECK(fputs("calculator\ncalculator\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    file = fopen(manifest, "w");
    CHECK(file != NULL);
    CHECK(fputs("filesystem-invalid/name\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    file = fopen(manifest, "w");
    CHECK(file != NULL);
    CHECK(fputs("unexpected\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    file = fopen(manifest, "w");
    CHECK(file != NULL);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    CHECK(strcmp(native_progress_run_status(status), "running") == 0);
    file = fopen(manifest, "w");
    CHECK(file != NULL);
    for (int n = 0; n < 160; ++n) CHECK(fprintf(file, "filesystem-future-%d\n", n) > 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == 160);

    CHECK(unlink(manifest) == 0);
    CHECK(rmdir(status) == 0);
    CHECK(rmdir(stage) == 0);
    CHECK(rmdir(root) == 0);
    puts("Selected native installation status and progress contracts passed");
    return 0;
}
