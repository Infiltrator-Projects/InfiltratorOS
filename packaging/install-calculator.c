#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
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

#define RELEASE_API "https://api.github.com/repos/Infiltrator-Projects/Calculator/releases/latest"
#define RELEASE_PREFIX "https://github.com/Infiltrator-Projects/Calculator/releases/download/"
#define PATH_LEN 4096
#define JSON_LIMIT (4U * 1024U * 1024U)
#define STATUS_DIR "/run/infiltratoros"
#define STATUS_PATH STATUS_DIR "/calculator-install.status"
#define APP_LOCK_PATH "/run/lock/infiltratoros-calculator.lock"
#define GLOBAL_LOCK_PATH "/run/lock/infiltratoros-native-install.lock"

static void write_status(const char *state, int progress,
                         const char *stage, const char *detail)
{
    (void)mkdir(STATUS_DIR, 0755);
    char temporary[PATH_LEN];
    if (snprintf(temporary, sizeof(temporary), STATUS_PATH ".%ld.tmp",
                 (long)getpid()) >= (int)sizeof(temporary))
        return;
    FILE *file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "state=%s\nprogress=%d\nstage=%s\ndetail=%s\n",
            state ? state : "running", progress,
            stage ? stage : "", detail ? detail : "");
    if (fclose(file) == 0) {
        (void)chmod(temporary, 0644);
        (void)rename(temporary, STATUS_PATH);
    } else {
        (void)unlink(temporary);
    }
}

static void fail(const char *message)
{
    write_status("failed", 100, "Calculator installation failed", message);
    fprintf(stderr, "InfiltratorOS Calculator setup: %s\n", message);
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
        write_status("failed", 100, "Calculator installation failed", detail);
        fprintf(stderr, "InfiltratorOS Calculator setup: command failed (%d): %s\n",
                rc, argv[0]);
        exit(EXIT_FAILURE);
    }
}

static bool installed(const char *package)
{
    FILE *file = fopen("/var/lib/dpkg/status", "r");
    if (!file) return false;

    char *line = NULL;
    size_t capacity = 0U;
    bool matching_package = false;
    bool installed_status = false;
    bool result = false;

    while (getline(&line, &capacity, file) >= 0) {
        size_t length = strlen(line);
        while (length > 0U &&
               (line[length - 1U] == '\n' || line[length - 1U] == '\r'))
            line[--length] = '\0';

        if (length == 0U) {
            if (matching_package && installed_status) {
                result = true;
                break;
            }
            matching_package = false;
            installed_status = false;
            continue;
        }

        if (strncmp(line, "Package: ", 9U) == 0)
            matching_package = strcmp(line + 9U, package) == 0;
        else if (strcmp(line, "Status: install ok installed") == 0)
            installed_status = true;
    }

    if (!result && matching_package && installed_status) result = true;
    free(line);
    (void)fclose(file);
    return result;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || (unsigned long)length > JSON_LIMIT) {
        (void)fclose(file);
        return NULL;
    }
    rewind(file);
    char *data = malloc((size_t)length + 1U);
    if (!data) {
        (void)fclose(file);
        return NULL;
    }
    size_t got = fread(data, 1U, (size_t)length, file);
    if (got != (size_t)length || ferror(file)) {
        free(data);
        (void)fclose(file);
        return NULL;
    }
    data[got] = '\0';
    (void)fclose(file);
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

static bool valid_version(const char *version)
{
    if (!version || !version[0]) return false;
    for (const unsigned char *p = (const unsigned char *)version; *p; ++p) {
        if (isalnum(*p) || *p == '.' || *p == '+' || *p == '~' || *p == '-')
            continue;
        return false;
    }
    return true;
}

static bool latest_installer_url(const char *json, char *url, size_t url_size,
                                 char *version, size_t version_size)
{
    const char key[] = "\"browser_download_url\"";
    const char file_prefix[] = "calculator-";
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

        const size_t length = strlen(candidate);
        const size_t release_prefix_length = strlen(RELEASE_PREFIX);
        const size_t suffix_length = strlen(suffix);
        if (length <= release_prefix_length + suffix_length ||
            strncmp(candidate, RELEASE_PREFIX, release_prefix_length) != 0 ||
            strcmp(candidate + length - suffix_length, suffix) != 0)
            continue;

        const char *name = strrchr(candidate, '/');
        if (!name) continue;
        ++name;
        const size_t name_length = strlen(name);
        const size_t file_prefix_length = strlen(file_prefix);
        if (name_length <= file_prefix_length + suffix_length ||
            strncmp(name, file_prefix, file_prefix_length) != 0)
            continue;

        const size_t version_length =
            name_length - file_prefix_length - suffix_length;
        if (version_length + 1U > version_size || length + 1U > url_size)
            return false;
        memcpy(version, name + file_prefix_length, version_length);
        version[version_length] = '\0';
        if (!valid_version(version)) return false;

        char release_segment[256];
        if (snprintf(release_segment, sizeof(release_segment),
                     "/download/v%s/", version) >= (int)sizeof(release_segment))
            return false;
        if (strstr(candidate, release_segment) == NULL) continue;

        memcpy(url, candidate, length + 1U);
        return true;
    }
    return false;
}

