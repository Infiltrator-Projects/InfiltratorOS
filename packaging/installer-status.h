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
bool installer_status_read(const char *path, InstallerStatus *output);
#endif
