#include "installer-status.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool installer_status_read(const char *path, InstallerStatus *output)
{
    if (!path || !output) return false;
    FILE *file = fopen(path, "r");
    if (!file) return false;
    InstallerStatus candidate = { .progress = -1 };
    unsigned int seen = 0U;
    bool valid = true;
    char line[1400];
    while (fgets(line, sizeof(line), file)) {
        char *newline = strchr(line, '\n');
        if (!newline) { valid = false; break; }
        *newline = '\0';
        if (newline != line && newline[-1] == '\r') newline[-1] = '\0';
        char *equals = strchr(line, '=');
        if (!equals) {
            if (*line) valid = false;
            continue;
        }
        *equals++ = '\0';
        unsigned int field = 0U;
        char *destination = NULL;
        size_t capacity = 0U;
        if (strcmp(line, "state") == 0) {
            field = 1U; destination = candidate.state; capacity = sizeof(candidate.state);
        } else if (strcmp(line, "progress") == 0) {
            field = 2U;
            errno = 0;
            char *end = NULL;
            long value = strtol(equals, &end, 10);
            if (errno || end == equals || *end || value < -1L || value > 100L)
                valid = false;
            else
                candidate.progress = (int)value;
        } else if (strcmp(line, "stage") == 0) {
            field = 4U; destination = candidate.stage; capacity = sizeof(candidate.stage);
        } else if (strcmp(line, "detail") == 0) {
            field = 8U; destination = candidate.detail; capacity = sizeof(candidate.detail);
        }
        if (field && (seen & field)) valid = false;
        seen |= field;
        if (destination) {
            size_t length = strlen(equals);
            if (length >= capacity) valid = false;
            else memcpy(destination, equals, length + 1U);
        }
    }
    if (ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;
    if (!valid || seen != 15U || !candidate.state[0]) return false;
    *output = candidate;
    return true;
}
