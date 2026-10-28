#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RELEASE_API "https://api.github.com/repos/Infiltrator-Projects/Files/releases/latest"
#define RELEASE_PREFIX "https://github.com/Infiltrator-Projects/Files/releases/download/"
#define PATH_LEN 4096
#define JSON_LIMIT (4U * 1024U * 1024U)
#define STATUS_DIR "/Temporary/InfiltratorOS"
#define STATUS_PATH STATUS_DIR "/files-install.status"
#define APP_LOCK_PATH STATUS_DIR "/files.lock"
#define GLOBAL_LOCK_PATH STATUS_DIR "/native-install.lock"

/* Status belongs to this particular OS installation; global locks and build
 * scratch remain shared under /Temporary so concurrent instances serialize. */
static const char *run_status_path(void)
{
    const char *runtime = getenv("INFILTRATOROS_INSTALL_RUNTIME");
    static char scoped[PATH_LEN];
    if (!runtime || !*runtime) return STATUS_PATH;
    if (strncmp(runtime, STATUS_DIR "/native-worker.", sizeof(STATUS_DIR "/native-worker.") - 1) != 0)
        return NULL;
    const char *suffix = runtime + sizeof(STATUS_DIR "/native-worker.") - 1;
    if (!*suffix || strchr(suffix, '/') != NULL) return NULL;
    if (snprintf(scoped, sizeof(scoped), "%s/status/files-install.status", runtime) >= (int)sizeof(scoped))
        return NULL;
    return scoped;
}


static void write_status(const char *state, int progress,
                         const char *stage, const char *detail)
{
    (void)mkdir(STATUS_DIR, 0755);
    char temporary[PATH_LEN];
    const char *destination = run_status_path();
    if (!destination || snprintf(temporary, sizeof(temporary), "%s.%ld.tmp",
                 destination, (long)getpid()) >= (int)sizeof(temporary))
        return;
    FILE *file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "state=%s\nprogress=%d\nstage=%s\ndetail=%s\n",
            state ? state : "running", progress,
            stage ? stage : "", detail ? detail : "");
    if (fclose(file) == 0) {
        (void)chmod(temporary, 0644);
        (void)rename(temporary, destination);
    } else {
        (void)unlink(temporary);
    }
}

static void fail(const char *message)
{
    write_status("failed", 100, "Files installation failed", message);
    fprintf(stderr, "InfiltratorOS Files setup: %s\n", message);
    exit(EXIT_FAILURE);
}

static int run(const char *const argv[], bool quiet)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (quiet) {
            int fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (fd >= 0) {
                (void)dup2(fd, STDOUT_FILENO);
                (void)dup2(fd, STDERR_FILENO);
                if (fd > STDERR_FILENO) close(fd);
            }
        }
        execv(argv[0], (char *const *)argv);
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 125;
}

static void required(const char *const argv[])
{
    int rc = run(argv, false);
    if (rc != 0) {
        char detail[PATH_LEN];
        (void)snprintf(detail, sizeof(detail),
                       "A required command failed (%d): %s", rc, argv[0]);
        write_status("failed", 100, "Files installation failed", detail);
        fprintf(stderr, "InfiltratorOS Files setup: command failed (%d): %s\n",
                rc, argv[0]);
        exit(EXIT_FAILURE);
    }
}

static bool installed(const char *package)
{
    const char *const argv[] = {
        "/usr/bin/dpkg-query", "-W", "-f=${db:Status-Abbrev}", package, NULL
    };
    return run(argv, true) == 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long length = ftell(f);
    if (length < 0 || (unsigned long)length > JSON_LIMIT) { fclose(f); return NULL; }
    rewind(f);
    char *data = malloc((size_t)length + 1U);
    if (!data) { fclose(f); return NULL; }
    size_t got = fread(data, 1, (size_t)length, f);
    if (got != (size_t)length || ferror(f)) { free(data); fclose(f); return NULL; }
    data[got] = '\0';
    fclose(f);
    return data;
}

static bool json_string(const char *start, char *out, size_t out_size,
                        const char **after)
{
    if (!start || *start != '"' || out_size == 0U) return false;
    size_t used = 0U;
    const char *p = start + 1;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\') {
            ch = (unsigned char)*p++;
            if (!ch) return false;
            switch (ch) {
                case '"': case '\\': case '/': break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                default: return false;
            }
        }
        if (ch < 0x20U || used + 1U >= out_size) return false;
        out[used++] = (char)ch;
    }
    if (*p != '"') return false;
    out[used] = '\0';
    if (after) *after = p + 1;
    return true;
}

static bool latest_installer_url(const char *json, char *url, size_t url_size)
{
    const char key[] = "\"browser_download_url\"";
    const char suffix[] = "-native-installer.run";
    const char *p = json;
    while ((p = strstr(p, key)) != NULL) {
        p += sizeof(key) - 1U;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p++ != ':') continue;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        char candidate[PATH_LEN];
        const char *after = NULL;
        if (!json_string(p, candidate, sizeof(candidate), &after)) continue;
        p = after;
        size_t length = strlen(candidate);
        size_t prefix_length = strlen(RELEASE_PREFIX);
        size_t suffix_length = strlen(suffix);
        if (length <= prefix_length + suffix_length ||
            strncmp(candidate, RELEASE_PREFIX, prefix_length) != 0 ||
            strcmp(candidate + length - suffix_length, suffix) != 0)
            continue;
        const char *name = strrchr(candidate, '/');
        if (!name || strncmp(name + 1, "files-", sizeof("files-") - 1U) != 0)
            continue;
        if (strlen(candidate) >= url_size) return false;
        memcpy(url, candidate, length + 1U);
        return true;
    }
    return false;
}