static bool regular_file(const char *path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static bool executable_file(const char *path)
{
    return regular_file(path) && access(path, X_OK) == 0;
}

static bool file_has_exact_line(const char *path, const char *expected)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;

    char *line = NULL;
    size_t capacity = 0U;
    bool found = false;
    while (getline(&line, &capacity, file) >= 0) {
        size_t length = strlen(line);
        while (length > 0U &&
               (line[length - 1U] == '\n' || line[length - 1U] == '\r'))
            line[--length] = '\0';
        if (strcmp(line, expected) == 0) {
            found = true;
            break;
        }
    }
    free(line);
    (void)fclose(file);
    return found;
}

static const char *installed_contract_error(void)
{
    const char desktop[] = "/usr/share/applications/net.ssmith.calculator.desktop";

    if (!installed("calculator"))
        return "the canonical Calculator package is not installed";
    if (!executable_file("/Programs/calculator"))
        return "the Calculator package is installed but /Programs/calculator is missing or not executable";
    if (!regular_file("/usr/share/doc/calculator/BUILD-INFO"))
        return "Calculator native build metadata is missing after installation";
    if (!regular_file(desktop))
        return "Calculator desktop launcher is missing after installation";
    if (!file_has_exact_line(desktop, "Type=Application") ||
        !file_has_exact_line(desktop, "Name=Calculator") ||
        !file_has_exact_line(desktop, "Exec=/Programs/calculator") ||
        !file_has_exact_line(desktop, "Icon=calculator") ||
        file_has_exact_line(desktop, "Hidden=true") ||
        file_has_exact_line(desktop, "NoDisplay=true"))
        return "Calculator desktop launcher does not match the installed native application contract";
    if (!regular_file("/usr/share/icons/hicolor/scalable/apps/calculator.svg"))
        return "Calculator desktop icon is missing after installation";
    if (!regular_file("/usr/share/app-install/icons/calculator.svg"))
        return "Calculator Software Manager icon is missing after installation";
    if (!regular_file("/usr/share/metainfo/calculator.metainfo.xml"))
        return "Calculator AppStream metadata is missing after installation";
    if (!regular_file("/etc/profile.d/91-infiltrator-calculator-path.sh"))
        return "Calculator native PATH bridge is missing after installation";
    if (!regular_file("/usr/share/fonts/truetype/calculator/mb_corpo_a_cond_regular.ttf") ||
        !regular_file("/usr/share/fonts/truetype/calculator/mb_corpo_s_bold.ttf") ||
        !regular_file("/usr/share/fonts/truetype/calculator/mb_corpo_s_regular.ttf"))
        return "Calculator MB Corpo font payload is incomplete after installation";
    return NULL;
}

static void purge_package(const char *package, const char *description)
{
    if (!installed(package)) return;
    printf("Removing %s package: %s\n", description, package);
    const char *const argv[] = {
        "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300",
        "purge", "-y", package, NULL
    };
    required(argv);
}

static void purge_replaced_calculators(void)
{
    static const char *obsolete_packages[] = {
        "infiltrator-calculator",
        "infiltrator-calc"
    };
    static const char *distribution_packages[] = {
        "gnome-calculator",
        "mate-calc",
        "kcalc",
        "galculator",
        "deepin-calculator",
        "ukui-calculator"
    };

    for (size_t i = 0U;
         i < sizeof(obsolete_packages) / sizeof(obsolete_packages[0]); ++i)
        purge_package(obsolete_packages[i], "obsolete Calculator identity");

    for (size_t i = 0U;
         i < sizeof(distribution_packages) / sizeof(distribution_packages[0]); ++i)
        purge_package(distribution_packages[i], "distribution Calculator");
}

