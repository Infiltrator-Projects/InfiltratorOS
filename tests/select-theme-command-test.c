/* Exercise the actual Plymouth selector without touching host boot files.
 * On Ubuntu the Debian-only theme command is not installed: our isolated PATH
 * deliberately supplies only update-alternatives, update-grub and initramfs. */
#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char test_theme[PATH_MAX];
static char test_png[PATH_MAX];
static char test_conf_dir[PATH_MAX];
static char test_conf[PATH_MAX];
static char test_grub[PATH_MAX];
static char grub_tool[PATH_MAX];
#define FILE_THEME test_theme
#define FILE_PNG test_png
#define CONF_DIR test_conf_dir
#define FILE_CONF test_conf
#define FILE_GRUB test_grub
#define GRUB_UPDATE grub_tool
#define main select_theme_program_main
#include "../packaging/select-theme.c"
#undef main

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Theme regression failed at line %d: %s\n", __LINE__, #x); \
    return EXIT_FAILURE; \
} } while (0)

static int touch(const char *path, const char *contents, mode_t mode)
{
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    if (fputs(contents, file) == EOF || fclose(file) != 0) return -1;
    return chmod(path, mode);
}

static int read_log(const char *path, char *text, size_t size)
{
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    size_t bytes = fread(text, 1, size - 1U, file);
    int valid = !ferror(file);
    text[bytes] = '\0';
    fclose(file);
    return valid ? 0 : -1;
}

