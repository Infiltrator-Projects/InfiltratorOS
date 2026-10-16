#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "../packaging/installer-process.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #condition, errno); \
    return EXIT_FAILURE; } } while (0)

static int refuse_setup(void *context)
{
    (void)context;
    errno = EACCES;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--marker") == 0) {
        int inherited = atoi(argv[3]);
        if (fcntl(inherited, F_GETFD) >= 0 || errno != EBADF) return EXIT_FAILURE;
        FILE *marker = fopen(argv[2], "w");
        if (!marker) return EXIT_FAILURE;
        return fclose(marker) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    CHECK(argc == 1);
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1U);
    CHECK(length > 0 && (size_t)length < sizeof(executable) - 1U);
    executable[length] = '\0';
    char *const absent[] = { "/nonexistent/infiltrator-fixture", NULL };
    CHECK(installer_spawn_detached(absent[0], absent, NULL, NULL) == -1 && errno == ENOENT);
    char *const child[] = { "/bin/true", NULL };
    CHECK(installer_spawn_detached(child[0], child, refuse_setup, NULL) == -1 && errno == EACCES);
    CHECK(installer_spawn_detached(child[0], child, NULL, NULL) == 0);
    char *const fails_after_exec[] = { "/bin/false", NULL };
    CHECK(installer_spawn_detached(fails_after_exec[0], fails_after_exec, NULL, NULL) == 0);
    CHECK(installer_spawn_detached(NULL, child, NULL, NULL) == -1 && errno == EINVAL);

    char directory[] = "/tmp/infiltrator-process-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char marker[4096];
    CHECK(snprintf(marker, sizeof(marker), "%s/marker", directory) < (int)sizeof(marker));
    int original = open("/dev/null", O_RDONLY);
    CHECK(original >= 0);
    /* The shared helper must close parent descriptors even above the old cap. */
    int descriptor = fcntl(original, F_DUPFD, 5000);
    close(original);
    CHECK(descriptor >= 5000);
    char descriptor_text[32];
    snprintf(descriptor_text, sizeof(descriptor_text), "%d", descriptor);
    char *const marker_arguments[] = { executable, "--marker", marker, descriptor_text, NULL };
    CHECK(installer_spawn_detached(executable, marker_arguments, NULL, NULL) == 0);
    struct timespec pause = { .tv_nsec = 10000000L };
    for (int attempt = 0; attempt < 300 && access(marker, F_OK) != 0; ++attempt)
        (void)nanosleep(&pause, NULL);
    CHECK(access(marker, F_OK) == 0);
    CHECK(fcntl(descriptor, F_GETFD) >= 0);
    close(descriptor);
    CHECK(unlink(marker) == 0);
    CHECK(rmdir(directory) == 0);
    /* A package manager may close stdin; its replacement must not eat the pipe. */
    close(STDIN_FILENO);
    CHECK(installer_spawn_detached(child[0], child, NULL, NULL) == 0);
    CHECK(installer_spawn_detached(absent[0], absent, NULL, NULL) == -1 && errno == ENOENT);
    puts("Detached process contracts passed");
    return EXIT_SUCCESS;
}
