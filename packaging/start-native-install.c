// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "installer-profile.h"
#include "installer-process.h"

#define ENV_LIMIT 65536U
#define TEXT_LIMIT 4096U
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
static char provider_slug[128];
#endif
static char run_status[TEXT_LIMIT];
static char run_log[TEXT_LIMIT];
static const char *status_file(void) { return run_status; }
static const char *log_file(void) { return run_log; }

typedef struct {
    uid_t uid;
    gid_t gid;
    char user[128];
    char home[TEXT_LIMIT];
    char display[256];
    char wayland[256];
    char runtime[TEXT_LIMIT];
    char bus[TEXT_LIMIT];
    char xauthority[TEXT_LIMIT];
    bool found;
    int rank;
} DesktopSession;

static void die(const char *message)
{
    fprintf(stderr, "InfiltratorOS installer launcher: %s\n", message);
    exit(EXIT_FAILURE);
}

static void ensure_status_dir(void)
{
    if (mkdir(STATUS_DIR, 0755) != 0 && errno != EEXIST)
        die("cannot create runtime status directory");
}

static void write_initial_status(void)
{
    FILE *file = fopen(status_file(), "w");
    if (!file) die("cannot create installation status");
    fputs("state=queued\nprogress=1\nstage=Preparing " INSTALL_NAME " optimisation\n"
          "detail=The installation will continue after the package manager releases its lock.\n",
          file);
    if (fclose(file) != 0) die("cannot write installation status");
    (void)chmod(status_file(), 0644);
}

static bool numeric_name(const char *text)
{
    if (!text || !*text) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (!isdigit(*p)) return false;
    return true;
}

static bool env_value(const char *data, size_t length, const char *key,
                      char *destination, size_t destination_size)
{
    const size_t key_length = strlen(key);
    for (size_t offset = 0U; offset < length;) {
        const char *entry = data + offset;
        const size_t remaining = length - offset;
        const size_t entry_length = strnlen(entry, remaining);
        if (entry_length == remaining) break;
        if (entry_length > key_length &&
            memcmp(entry, key, key_length) == 0 &&
            entry[key_length] == '=') {
            const char *value = entry + key_length + 1U;
            const size_t value_length = entry_length - key_length - 1U;
            if (value_length + 1U > destination_size) return false;
            memcpy(destination, value, value_length);
            destination[value_length] = '\0';
            return true;
        }
        offset += entry_length + 1U;
    }
    return false;
}

