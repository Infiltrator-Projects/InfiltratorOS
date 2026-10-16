#ifndef INFILTRATOR_INSTALL_PROCESS_H
#define INFILTRATOR_INSTALL_PROCESS_H

typedef int (*InstallerChildSetup)(void *context);

/* Returns only after exec succeeds or a concrete pre-exec error is received.
 * Success acknowledges process handoff; the worker's status owns completion. */
int installer_spawn_detached(const char *executable, char *const arguments[],
                             InstallerChildSetup setup, void *context);
int installer_redirect_io(const char *log_path);

#endif
