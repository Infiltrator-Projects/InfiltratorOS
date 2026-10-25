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

#define RELEASE_API "https://api.github.com/repos/Infiltrator-Projects/Defragmenter/releases/latest"
#define RELEASE_PREFIX "https://github.com/Infiltrator-Projects/Defragmenter/releases/download/"
#define PATH_LEN 4096
#define JSON_LIMIT (4U * 1024U * 1024U)
#define STATUS_DIR "/run/infiltratoros"
#define STATUS_PATH STATUS_DIR "/defragmenter-install.status"
#define APP_LOCK_PATH "/run/lock/infiltratoros-defragmenter.lock"
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
    write_status("failed", 100, "Defragmenter installation failed", message);
    fprintf(stderr, "InfiltratorOS Defragmenter setup: %s\n", message);
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
        write_status("failed", 100, "Defragmenter installation failed", detail);
        fprintf(stderr, "InfiltratorOS Defragmenter setup: command failed (%d): %s\n",
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

static bool latest_installer_url(const char *json, char *url, size_t url_size,
                                 char *version, size_t version_size)
{
    const char key[] = "\"browser_download_url\"";
    const char suffix[] = "-local-folder.run";
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
        if (!name || strncmp(name + 1, "Defragmenter-", sizeof("Defragmenter-") - 1U) != 0)
            continue;
        const char *base = name + 1;
        const size_t base_length = strlen(base);
        const size_t name_prefix = sizeof("Defragmenter-") - 1U;
        if (base_length <= name_prefix + suffix_length) continue;
        const size_t version_length = base_length - name_prefix - suffix_length;
        if (version_length + 1U > version_size) return false;
        memcpy(version, base + name_prefix, version_length);
        version[version_length] = '\0';
        for (const unsigned char *v = (const unsigned char *)version; *v; ++v)
            if (!((*v >= '0' && *v <= '9') || *v == '.' || *v == '-')) return false;
        char tag_segment[192];
        if (snprintf(tag_segment, sizeof(tag_segment), "/download/v%s/", version) >=
            (int)sizeof(tag_segment)) return false;
        if (!strstr(candidate, tag_segment) || strlen(candidate) >= url_size)
            continue;
        memcpy(url, candidate, length + 1U);
        return true;
    }
    return false;
}


/* The privileged native compiler must match its published release digest. */
static bool checksum_from_manifest(const char *manifest, const char *basename,
                                   const char *target, const char *check_path)
{
    FILE *source = fopen(manifest, "r");
    if (!source) return false;
    char line[PATH_LEN + 128U], digest[65] = {0};
    int count = 0;
    const size_t name_length = strlen(basename);
    while (fgets(line, sizeof(line), source)) {
        const size_t n = strlen(line);
        if (n != 67U + name_length ||
            line[64] != ' ' || line[65] != ' ' ||
            strncmp(line + 66U, basename, name_length) != 0 ||
            line[66U + name_length] != '\n') continue;
        bool hex = true;
        for (size_t i = 0; i < 64U; ++i)
            if (!isxdigit((unsigned char)line[i])) hex = false;
        if (!hex) continue;
        memcpy(digest, line, 64U);
        ++count;
    }
    if (ferror(source)) count = 0;
    (void)fclose(source);
    if (count != 1) return false;
    FILE *check = fopen(check_path, "w");
    if (!check) return false;
    if (fprintf(check, "%s  %s\n", digest, target) < 0) {
        (void)fclose(check);
        return false;
    }
    return fclose(check) == 0;
}