static bool session_from_process(const char *pid_name, DesktopSession *session)
{
    char path[TEXT_LIMIT];
    if (snprintf(path, sizeof(path), "/proc/%s", pid_name) >= (int)sizeof(path))
        return false;
    struct stat st;
    if (stat(path, &st) != 0 || st.st_uid < 1000U || st.st_uid == 65534U)
        return false;

    if (snprintf(path, sizeof(path), "/proc/%s/environ", pid_name) >= (int)sizeof(path))
        return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char environment[ENV_LIMIT];
    ssize_t got = read(fd, environment, sizeof(environment));
    close(fd);
    if (got <= 0) return false;

    DesktopSession candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.uid = st.st_uid;
    struct passwd *pw = getpwuid(candidate.uid);
    if (!pw || !pw->pw_name || !pw->pw_dir) return false;
    candidate.gid = pw->pw_gid;
    snprintf(candidate.user, sizeof(candidate.user), "%s", pw->pw_name);
    snprintf(candidate.home, sizeof(candidate.home), "%s", pw->pw_dir);

    (void)env_value(environment, (size_t)got, "DISPLAY",
                    candidate.display, sizeof(candidate.display));
    (void)env_value(environment, (size_t)got, "WAYLAND_DISPLAY",
                    candidate.wayland, sizeof(candidate.wayland));
    (void)env_value(environment, (size_t)got, "XDG_RUNTIME_DIR",
                    candidate.runtime, sizeof(candidate.runtime));
    (void)env_value(environment, (size_t)got, "DBUS_SESSION_BUS_ADDRESS",
                    candidate.bus, sizeof(candidate.bus));
    (void)env_value(environment, (size_t)got, "XAUTHORITY",
                    candidate.xauthority, sizeof(candidate.xauthority));

    if (!candidate.display[0] && !candidate.wayland[0]) return false;
    if (!candidate.runtime[0])
        snprintf(candidate.runtime, sizeof(candidate.runtime), "/run/user/%lu",
                 (unsigned long)candidate.uid);
    if (!candidate.bus[0]) {
        static const char prefix[] = "unix:path=";
        static const char suffix[] = "/bus";
        const size_t runtime_length = strlen(candidate.runtime);
        const size_t total = (sizeof(prefix) - 1U) + runtime_length +
                             (sizeof(suffix) - 1U) + 1U;
        if (total > sizeof(candidate.bus)) return false;
        char *cursor = candidate.bus;
        memcpy(cursor, prefix, sizeof(prefix) - 1U);
        cursor += sizeof(prefix) - 1U;
        memcpy(cursor, candidate.runtime, runtime_length);
        cursor += runtime_length;
        memcpy(cursor, suffix, sizeof(suffix));
    }
    // Prefer an active graphical logind session, followed by the invoking
    // desktop user and known session leaders. Fall back to graphical processes
    // for systems without logind. Never take the first DISPLAY blindly.
    candidate.rank = 1;
    const char *caller = getenv("SUDO_UID");
    if (caller && numeric_name(caller) && strtoul(caller, NULL, 10) == candidate.uid)
        candidate.rank += 20;
    char comm[256] = {0};
    if (snprintf(path, sizeof(path), "/proc/%s/comm", pid_name) < (int)sizeof(path)) {
        FILE *process = fopen(path, "r");
        if (process) {
            if (fgets(comm, sizeof(comm), process) &&
                (strstr(comm, "cinnamon-sess") || strstr(comm, "gnome-session") ||
                 strstr(comm, "xfce4-session") || strstr(comm, "mate-session") ||
                 strstr(comm, "startplasma") || strstr(comm, "ksmserver")))
                candidate.rank += 10;
            (void)fclose(process);
        }
    }
    if (snprintf(path, sizeof(path), "/proc/%s/cgroup", pid_name) < (int)sizeof(path)) {
        FILE *groups = fopen(path, "r");
        if (groups) {
            char line[TEXT_LIMIT];
            while (fgets(line, sizeof(line), groups)) {
                const char *scope = strstr(line, "/session-");
                if (!scope) continue;
                scope += strlen("/session-");
                const char *end = strstr(scope, ".scope");
                if (!end || end == scope || (size_t)(end - scope) >= 64U) continue;
                char id[64];
                memcpy(id, scope, (size_t)(end - scope));
                id[end - scope] = '\0';
                if (strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(id))
                    continue;
                char session_path[256];
                snprintf(session_path, sizeof(session_path), "/run/systemd/sessions/%s", id);
                int session_fd = open(session_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
                struct stat info;
                if (session_fd < 0) continue;
                if (fstat(session_fd, &info) != 0 || info.st_uid != 0 ||
                    !S_ISREG(info.st_mode) || (info.st_mode & 022)) {
                    close(session_fd);
                    continue;
                }
                FILE *record = fdopen(session_fd, "r");
                if (!record) { close(session_fd); continue; }
                bool active = false, graphical = false, owner = false;
                char expected[64];
                snprintf(expected, sizeof(expected), "UID=%lu\n", (unsigned long)candidate.uid);
                while (fgets(line, sizeof(line), record)) {
                    if (strcmp(line, "ACTIVE=1\n") == 0 || strcmp(line, "STATE=active\n") == 0) active = true;
                    if (strcmp(line, "TYPE=x11\n") == 0 || strcmp(line, "TYPE=wayland\n") == 0) graphical = true;
                    if (strcmp(line, expected) == 0) owner = true;
                }
                (void)fclose(record);
                if (active && graphical && owner) candidate.rank += 100;
                break;
            }
            (void)fclose(groups);
        }
    }
    candidate.found = true;
    *session = candidate;
    return true;
}

static DesktopSession find_desktop_session(void)
{
    DesktopSession result;
    memset(&result, 0, sizeof(result));
    DIR *proc = opendir("/proc");
    if (!proc) return result;
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!numeric_name(entry->d_name)) continue;
        DesktopSession candidate;
        if (session_from_process(entry->d_name, &candidate) &&
            (!result.found || candidate.rank > result.rank))
            result = candidate;
    }
    closedir(proc);
    return result;
}

static int prepare_worker(void *context)
{
    (void)context;
    if (setenv("TMPDIR", STATUS_DIR, 1) != 0) return -1;
    return installer_redirect_io(log_file());
}

static int prepare_progress(void *context)
{
    const DesktopSession *session = context;
    if (setgid(session->gid) != 0 ||
        initgroups(session->user, session->gid) != 0 ||
        setuid(session->uid) != 0)
        return -1;
    if (setenv("HOME", session->home, 1) != 0 ||
        setenv("USER", session->user, 1) != 0 ||
        setenv("LOGNAME", session->user, 1) != 0 ||
        setenv("XDG_RUNTIME_DIR", session->runtime, 1) != 0 ||
        setenv("DBUS_SESSION_BUS_ADDRESS", session->bus, 1) != 0)
        return -1;
    // Clear any display credentials inherited from a different caller.
    if (session->display[0] ? setenv("DISPLAY", session->display, 1) : unsetenv("DISPLAY")) return -1;
    if (session->wayland[0] ? setenv("WAYLAND_DISPLAY", session->wayland, 1) : unsetenv("WAYLAND_DISPLAY")) return -1;
    if (session->xauthority[0] ? setenv("XAUTHORITY", session->xauthority, 1) : unsetenv("XAUTHORITY")) return -1;
    return installer_redirect_io("/dev/null");
}

