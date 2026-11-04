/* Execute the actual boot-theme command helper without mutating the host. */
#define _POSIX_C_SOURCE 200809L
#define main select_theme_program_main
#include "../packaging/select-theme.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Theme command test failed: %s\n", #x); return EXIT_FAILURE; } } while (0)
int main(void)
{
    char *const good[] = {"/bin/true", NULL};
    char *const failed[] = {"/bin/false", NULL};
    char *const absent[] = {"/missing-infiltratoros-system-update", NULL};
    CHECK(run_system_command(good) == 0);
    CHECK(run_system_command(failed) != 0);
    CHECK(run_system_command(absent) != 0);
    puts("Plymouth/GRUB/initramfs command failures propagate");
    return EXIT_SUCCESS;
}
