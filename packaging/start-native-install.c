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
        if (session_from_process(entry->d_name, &result)) break;
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
    if (session->display[0] && setenv("DISPLAY", session->display, 1) != 0) return -1;
    if (session->wayland[0] && setenv("WAYLAND_DISPLAY", session->wayland, 1) != 0) return -1;
    if (session->xauthority[0] && setenv("XAUTHORITY", session->xauthority, 1) != 0) return -1;
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

static void spawn_progress_ui(DesktopSession *session)
{
    if (!session->found) return;
    #if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    char *const arguments[] = { runtime_progress, run_status, provider_slug, NULL };
#else
    char *const arguments[] = { runtime_progress, (char *)status_file(), NULL };
#endif
    if (installer_spawn_detached(runtime_progress, arguments, prepare_progress, session) != 0)
        fprintf(stderr, INSTALL_NAME " progress window: %s\n", strerror(errno));
}

static void report_start_failure(int error)
{
    FILE *file = fopen(STATUS_PATH, "w");
    if (file) {
        fprintf(file, "state=failed\nprogress=0\nstage=" INSTALL_NAME
                      " installer could not start\ndetail=%s\n", strerror(error));
        (void)fclose(file);
    }
    fprintf(stderr, INSTALL_NAME " installer could not start: %s\n", strerror(error));
}

int main(int argc, char **argv)
{
    if (geteuid() != 0) die("must run as root");
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    if (argc != 2 || strlen(argv[1]) >= sizeof(provider_slug) ||
        (strncmp(argv[1], "calendar-", 9) != 0 &&
         strncmp(argv[1], "clock-", 6) != 0 &&
         strncmp(argv[1], "filesystem-fat", 14) != 0))
        die("expected one calendar or clock provider slug");
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
#if defined(INSTALL_TEMPORAL_RESOURCES) || defined(INSTALL_FILESYSTEM_RESOURCES)
    char *const arguments[] = { runtime_worker, provider_slug, NULL };
#else
    char *const arguments[] = { runtime_worker, NULL };
#endif
    if (installer_spawn_detached(runtime_worker, arguments, prepare_worker, NULL) != 0) {
        report_start_failure(errno);
        spawn_progress_ui(&session);
        return EXIT_FAILURE;
    }
    spawn_progress_ui(&session);
    puts(INSTALL_NAME " native optimisation started in the background.");
    return EXIT_SUCCESS;
}