static char runtime_worker[TEXT_LIMIT];
static char runtime_progress[TEXT_LIMIT];

static void resolve_temporary_runtime(void)
{
    const char *root = getenv("INFILTRATOROS_INSTALL_RUNTIME");
    static const char prefix[] = "/Temporary/InfiltratorOS/native-worker.";
    if (!root || strncmp(root, prefix, sizeof(prefix) - 1U) != 0 ||
        strchr(root + sizeof(prefix) - 1U, '/') != NULL ||
        !root[sizeof(prefix) - 1U])
        die("the temporary installation runtime is missing");
    if (snprintf(runtime_worker, sizeof(runtime_worker), "%s/install-%s", root,
                 INSTALL_SLUG) >= (int)sizeof(runtime_worker) ||
        snprintf(runtime_progress, sizeof(runtime_progress), "%s/%s-progress",
                 root, INSTALL_SLUG) >= (int)sizeof(runtime_progress))
        die("temporary installation runtime path is too long");
}

static int spawn_progress_ui(DesktopSession *session)
{
    if (!session->found) return 1;
    const char *progress_mode = getenv("INFILTRATOROS_PROGRESS_MODE");
    if (progress_mode != NULL && strcmp(progress_mode, "none") == 0) return 1;
    if (progress_mode != NULL && strcmp(progress_mode, "aggregate") == 0) {
#ifdef INSTALL_SYSTEM_MONITOR
        char directory[TEXT_LIMIT];
        const char *runtime = getenv("INFILTRATOROS_INSTALL_RUNTIME");
        if (runtime == NULL ||
            snprintf(directory, sizeof(directory), "%s/status", runtime) >= (int)sizeof(directory)) {
            fprintf(stderr, "InfiltratorOS aggregate status path is invalid\n");
            return -1;
        }
        const char *early = getenv("INFILTRATOROS_EARLY_PROGRESS");
        bool wait_for_draw = early && strcmp(early, "1") == 0;
        char ready[TEXT_LIMIT];
        if (wait_for_draw) {
            if (snprintf(ready, sizeof(ready), "%s/gui-ready", runtime) >= (int)sizeof(ready))
                return -1;
            // Only this unprivileged desktop user may write the acknowledgement.
            // The parent directory remains root-owned; the token controls no
            // installation decision other than presentation timing.
            int fd = open(ready, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
            if (fd < 0) return -1;
            int owned = fchown(fd, session->uid, session->gid);
            (void)close(fd);
            if (owned != 0) return -1;
        }
        char *const arguments[] = {runtime_progress, "--aggregate", directory, NULL};
        if (installer_spawn_detached(runtime_progress, arguments, prepare_progress, session) != 0) {
            fprintf(stderr, "InfiltratorOS aggregate progress window: %s\n", strerror(errno));
            return -1;
        }
        if (wait_for_draw) {
            const struct timespec interval = {0, 100000000L};
            for (int i = 0; i < 150; ++i) {
                struct stat state;
                if (lstat(ready, &state) == 0 && S_ISREG(state.st_mode) && state.st_size > 0)
                    return 0;
                (void)nanosleep(&interval, NULL);
            }
            fprintf(stderr, "InfiltratorOS graphical window has not acknowledged its first frame; continuing installation.\n");
            // Cancel a late preinst window before postinst tries again.
            char cancel[TEXT_LIMIT];
            if (snprintf(cancel, sizeof(cancel), "%s/early-ui-cancelled", runtime) < (int)sizeof(cancel)) {
                int fd = open(cancel, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
                if (fd >= 0) (void)close(fd);
            }
            return -1;
        }
#endif
        return 0;
    }
    #if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    char *const arguments[] = { runtime_progress, run_status, provider_slug, NULL };
#else
    char *const arguments[] = { runtime_progress, (char *)status_file(), NULL };
#endif
    if (installer_spawn_detached(runtime_progress, arguments, prepare_progress, session) != 0) {
        fprintf(stderr, INSTALL_NAME " progress window: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void report_start_failure(int error)
{
    FILE *file = fopen(status_file(), "w");
    if (file) {
        fprintf(file, "state=failed\nprogress=0\nstage=" INSTALL_NAME
                      " installer could not start\ndetail=%s\n", strerror(error));
        (void)fclose(file);
    }
    fprintf(stderr, INSTALL_NAME " installer could not start: %s\n", strerror(error));
}

#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
/* Validate signed provider types, including the independent OFS installer. */
static bool valid_provider_slug(const char *slug)
{
    if (!slug || !*slug || strlen(slug) >= sizeof(provider_slug)) return false;
    if (strncmp(slug, "calendar-", 9) == 0) return slug[9] != '\0';
    if (strncmp(slug, "clock-", 6) == 0) return slug[6] != '\0';
    return strcmp(slug, "filesystem-fat12") == 0 ||
           strcmp(slug, "filesystem-fat16") == 0 ||
           strcmp(slug, "filesystem-fat32") == 0 ||
           strcmp(slug, "filesystem-ofs") == 0 ||
           strcmp(slug, "filesystem-exfat") == 0 ||
           strcmp(slug, "filesystem-ntfs") == 0 ||
           strcmp(slug, "filesystem-ext2") == 0 ||
           strcmp(slug, "filesystem-ext3") == 0 ||
           strcmp(slug, "filesystem-ext4") == 0 ||
           strcmp(slug, "filesystem-xfs") == 0 ||
           strcmp(slug, "filesystem-btrfs") == 0 ||
           strcmp(slug, "filesystem-ffs") == 0 ||
           strcmp(slug, "filesystem-sfs") == 0 ||
           strcmp(slug, "filesystem-pfs3") == 0 ||
           strcmp(slug, "filesystem-hfs") == 0 ||
           strcmp(slug, "filesystem-hfsplus") == 0 ||
           strcmp(slug, "filesystem-minix") == 0 ||
           strcmp(slug, "filesystem-ufs2") == 0 ||
           strcmp(slug, "filesystem-zfs") == 0 ||
           strcmp(slug, "filesystem-apfs") == 0 ||
           strcmp(slug, "filesystem-swap") == 0 ||
           strcmp(slug, "filesystem-sfs2") == 0 ||
           strcmp(slug, "filesystem-ufs1") == 0;
}
#endif

int main(int argc, char **argv)
{
    if (geteuid() != 0) die("must run as root");
#ifdef INSTALL_SYSTEM_MONITOR
    if (argc == 2 && strcmp(argv[1], "--show-progress") == 0) {
        resolve_temporary_runtime();
        DesktopSession session = find_desktop_session();
        const int result = spawn_progress_ui(&session);
        return result == 0 ? EXIT_SUCCESS : result > 0 ? 2 : EXIT_FAILURE;
    }
#endif
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    if (argc != 2 || !valid_provider_slug(argv[1]))
        die("expected one calendar, clock, or filesystem provider slug");
    for (const char *p = argv[1]; *p; ++p)
        if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '-'))
            die("unsafe temporal provider slug");
    snprintf(provider_slug, sizeof(provider_slug), "%s", argv[1]);
#else
    (void)argv;
    if (argc != 1) die("unexpected installer arguments");
#endif
    resolve_temporary_runtime();
    ensure_status_dir();
    char directory[TEXT_LIMIT];
    const char *runtime = getenv("INFILTRATOROS_INSTALL_RUNTIME");
    if (snprintf(directory, sizeof(directory), "%s/status", runtime) >= (int)sizeof(directory))
        die("status directory path is too long");
    if (mkdir(directory, 0755) != 0 && errno != EEXIST)
        die("cannot create per-run status directory");
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    const char *slug = provider_slug;
#else
    const char *slug = INSTALL_SLUG;
#endif
    if (snprintf(run_status, sizeof(run_status), "%s/%s-install.status", directory,
                 slug) >= (int)sizeof(run_status) ||
        snprintf(run_log, sizeof(run_log), "%s/%s-install.log", directory,
                 slug) >= (int)sizeof(run_log))
        die("run status or log path is too long");
    write_initial_status();
    DesktopSession session = find_desktop_session();
    /* Bound every native worker before the run monitor's 24-hour timeout.
     * GNU timeout signals the complete installer process group and escalates
     * to SIGKILL, preventing new batches from racing old unbounded workers. */
    char *const bounded[] = {
        "/usr/bin/timeout", "--signal=TERM", "--kill-after=30s", "86300s",
        runtime_worker,
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
        provider_slug,
#endif
        NULL
    };
    if (installer_spawn_detached(bounded[0], bounded, prepare_worker, NULL) != 0) {
        report_start_failure(errno);
        spawn_progress_ui(&session);
        return EXIT_FAILURE;
    }
    spawn_progress_ui(&session);
    puts(INSTALL_NAME " native optimisation started in the background.");
    return EXIT_SUCCESS;
}
