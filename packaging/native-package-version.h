/* Inspect the actual dpkg database, not merely dpkg-query's exit code. */
#ifndef INFILTRATOR_NATIVE_PACKAGE_VERSION_H
#define INFILTRATOR_NATIVE_PACKAGE_VERSION_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool native_package_version_matches(const char *status_path,
                                            const char *package, const char *version)
{
    FILE *file = fopen(status_path, "r");
    if (!file) return false;
    char *line = NULL;
    size_t capacity = 0U;
    bool matching = false, installed = false, exact = false, found = false;
    while (getline(&line, &capacity, file) >= 0) {
        const size_t length = strlen(line);
        while (length > 0U && (line[strlen(line)-1] == '\n' ||
                               line[strlen(line)-1] == '\r'))
            line[strlen(line)-1] = '\0';
        if (line[0] == '\0') {
            if (matching && installed && exact) { found = true; break; }
            matching = installed = exact = false;
        } else if (strncmp(line, "Package: ", 9U) == 0) {
            matching = strcmp(line + 9U, package) == 0;
        } else if (strcmp(line, "Status: install ok installed") == 0) {
            installed = true;
        } else if (strncmp(line, "Version: ", 9U) == 0) {
            exact = strcmp(line + 9U, version) == 0;
        }
    }
    if (!found && matching && installed && exact) found = true;
    if (ferror(file)) found = false;
    free(line);
    if (fclose(file) != 0) found = false;
    return found;
}
#endif
