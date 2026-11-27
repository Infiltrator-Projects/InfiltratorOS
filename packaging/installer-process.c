// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "installer-process.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void child_failure(int descriptor, int error)
{
    const unsigned char *cursor = (const unsigned char *)&error;
    size_t remaining = sizeof(error);
    while (remaining != 0U) {
        ssize_t written = write(descriptor, cursor, remaining);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    _exit(127);
}

/* Close the entire Linux descriptor table except the exec notification pipe.
 * Unlike an RLIMIT_NOFILE scan, close_range also closes descriptors opened
 * before the limit was reduced, and avoids one syscall per unused slot. */
static int close_extra_descriptors(int retained)
{
    if (retained > STDERR_FILENO + 1 &&
        close_range(STDERR_FILENO + 1U, (unsigned int)retained - 1U, 0) != 0)
        return -1;
    return close_range((unsigned int)retained + 1U, UINT_MAX, 0);
}

int installer_redirect_io(const char *log_path)
{
    int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (input < 0) return -1;
    int log = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log < 0) {
        int error = errno;
        close(input);
        errno = error;
        return -1;
    }
    int result = 0;
    if (dup2(input, STDIN_FILENO) < 0 ||
        dup2(log, STDOUT_FILENO) < 0 ||
        dup2(log, STDERR_FILENO) < 0)
        result = -1;
    if (result == 0 && (fcntl(STDIN_FILENO, F_SETFD, 0) < 0 ||
                        fcntl(STDOUT_FILENO, F_SETFD, 0) < 0 ||
                        fcntl(STDERR_FILENO, F_SETFD, 0) < 0)) result = -1;
    int error = errno;
    if (input > STDERR_FILENO) close(input);
    if (log > STDERR_FILENO) close(log);
    errno = error;
    return result;
}

int installer_spawn_detached(const char *executable, char *const arguments[],
                             InstallerChildSetup setup, void *context)
{
    if (!executable || !arguments || !arguments[0]) {
        errno = EINVAL;
        return -1;
    }
    int notification[2];
    // Set close-on-exec atomically, including when another thread forks.
    if (pipe2(notification, O_CLOEXEC) != 0) return -1;
    for (size_t index = 0U; index < 2U; ++index) {
        int descriptor = notification[index];
        if (descriptor > STDERR_FILENO) continue;
        int result = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        if (result < 0) {
            int error = errno;
            close(notification[0]);
            close(notification[1]);
            errno = error;
            return -1;
        }
        close(descriptor);
        notification[index] = result;
    }
    pid_t first = fork();
    if (first < 0) {
        int error = errno;
        close(notification[0]);
        close(notification[1]);
        errno = error;
        return -1;
    }
    if (first == 0) {
        close(notification[0]);
        if (setsid() < 0) child_failure(notification[1], errno);
        pid_t second = fork();
        if (second < 0) child_failure(notification[1], errno);
        if (second > 0) _exit(0);
        if (chdir("/") != 0) child_failure(notification[1], errno);
        if (setup && setup(context) != 0)
            child_failure(notification[1], errno ? errno : EIO);
        if (close_extra_descriptors(notification[1]) != 0)
            child_failure(notification[1], errno);
        execv(executable, arguments);
        child_failure(notification[1], errno);
    }

    close(notification[1]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(first, &status, 0); } while (waited < 0 && errno == EINTR);
    int wait_error = errno;
    int child_error = 0;
    unsigned char *cursor = (unsigned char *)&child_error;
    size_t received = 0U;
    while (received < sizeof(child_error)) {
        ssize_t got = read(notification[0], cursor + received,
                           sizeof(child_error) - received);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) {
            int error = errno;
            close(notification[0]);
            errno = error;
            return -1;
        }
        if (got == 0) break;
        received += (size_t)got;
    }
    close(notification[0]);
    if (received != 0U) {
        errno = received == sizeof(child_error) ? child_error : EIO;
        return -1;
    }
    if (waited != first || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = waited < 0 ? wait_error : EIO;
        return -1;
    }
    return 0;
}