int main(void)
{
    char *const good[] = {"/bin/true", NULL};
    char *const failed[] = {"/bin/false", NULL};
    char *const absent[] = {"/missing-infiltratoros-system-update", NULL};
    CHECK(required(good) == 0);
    CHECK(required(failed) != 0);
    CHECK(required(absent) != 0);

    char root[] = "/tmp/infiltratoros-theme-test-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char bin[PATH_MAX], stub[PATH_MAX], log[PATH_MAX], alternatives[PATH_MAX];
    char initramfs_tool[PATH_MAX];
#define PATH_JOIN(buffer, suffix) CHECK(snprintf(buffer, sizeof(buffer), "%s/%s", root, suffix) < (int)sizeof(buffer))
    PATH_JOIN(test_theme, "infiltratoros.plymouth");
    PATH_JOIN(test_png, "infiltratoros.png");
    PATH_JOIN(test_conf_dir, "plymouth");
    PATH_JOIN(test_conf, "plymouth/plymouthd.conf");
    PATH_JOIN(test_grub, "grub");
    PATH_JOIN(bin, "bin");
    PATH_JOIN(stub, "bin/stub");
    PATH_JOIN(log, "execution.log");
    PATH_JOIN(alternatives, "bin/update-alternatives");
    PATH_JOIN(grub_tool, "bin/update-grub");
    PATH_JOIN(initramfs_tool, "bin/update-initramfs");

    CHECK(mkdir(bin, 0700) == 0);
    CHECK(touch(test_theme, "[Plymouth Theme]\nName=InfiltratorOS\n", 0600) == 0);
    CHECK(touch(test_png, "mock image", 0600) == 0);
    CHECK(touch(test_grub, "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet splash\"\n", 0600) == 0);
    CHECK(touch(stub,
        "#!/bin/sh\n"
        "printf '%s\\n' \"$0 $*\" >> \"$INFILTRATOR_TEST_LOG\"\n"
        "case \"$0\" in *\"/$INFILTRATOR_FAIL_CMD\") exit 7;; esac\n"
        "exit 0\n", 0700) == 0);
    CHECK(symlink(stub, alternatives) == 0);
    CHECK(symlink(stub, grub_tool) == 0);
    CHECK(symlink(stub, initramfs_tool) == 0);
    CHECK(setenv("PATH", bin, 1) == 0);
    CHECK(setenv("INFILTRATOR_TEST_LOG", log, 1) == 0);
    CHECK(setenv("INFILTRATOR_FAIL_CMD", "none", 1) == 0);

    char *configure[] = {"select-theme", "configure", NULL};
    CHECK(select_theme_program_main(2, configure) == 0);
    char output[4096];
    CHECK(read_log(log, output, sizeof(output)) == 0);
    char *install = strstr(output, "update-alternatives --install");
    char *select = strstr(output, "update-alternatives --set");
    char *grub = strstr(output, "update-grub");
    char *rebuild = strstr(output, "update-initramfs -u -k all");
    CHECK(install && select && grub && rebuild && install < select && select < grub && grub < rebuild);
    CHECK(strstr(output, "plymouth-set-default-theme") == NULL);
    CHECK(access(test_conf, R_OK) == 0);

    CHECK(touch(log, "", 0600) == 0);
    CHECK(setenv("INFILTRATOR_FAIL_CMD", "update-alternatives", 1) == 0);
    CHECK(select_theme_program_main(2, configure) != 0);
    CHECK(read_log(log, output, sizeof(output)) == 0);
    CHECK(strstr(output, "update-alternatives --install") != NULL);
    CHECK(strstr(output, "update-initramfs") == NULL);
    CHECK(touch(log, "", 0600) == 0);
    CHECK(setenv("INFILTRATOR_FAIL_CMD", "update-initramfs", 1) == 0);
    CHECK(select_theme_program_main(2, configure) != 0);

    CHECK(touch(log, "", 0600) == 0);
    CHECK(touch(test_grub, "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet\"\n", 0600) == 0);
    CHECK(setenv("INFILTRATOR_FAIL_CMD", "update-grub", 1) == 0);
    CHECK(select_theme_program_main(2, configure) != 0);
    CHECK(read_log(test_grub, output, sizeof(output)) == 0);
    CHECK(strstr(output, "\"quiet splash\"") != NULL);
    CHECK(read_log(log, output, sizeof(output)) == 0);
    CHECK(strstr(output, "update-initramfs") == NULL);
    CHECK(touch(log, "", 0600) == 0);
    CHECK(setenv("INFILTRATOR_FAIL_CMD", "none", 1) == 0);
    CHECK(select_theme_program_main(2, configure) == 0);
    CHECK(read_log(log, output, sizeof(output)) == 0);
    CHECK(strstr(output, "update-grub") != NULL);
    CHECK(strstr(output, "update-initramfs") != NULL);

    CHECK(unlink(grub_tool) == 0);
    CHECK(touch(log, "", 0600) == 0);
    CHECK(touch(test_grub, "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet\"\n", 0600) == 0);
    CHECK(select_theme_program_main(2, configure) == 0);
    CHECK(read_log(test_grub, output, sizeof(output)) == 0);
    CHECK(strcmp(output, "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet\"\n") == 0);
    CHECK(read_log(log, output, sizeof(output)) == 0);
    CHECK(strstr(output, "update-grub") == NULL);
    CHECK(strstr(output, "update-initramfs") != NULL);

    CHECK(unlink(test_grub) == 0);
    CHECK(symlink(stub, grub_tool) == 0);
    CHECK(touch(log, "", 0600) == 0);
    CHECK(select_theme_program_main(2, configure) == 0);
    CHECK(access(test_grub, F_OK) != 0 && errno == ENOENT);
    CHECK(read_log(log, output, sizeof(output)) == 0);
    CHECK(strstr(output, "update-grub") == NULL);

    CHECK(unlink(initramfs_tool) == 0);
    CHECK(unlink(grub_tool) == 0);
    CHECK(unlink(alternatives) == 0);
    CHECK(unlink(stub) == 0);
    CHECK(unlink(test_png) == 0);
    CHECK(unlink(test_theme) == 0);
    CHECK(unlink(test_conf) == 0);
    CHECK(unlink(log) == 0);
    CHECK(rmdir(test_conf_dir) == 0);
    CHECK(rmdir(bin) == 0);
    CHECK(rmdir(root) == 0);
    puts("Ubuntu-compatible Plymouth activation and strict command failures passed");
    return EXIT_SUCCESS;
}
