#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../packaging/verify-native-download.h"

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    if (!verify_native_download(argv[1], "Calculator", argv[2], argv[3])) {
        fprintf(stderr, "Valid native installer SHA-256 rejected\n");
        return 1;
    }
    /* A caller may have both standard descriptors closed; pipe() can then
     * allocate its write end as descriptor 1. Exercise the real checksum child. */
    pid_t child = fork();
    if (child < 0) return 5;
    if (child == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        _exit(verify_native_download(argv[1], "Calculator", argv[2], argv[3]) ? 0 : 1);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Checksum verification failed with closed standard descriptors\n");
        return 6;
    }
    FILE *output = fopen(argv[3], "ab");
    if (!output) return 3;
    if (fputs("tampered\n", output) == EOF || fclose(output) != 0) return 4;
    if (verify_native_download(argv[1], "Calculator", argv[2], argv[3])) {
        fprintf(stderr, "Tampered native installer SHA-256 accepted\n");
        return 1;
    }
    return 0;
}
