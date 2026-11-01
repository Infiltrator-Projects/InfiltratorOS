/* Compile and test the actual production launcher validation function. */
#define main native_launcher_entry_point
#include "../packaging/start-native-install.c"
#undef main

static int expect(const char *slug, bool accepted)
{
    if (valid_provider_slug(slug) == accepted) return 0;
    fprintf(stderr, "Invalid provider-slug result for %s\n", slug ? slug : "(null)");
    return 1;
}

int main(void)
{
    const char *accepted[] = {
        "calendar-gregorian", "clock-binary", "clock-chinese-time",
        "filesystem-fat12", "filesystem-fat16", "filesystem-fat32", "filesystem-ofs", "filesystem-exfat",
        "filesystem-ntfs", "filesystem-ext2", "filesystem-ext3", "filesystem-ext4", "filesystem-xfs", "filesystem-btrfs", "filesystem-ffs", "filesystem-sfs", "filesystem-pfs3", "filesystem-hfs", "filesystem-hfsplus", "filesystem-minix", "filesystem-ufs2", "filesystem-zfs", "filesystem-apfs", "filesystem-swap", "filesystem-sfs2", "filesystem-ufs1"
    };
    const char *rejected[] = {
        "", "calendar-", "clock-", "filesystem-fat", "filesystem-fat1",
        "filesystem-fat64", "filesystem-ofs-extra", "filesystem-exfat-extra", "filesystem-ext444", "filesystem-OFs"
    };
    for (size_t i = 0; i < sizeof(accepted)/sizeof(accepted[0]); ++i)
        if (expect(accepted[i], true)) return 1;
    for (size_t i = 0; i < sizeof(rejected)/sizeof(rejected[0]); ++i)
        if (expect(rejected[i], false)) return 1;
    if (expect(NULL, false)) return 1;
    puts("Native provider slug checks passed, including OFS and exFAT");
    return 0;
}
