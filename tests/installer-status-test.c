#define _POSIX_C_SOURCE 200809L
#include "../packaging/installer-status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); } } while (0)

static char temporary[] = "/tmp/infiltrator-status-XXXXXX";
static void expect(const char *record, size_t size, bool valid, int progress)
{
    FILE *file = fopen(temporary, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(record, 1U, size, file) == size);
    CHECK(fclose(file) == 0);
    InstallerStatus result;
    memset(&result, 0x5a, sizeof(result));
    InstallerStatus previous = result;
    CHECK(installer_status_read(temporary, &result) == valid);
    if (valid) CHECK(result.progress == progress);
    else CHECK(memcmp(&result, &previous, sizeof(result)) == 0);
}
#define RECORD(text, valid, progress) expect(text, sizeof(text) - 1U, valid, progress)
int main(void)
{
    int descriptor = mkstemp(temporary);
    CHECK(descriptor >= 0);
    close(descriptor);
    RECORD("state=complete\nprogress=100\nstage=Ready\ndetail=Done\n", true, 100);
    RECORD("state=running\r\nprogress=-1\r\nstage=\r\ndetail=\r\nfuture=value\r\n", true, -1);
    RECORD("state=queued\nprogress=0\nstage=Waiting\ndetail=Pending\n", true, 0);
    RECORD("state=complete\nprogress=100\nstage=Ready\n", false, 0);
    RECORD("state=complete\nprogress=100\nstage=Ready\ndetail=Done", false, 0);
    RECORD("state=running\nprogress=garbage\nstage=Work\ndetail=Work\n", false, 0);
    RECORD("state=running\nprogress=999999999999999999999\nstage=Work\ndetail=Work\n", false, 0);
    RECORD("state=running\nprogress=101\nstage=Work\ndetail=Work\n", false, 0);
    RECORD("state=running\nprogress=-2\nstage=Work\ndetail=Work\n", false, 0);
    RECORD("state=running\nprogress=5x\nstage=Work\ndetail=Work\n", false, 0);
    RECORD("state=complete\nstate=failed\nprogress=100\nstage=Ready\ndetail=Done\n", false, 0);
    RECORD("state=complete\0\nprogress=100\nstage=Ready\ndetail=Done\n", false, 0);
    char long_record[4096];
    memset(long_record, 'x', sizeof(long_record));
    memcpy(long_record, "state=", 6U);
    long_record[sizeof(long_record) - 1U] = '\n';
    expect(long_record, sizeof(long_record), false, 0);
    CHECK(unlink(temporary) == 0);
    puts("Status record contracts passed");
    return EXIT_SUCCESS;
}