static bool verified_native_package(const char *version, const char *output)
{
    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) _exit(125);
        if (fd > STDERR_FILENO) (void)close(fd);
        const char *const args[] = {
            "/usr/bin/dpkg-query", "-W",
            "-f=${Version}\t${X-Defragmenter-Build}\n", "defragmenter", NULL
        };
        execv(args[0], (char *const *)args);
        _exit(126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;
    char expected[256];
    if (snprintf(expected, sizeof(expected), "%s+native1\tnative\n", version) >=
        (int)sizeof(expected)) return false;
    char *actual = read_file(output);
    bool matches = actual && strcmp(actual, expected) == 0;
    free(actual);
    return matches;
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
    if (app_lock_fd < 0) fail("cannot create Defragmenter installation lock");
    if (flock(app_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        puts("A Defragmenter native installation is already running.");
        close(app_lock_fd);
        return EXIT_SUCCESS;
    }

    int global_lock_fd = open(GLOBAL_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (global_lock_fd < 0) fail("cannot create global native installation lock");
    write_status("queued", 2, "Waiting for another native installation",
                 "Defragmenter will start as soon as the current Infiltrator native build has finished.");
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

    const char *const wait_argv[] = {
        "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "check", NULL
    };
    required(wait_argv);

    write_status("running", 12, "Checking the latest Defragmenter release",
                 "InfiltratorOS is selecting the newest published native installer.");

    char work[] = "/tmp/defragmenter-XXXXXX";
    if (!mkdtemp(work)) fail("cannot create temporary working directory");

    char json_path[PATH_LEN];
    char installer_path[PATH_LEN];
    if (snprintf(json_path, sizeof(json_path), "%s/release.json", work) >= (int)sizeof(json_path) ||
        snprintf(installer_path, sizeof(installer_path), "%s/defragmenter.run", work) >= (int)sizeof(installer_path))
        fail("temporary path is too long");

    const char *const api_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", json_path, RELEASE_API, NULL
    };
    required(api_argv);

    char *json = read_file(json_path);
    if (!json) fail("cannot read latest Defragmenter release metadata");
    char url[PATH_LEN], version[128];
    bool found = latest_installer_url(json, url, sizeof(url), version, sizeof(version));
    free(json);
    if (!found) fail("latest Defragmenter release has no trusted native installer asset");

    write_status("running", 22, "Downloading Defragmenter",
                 "Downloading the latest native installer from Infiltrator Projects.");
    printf("Downloading latest Defragmenter native installer:\n  %s\n", url);
    const char *const download_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", installer_path, url, NULL
    };
    required(download_argv);
    char manifest_url[PATH_LEN], manifest_path[PATH_LEN], check_path[PATH_LEN];
    const char *basename = strrchr(url, '/');
    if (!basename || basename[1] == '\0' ||
        snprintf(manifest_url, sizeof(manifest_url),
                 RELEASE_PREFIX "v%s/RELEASE_SHA256SUMS.txt", version) >= (int)sizeof(manifest_url) ||
        snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.txt", work) >= (int)sizeof(manifest_path) ||
        snprintf(check_path, sizeof(check_path), "%s/check.txt", work) >= (int)sizeof(check_path))
        fail("invalid release checksum path");
    const char *const manifest_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", manifest_path, manifest_url, NULL
    };
    required(manifest_argv);
    if (!checksum_from_manifest(manifest_path, basename + 1, installer_path, check_path))
        fail("published release checksum is missing or inconsistent");
    const char *const verify_argv[] = {
        "/usr/bin/sha256sum", "--check", "--status", check_path, NULL
    };
    required(verify_argv);
    if (chmod(installer_path, 0700) != 0)
        fail("cannot mark native installer executable");

    write_status("running", 35, "Compiling and optimising Defragmenter",
                 "Building the released source with -O3, -march=native and LTO; trained two-pass PGO is not yet supported.");
    /* Native .run is the product-owned package builder; it has no
     * --system-package-mode switch. The global lock prevents parallel builds. */
    if (setenv("CC", "gcc", 1) != 0 ||
        setenv("CXX", "g++", 1) != 0 ||
        setenv("CFLAGS", "-O3 -march=native -mtune=native -flto", 1) != 0 ||
        setenv("CXXFLAGS", "-O3 -march=native -mtune=native -flto", 1) != 0 ||
        setenv("LDFLAGS", "-flto", 1) != 0)
        fail("could not establish native LTO compiler policy");
    const char *const install_argv[] = {installer_path, NULL};
    required(install_argv);

    write_status("running", 94, "Verifying native Defragmenter",
                 "Checking package build identity, filesystem workers and desktop launchers.");
    char package_info[PATH_LEN];
    if (snprintf(package_info, sizeof(package_info), "%s/package.txt", work) >=
        (int)sizeof(package_info)) fail("invalid package verification path");
    if (!verified_native_package(version, package_info) ||
        !installed("defragmenter") ||
        access("/Programs/defragmenter", X_OK) != 0 ||
        access("/Programs/defragmenter-desktop", X_OK) != 0 ||
        access("/Programs/defragmenter-test-media", X_OK) != 0 ||
        access("/System/Filesystems/Defragmenter/defragmenter-operation-engine", X_OK) != 0 ||
        access("/System/Filesystems/Defragmenter/defragmenter-mapper", X_OK) != 0 ||
        access("/System/Filesystems/Defragmenter/ext4/ext-metadata-worker", X_OK) != 0 ||
        access("/System/Filesystems/Defragmenter/defragmenter-privileged-helper", X_OK) != 0 ||
        access("/System/Commands/defragmenter-ensure-fonts", X_OK) != 0 ||
        access("/System/Resources/Documentation/Defragmenter/README.md", R_OK) != 0 ||
        access("/System/Resources/Icons/defragmenter.png", R_OK) != 0 ||
        !file_contains("/usr/share/applications/io.github.defragmenter.desktop",
                       "Exec=/Programs/defragmenter") ||
        !file_contains("/usr/share/applications/io.github.defragmenter.TestMedia.desktop",
                       "Exec=/Programs/defragmenter-test-media"))
        fail("native package identity, executables or launchers failed qualification");
    unlink(package_info);
    unlink(installer_path);
    unlink(json_path);
    unlink(manifest_path);
    unlink(check_path);
    rmdir(work);
    write_status("complete", 100, "Defragmenter native installation complete",
                 "Defragmenter and Test Media are installed with CPU-native optimisation and LTO.");
    puts("InfiltratorOS Defragmenter installation completed successfully.");
    close(global_lock_fd);
    close(app_lock_fd);
    return EXIT_SUCCESS;
}
