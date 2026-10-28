/* SPDX-License-Identifier: GPL-3.0-or-later
 * Verify downloaded native RUN bytes against the checksum in the HTTPS
 * release snapshot.  Only enabled for managed InfiltratorOS installations.
 * No shell is involved in hashing an executable path.
 */
#ifndef INFILTRATOR_VERIFY_NATIVE_DOWNLOAD_H
#define INFILTRATOR_VERIFY_NATIVE_DOWNLOAD_H

static bool verify_native_download(const char *directory, const char *repository,
                                   const char *url, const char *download)
{
    if (!directory || !*directory || !repository || !url || !download) return false;
    char metadata[4096];
    if (snprintf(metadata, sizeof(metadata), "%s/%s.sha256", directory, repository) >=
        (int)sizeof(metadata)) return false;
    FILE *file = fopen(metadata, "r");
    if (!file) return false;
    char expected[65] = {0}, filename[256] = {0}, trailing;
    int parsed = fscanf(file, "%64[0-9a-f] %255s %c", expected, filename, &trailing);
    fclose(file);
    const char *basename = strrchr(url, '/');
    if (parsed != 2 || strlen(expected) != 64 || !basename ||
        strcmp(filename, basename + 1) != 0) return false;

    int channel[2];
    if (pipe(channel) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(channel[0]);
        close(channel[1]);
        return false;
    }
    if (pid == 0) {
        close(channel[0]);
        if (dup2(channel[1], STDOUT_FILENO) < 0) _exit(127);
        close(channel[1]);
        execl("/usr/bin/sha256sum", "sha256sum", "--binary", "--", download, (char *)NULL);
        _exit(127);
    }
    close(channel[1]);
    char actual[128] = {0};
    FILE *result = fdopen(channel[0], "r");
    bool read_ok = result && fgets(actual, sizeof(actual), result) != NULL;
    if (result) fclose(result);
    else close(channel[0]);
    int code = 0;
    if (waitpid(pid, &code, 0) != pid) return false;
    return read_ok && WIFEXITED(code) && WEXITSTATUS(code) == 0 &&
           strlen(actual) >= 65 && actual[64] == ' ' &&
           memcmp(expected, actual, 64) == 0;
}

#endif
