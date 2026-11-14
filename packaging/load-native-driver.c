// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static bool trusted(int fd, bool directory)
{
    struct stat st;

    if (fd < 0 || fstat(fd, &st) != 0 || st.st_uid != 0 || (st.st_mode & 022))
        return false;
    return directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);
}

int main(int argc, char **argv)
{
    struct utsname system;
    unsigned int major, minor, patch;
    char trailing;
    char runtime[sizeof(system.release) + sizeof("InfiltratorOS-")];
    const char *parents[] = {"/System", "Kernel", "Runtime", runtime};
    int directory = AT_FDCWD;

    if (argc != 2 || geteuid() != 0 || uname(&system) != 0)
        return 1;
    if (sscanf(system.release, "%u.%u.%u%c", &major, &minor, &patch, &trailing) != 3 ||
        strspn(system.release, "0123456789.") != strlen(system.release) ||
        (major == 0 && minor == 1 && patch < 6) ||
        (major == 0 && minor == 0))
        return 0;
    snprintf(runtime, sizeof(runtime), "InfiltratorOS-%s", system.release);
    for (size_t index = 0; index < sizeof(parents) / sizeof(parents[0]); ++index) {
        int child = openat(directory, parents[index], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

        if (directory != AT_FDCWD)
            close(directory);
        if (!trusted(child, true))
            return 1;
        directory = child;
    }
    int loader = openat(directory, "modprobe", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);

    close(directory);
    if (!trusted(loader, false))
        return 1;
    char *const args[] = {"modprobe", "-q", "-b", "--", argv[1], NULL};

    fexecve(loader, args, environ);
    return 1;
}