static void cleanup_work(const char *work)
{
    if (!work || !work[0]) return;
    const char *const argv[] = { "/usr/bin/rm", "-rf", "--", work, NULL };
    (void)run(argv, true);
}

int main(void)
{
    if (geteuid() != 0) fail("must run as root");
    (void)mkdir(STATUS_DIR, 0755);

    int app_lock_fd = open(APP_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (app_lock_fd < 0) fail("cannot create Calculator installation lock");
    if (flock(app_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        puts("A Calculator native installation is already running.");
        close(app_lock_fd);
        return EXIT_SUCCESS;
    }

    int global_lock_fd = open(GLOBAL_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (global_lock_fd < 0) fail("cannot create global native installation lock");
    write_status("queued", 2, "Waiting for another native installation",
                 "Calculator will start as soon as the current Infiltrator native build has finished.");
    while (flock(global_lock_fd, LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        close(global_lock_fd);
        close(app_lock_fd);
        fail("cannot acquire global native installation lock");
    }

    write_status("running", 5, "Waiting for the package manager",
                 "The Calculator native build will begin as soon as the package transaction has finished.");
    if (access("/usr/bin/apt-get", X_OK) != 0 ||
        access("/usr/bin/wget", X_OK) != 0 ||
        access("/usr/bin/rm", X_OK) != 0)
        fail("required Debian/Mint installation tools are missing");

    if (setenv("DEBIAN_FRONTEND", "noninteractive", 1) != 0)
        fail("cannot set noninteractive package environment");

    const char *const wait_argv[] = {
        "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "check", NULL
    };
    required(wait_argv);

    write_status("running", 12, "Checking the latest Calculator release",
                 "InfiltratorOS is selecting Calculator's newest published native installer.");

    char work[] = "/tmp/calculator-XXXXXX";
    if (!mkdtemp(work)) fail("cannot create temporary Calculator working directory");

    char json_path[PATH_LEN];
    char installer_path[PATH_LEN];
    if (snprintf(json_path, sizeof(json_path), "%s/release.json", work) >=
            (int)sizeof(json_path) ||
        snprintf(installer_path, sizeof(installer_path), "%s/calculator.run", work) >=
            (int)sizeof(installer_path)) {
        cleanup_work(work);
        fail("temporary Calculator path is too long");
    }

    const char *const api_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", json_path, RELEASE_API, NULL
    };
    required(api_argv);

    char *json = read_file(json_path);
    if (!json) {
        cleanup_work(work);
        fail("cannot read latest Calculator release metadata");
    }

    char url[PATH_LEN];
    char version[128];
    const bool found = latest_installer_url(
        json, url, sizeof(url), version, sizeof(version));
    free(json);
    if (!found) {
        cleanup_work(work);
        fail("latest Calculator release has no trusted native installer asset");
    }

    write_status("running", 22, "Downloading Calculator",
                 "Downloading Calculator's published native installer from Infiltrator Projects.");
    printf("Downloading Calculator %s native installer:\n  %s\n", version, url);
    const char *const download_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", installer_path, url, NULL
    };
    required(download_argv);
    if (chmod(installer_path, 0700) != 0) {
        cleanup_work(work);
        fail("cannot mark Calculator native installer executable");
    }

    write_status("running", 35, "Compiling and optimising Calculator",
                 "Calculator's own installer is building this release for the CPU with its canonical aggressive native policy.");
    const char *const install_argv[] = {
        installer_path, "--system-package-mode", NULL
    };
    const int install_result = run(install_argv, false);
    if (install_result != 0) {
        char detail[256];
        (void)snprintf(detail, sizeof(detail),
                       "Calculator's native installer failed with status %d",
                       install_result);
        cleanup_work(work);
        fail(detail);
    }

    write_status("running", 88, "Verifying the native Calculator",
                 "Confirming the package, /Programs executable, launcher, icon, metadata and bundled fonts are all installed correctly.");
    const char *contract_error = installed_contract_error();
    if (contract_error) {
        cleanup_work(work);
        fail(contract_error);
    }

    write_status("running", 94, "Removing replaced Calculator packages",
                 "Cleaning up obsolete Calculator identities and distribution Calculator packages only after the native installation passed verification.");
    purge_replaced_calculators();

    cleanup_work(work);
    write_status("complete", 100, "Calculator native installation complete",
                 "The latest Calculator is installed, menu-integrated and optimised for this computer.");
    puts("InfiltratorOS Calculator replacement completed successfully.");
    close(global_lock_fd);
    close(app_lock_fd);
    return EXIT_SUCCESS;
}