static bool file_contains(const char *path, const char *needle)
{
    char *text = read_file(path);
    if (!text) return false;
    bool found = strstr(text, needle) != NULL;
    free(text);
    return found;
}

int main(void)
{
    if (geteuid() != 0) fail("must run as root");
    (void)mkdir(STATUS_DIR, 0755);

    int app_lock_fd = open(APP_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (app_lock_fd < 0) fail("cannot create Files installation lock");
    /* A second invocation is a separate requested installation. Queue it;
     * never return success leaving that run's status permanently queued. */
    while (flock(app_lock_fd, LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        close(app_lock_fd);
        fail("cannot wait for the previous files installation");
    }

    int global_lock_fd = open(GLOBAL_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (global_lock_fd < 0) fail("cannot create global native installation lock");
    write_status("queued", 2, "Waiting for another native installation",
                 "Files will start as soon as the current Infiltrator native build has finished.");
    while (flock(global_lock_fd, LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        close(global_lock_fd);
        close(app_lock_fd);
        fail("cannot acquire global native installation lock");
    }

    write_status("running", 5, "Waiting for the package manager",
                 "The native build will begin as soon as the package transaction has finished.");
    if (access("/usr/bin/apt-get", X_OK) != 0 ||
        access("/usr/bin/dpkg-query", X_OK) != 0 ||
        access("/usr/bin/wget", X_OK) != 0)
        fail("required Debian/Mint package tools are missing");

    if (setenv("DEBIAN_FRONTEND", "noninteractive", 1) != 0)
        fail("cannot set noninteractive package environment");

    /* aptk/DPKG may hold the frontend lock while the parent OS transaction
     * finishes. Waiting without a short arbitrary timeout is required here;
     * the installation monitor separately tracks native job lifetime. */
    const char *const wait_argv[] = {
        "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check", NULL
    };
    required(wait_argv);

    write_status("running", 12, "Checking the latest Files release",
                 "InfiltratorOS is selecting the newest published native installer.");

    char work[] = STATUS_DIR "/files-XXXXXX";
    if (!mkdtemp(work)) fail("cannot create temporary working directory");

    char json_path[PATH_LEN];
    char installer_path[PATH_LEN];
    if (snprintf(json_path, sizeof(json_path), "%s/release.json", work) >= (int)sizeof(json_path) ||
        snprintf(installer_path, sizeof(installer_path), "%s/files.run", work) >= (int)sizeof(installer_path))
        fail("temporary path is too long");

    const char *release_dir = getenv("INFILTRATOROS_RELEASE_DIR");
    if (release_dir && *release_dir) {
        char source[PATH_LEN];
        if (snprintf(source, sizeof(source), "%s/Files.json", release_dir) >=
            (int)sizeof(source)) fail("signed native release metadata path too long");
        const char *const copy_argv[] = {
            "/usr/bin/cp", "--", source, json_path, NULL
        };
        required(copy_argv);
    } else {
        /* Standalone installer invocation retains its own release discovery.
         * Managed InfiltratorOS installs always use the signed snapshot. */
        const char *const api_argv[] = {
            "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
            "--output-document", json_path, RELEASE_API, NULL
        };
        required(api_argv);
    }

    char *json = read_file(json_path);
    if (!json) fail("cannot read latest Files release metadata");
    char url[PATH_LEN];
    bool found = latest_installer_url(json, url, sizeof(url));
    free(json);
    if (!found) fail("latest Files release has no trusted native installer asset");

    write_status("running", 22, "Downloading Files",
                 "Downloading the latest native installer from Infiltrator Projects.");
    printf("Downloading latest Files native installer:\n  %s\n", url);
    const char *const download_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", installer_path, url, NULL
    };
    required(download_argv);
    if (chmod(installer_path, 0700) != 0)
        fail("cannot mark native installer executable");

    write_status("running", 35, "Compiling and optimising Files",
                 "Building for this CPU with aggressive native optimisation, LTO and two-pass PGO.");
    const char *const install_argv[] = {
        installer_path, "--system-package-mode", NULL
    };
    required(install_argv);

    write_status("running", 94, "Verifying the native Files installation",
                 "Checking /Programs, build provenance and desktop integration without changing the default file manager.");

    if (!installed("files") || access("/Programs/files", X_OK) != 0)
        fail("native Files package or /Programs executable is not installed after the build");
    if (access("/usr/bin/files", F_OK) == 0)
        fail("obsolete /usr/bin/files path remains after native Files installation");

    const char info[] = "/System/Resources/Documentation/Files/BUILD-INFO";
    if (!file_contains(info, "Profile: aggressive") ||
        !file_contains(info, "PGO: trained two-pass profile") ||
        !file_contains(info, "CXX flags: -O3") ||
        !file_contains(info, "-march=native"))
        fail("installed Files is not an aggressive two-pass native build");

    const char desktop[] = "/usr/share/applications/files.desktop";
    if (!file_contains(desktop, "Exec=/Programs/files"))
        fail("Files desktop launcher does not target the canonical /Programs executable");

    unlink(installer_path);
    unlink(json_path);
    rmdir(work);
    write_status("complete", 100, "Files native installation complete",
                 "Files is installed and CPU-optimised; the existing default file manager is unchanged.");
    puts("InfiltratorOS Files installation completed successfully.");
    close(global_lock_fd);
    close(app_lock_fd);
    return EXIT_SUCCESS;
}
