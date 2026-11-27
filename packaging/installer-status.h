// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_INSTALL_STATUS_H
#define INFILTRATOR_INSTALL_STATUS_H
#include <stdbool.h>

typedef struct {
    char state[32];
    int progress;
    char stage[256];
    char detail[1024];
} InstallerStatus;

/* A partial, malformed or truncated record leaves output unchanged. */
#ifdef __cplusplus
extern "C" {
#endif
bool installer_status_read(const char *path, InstallerStatus *output);
#ifdef __cplusplus
}
#endif
#endif
