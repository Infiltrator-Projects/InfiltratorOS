/* SPDX-License-Identifier: GPL-3.0-or-later
 * Verify the production GTK progress denominator from real run manifests.
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

static int write_manifest(const char *stage, const char *name,
                          const char *prefix, int count)
{
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", stage, name) >= (int)sizeof(path))
        return -1;
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    for (int n = 0; n < count; ++n)
        if (fprintf(file, "%s%d\n", prefix, n) < 0) {
            (void)fclose(file);
            return -1;
        }
    return fclose(file);
}

int main(void)
{
    char root[] = "/tmp/infiltrator-progress-test.XXXXXX";
    char stage[4096], status[4096], optional[4096];
    CHECK(mkdtemp(root) != NULL);
    CHECK(snprintf(stage, sizeof(stage), "%s/native-worker.test", root) < (int)sizeof(stage));
    CHECK(snprintf(status, sizeof(status), "%s/status", stage) < (int)sizeof(status));
    CHECK(snprintf(optional, sizeof(optional), "%s/filesystems-optional.list", stage) < (int)sizeof(optional));
    CHECK(mkdir(stage, 0700) == 0);
    CHECK(mkdir(status, 0700) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    CHECK(write_manifest(stage, "providers.list", "calendar-test-", 54) == 0);
    CHECK(write_manifest(stage, "filesystems.list", "filesystem-main-", 5) == 0);
    CHECK(write_manifest(stage, "filesystems-optional.list", "filesystem-extra-", 18) == 0);
    CHECK(native_progress_total_jobs(status) == 81);
    CHECK(write_manifest(stage, "filesystems-optional.list", "filesystem-extra-", 16) == 0);
    CHECK(native_progress_total_jobs(status) == 79);
    CHECK(write_manifest(stage, "filesystems-optional.list", "filesystem-extra-", 0) == 0);
    CHECK(native_progress_total_jobs(status) == 63);
    CHECK(write_manifest(stage, "filesystems-optional.list", "filesystem-extra-", 18) == 0);
    CHECK(native_progress_total_jobs(status) == 81);
    FILE *file = fopen(optional, "w");
    CHECK(file != NULL);
    CHECK(fputs("filesystem-extra-0\nfilesystem-invalid/name\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    CHECK(unlink(optional) == 0);
    CHECK(native_progress_total_jobs(status) == -1);
    char path[4096];
    CHECK(snprintf(path, sizeof(path), "%s/providers.list", stage) < (int)sizeof(path));
    CHECK(unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/filesystems.list", stage) < (int)sizeof(path));
    CHECK(unlink(path) == 0);
    CHECK(rmdir(status) == 0);
    CHECK(rmdir(stage) == 0);
    CHECK(rmdir(root) == 0);
    puts("Native installer manifest totals passed (63, 79, 81, invalid and missing lists)");
    return 0;
}
