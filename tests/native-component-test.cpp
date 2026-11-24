// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the production native C++ installation worker without downloading
// or invoking independent component builds. All tests are non-destructive.
#define main native_component_worker_entrypoint
#include "../packaging/native-component-worker.cpp"
#undef main
#include <functional>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(std::string("Native worker contract: ") + message);
}
static void rejects(const std::function<void()> &action, const char *description) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(std::string("Native worker accepted invalid ") + description);
}
static void add_asset(json_object *assets, const std::string &name, const std::string &url,
                      const std::string &hash = std::string(64, 'a')) {
    json_object *asset = json_object_new_object();
    json_object_object_add(asset, "name", json_object_new_string(name.c_str()));
    json_object_object_add(asset, "browser_download_url", json_object_new_string(url.c_str()));
    json_object_object_add(asset, "digest", json_object_new_string(("sha256:" + hash).c_str()));
    json_object_object_add(asset, "size", json_object_new_int64(123));
    json_object_array_add(assets, asset);
}
static Document sample_release(const Entry &e, const std::string &version, bool temporal,
                               std::string run = {}) {
    auto *data = json_object_new_object();
    json_object_object_add(data, "draft", json_object_new_boolean(0));
    json_object_object_add(data, "prerelease", json_object_new_boolean(0));
    json_object_object_add(data, "tag_name", json_object_new_string(("v" + version).c_str()));
    json_object_object_add(data, "target_commitish",
                           json_object_new_string(std::string(40, 'b').c_str()));
    auto *assets = json_object_new_array();
    const auto prefix = "https://github.com/Infiltrator-Projects/" + e.repo +
                        "/releases/download/v" + version + "/";
    if (run.empty()) run = e.slug() + "-" + version + "-native-installer.run";
    const std::string deb = e.package + "_" + version + "_amd64.deb";
    add_asset(assets, run, prefix + run);
    add_asset(assets, deb, prefix + deb);
    if (temporal) {
        const auto zip = e.slug() + "-" + version + "-source.zip";
        add_asset(assets, zip, prefix + zip);
    }
    json_object_object_add(data, "assets", assets);
    return Document(data);
}
static void check_build_resources() {
    using namespace infiltrator_native;
    std::vector<int> cpus;
    for (int cpu = 0; cpu < 16; ++cpu) cpus.push_back(cpu * 2);
    const BuildResources roomy{cpus, cpus.size(), 24 * 1024};
    std::vector<BuildReservation> active;
    std::set<int> assigned;
    for (std::size_t i = 0; i < build_queue.size(); ++i) {
        const auto kind = build_queue[i] == "files" ? BuildKind::files :
            build_queue[i] == "filesystem-exfat" ? BuildKind::exfat : BuildKind::module;
        auto job = reserve_build(roomy, active, kind, build_queue.size() - i);
        check(job.has_value(), "a machine with room can admit the full 16-repo queue");
        check(job->cpus.size() == 1 && assigned.insert(job->cpus.front()).second,
              "concurrent jobs get distinct CPUs from the allowed affinity mask");
        active.push_back(*job);
    }
    check(!reserve_build(roomy, active, BuildKind::module, 1), "CPU slots are not oversubscribed");
    active.pop_back();
    check(reserve_build(roomy, active, BuildKind::module, 1).has_value(),
          "a finished build frees its CPU and memory reservation");

    TempDir fixture(fs::temp_directory_path(), "native-resources-");
    const auto proc = fixture.path;
    const auto groups = fixture.path / "cgroup";
    fs::create_directories(groups / "parent" / "worker");
    save(proc / "meminfo", "MemTotal: 33554432 kB\nMemAvailable: 25165824 kB\n");
    save(proc / "membership", "0::/parent/worker\n");
    save(groups / "parent" / "cpu.max", "400000 100000\n");
    save(groups / "parent" / "worker" / "cpu.max", "max 100000\n");
    save(groups / "parent" / "memory.max", "4294967296\n");
    save(groups / "parent" / "memory.current", "1073741824\n");
    auto sampled = build_resources(cpus, proc / "meminfo", proc / "membership", groups, proc / "io");
    save(proc / "io", "some avg10=30.00 avg60=10.00 avg300=5.00 total=1\n");
    check(io_pressure_high(proc / "io"), "I/O pressure threshold is detected");
    auto congested = build_resources(cpus, proc / "meminfo", proc / "membership", groups, proc / "io");
    auto first = reserve_build(congested, {}, BuildKind::module, 2);
    check(first.has_value() && !reserve_build(congested, {*first}, BuildKind::module, 1),
          "I/O pressure serialises admissions while allowing one build to progress");
    check(sampled.cpu_budget == 4 && sampled.memory_mib == 2560,
          "parent cgroup quota and memory headroom limit host resources");
    auto files = reserve_build(sampled, {}, BuildKind::files, 1);
    check(files.has_value() && !reserve_build(sampled, {*files}, BuildKind::module, 1),
          "the heavier Files build consumes more of the available RAM");

    save(groups / "parent" / "memory.current", "3758096384\n");
    sampled = build_resources(cpus, proc / "meminfo", proc / "membership", groups, proc / "absent-io");
    auto alone = reserve_build(sampled, {}, BuildKind::module, 2);
    check(alone.has_value() && !reserve_build(sampled, {*alone}, BuildKind::module, 1),
          "live memory pressure prevents another start but allows serial progress");
}
int main() {
    try {
        check_build_resources();
        {
            std::string capture;
            check(execute({"/bin/sh", "-c", "printf verified"}, &capture) == 0 &&
                  capture == "verified", "Subprocess capture retains output");
            rejects([&]() {
                std::string large;
                (void)execute({"/usr/bin/head", "-c", "5000000", "/dev/zero"}, &large);
            }, "Oversized subprocess capture must be rejected after reaping");
            int status = 0;
            errno = 0;
            check(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD,
                  "Oversized capture left an unreaped child");
        }
        {
            TempDir fixture(fs::temp_directory_path(), "native-efi-inventory-");
            const auto image = fixture.path / "BOOTX64.EFI";
            const auto drivers = fixture.path / "drivers_x64";
            fs::create_directory(drivers);
            save(image, "MZboot");
            save(fixture.path / "shimx64.efi", "MZshim");
            for (const auto *name : {"ext2", "ext4", "reiserfs", "iso9660", "hfs", "btrfs"})
                save(drivers / (std::string(name) + "_x64.efi"), "MZdriver");
            const auto initial = boot_efi_inventory(image);
            save(fixture.path / "boot.conf", "user-edited configuration");
            check(initial == boot_efi_inventory(image), "mutable BOOT configuration excluded");
            save(drivers / "btrfs_x64.efi", "MZchanged");
            check(initial != boot_efi_inventory(image), "EFI driver mutation changes attestation");
            fs::remove(drivers / "btrfs_x64.efi");
            rejects([&]() { (void)boot_efi_inventory(image); }, "missing required EFI driver");
        }
        const std::string firmware = R"(BootOrder: 0007,0005,0003
Boot0007* BOOT Boot Manager HD(1,GPT,1234)/File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)
)";
        check(boot_registered(firmware), "BOOT owns one active, first-priority native EFI entry");
        // Secure Boot launches Shim and Shim launches the signed BOOTX64.EFI.
        // Only an explicitly permitted, single active first-priority Shim
        // entry inside BOOT's fixed directory can satisfy this contract.
        // Model efivarfs with a stream buffer that deliberately provides no
        // seek operations. A seek-based implementation fails this regression.
        struct NonSeekableVariable : std::streambuf {
            char bytes[5] = {7, 0, 0, 0, 1};
            explicit NonSeekableVariable(char flag, int size = 5) {
                bytes[4] = flag;
                setg(bytes, bytes, bytes + size);
            }
        };
        NonSeekableVariable enabled_var(1), disabled_var(0), truncated_var(1, 4);
        std::istream enabled_stream(&enabled_var), disabled_stream(&disabled_var),
                     truncated_stream(&truncated_var);
        check(secure_boot_flag(enabled_stream, true), "Non-seekable SecureBoot variable enabled");
        check(!secure_boot_flag(disabled_stream, true), "Non-seekable SecureBoot variable disabled");
        check(!secure_boot_flag(truncated_stream, true), "Truncated SecureBoot variable rejected");
        std::istringstream legacy_enabled(std::string(1, '\1'));
        check(secure_boot_flag(legacy_enabled, false), "Legacy SecureBoot data enabled");
        const std::string shim_firmware = R"(BootCurrent: 0007
Timeout: 0 seconds
BootOrder: 0007,0004,0002,0000,0001,0005
Boot0004* Ubuntu HD(1,GPT,1234)/File(\EFI\ubuntu\shimx64.efi)
Boot0007* BOOT Boot Manager HD(1,GPT,1234)/File(\EFI\InfiltratorOS\BOOT\shimx64.efi) File(.BOOTX64.EFI)
)";
        check(boot_registered(shim_firmware, true),
              "one first-priority BOOT Shim EFI entry accepted for Secure Boot");
        check(!boot_registered(shim_firmware),
              "Shim firmware entry without approved Secure Boot path rejected");
        check(!boot_registered(shim_firmware + shim_firmware.substr(shim_firmware.find("Boot0007")), true),
              "duplicate Shim entries rejected");
        const std::string signed_shim_firmware = R"(BootOrder: 0007,0005
Boot0007* BOOT Boot Manager HD(1,GPT,1234)/File(\EFI\InfiltratorOS\BOOT\shimx64.efi.signed)
)";
        check(boot_registered(signed_shim_firmware, true),
              "installed Shim .efi.signed filename accepted");
        // Hardware-installation regression: BOOT reports Boot0006 first, and
        // the firmware describes the canonical InfiltratorOS/BOOT EFI path.
        // This must not be rejected by the OS post-install validation parser.
        const std::string installed_firmware = R"(BootCurrent: 0006
Timeout: 5 seconds
BootOrder: 0006,0004,0000,0001,0002,0003
Boot0000* UEFI NVMe HD(1,GPT,1234)/File(\EFI\Boot\BootX64.efi)
Boot0004* Ubuntu HD(1,GPT,1234)/File(\EFI\ubuntu\shimx64.efi)
Boot0006* BOOT Boot Manager HD(1,GPT,1234)/File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)
)";
        check(boot_registered(installed_firmware),
              "real BOOT 0.1.558 log proves a unique first-priority fixed EFI entry");
        // The old verifier assumed /usr/sbin/efibootmgr and interpreted a
        // missing binary as a missing BOOT entry. Exercise both modern
        // /usr/bin placement and older /usr/sbin fallback without touching
        // EFI/NVRAM or relying on host packaging.
        TempDir efi_fixture(fs::temp_directory_path(), "native-efi-verifier-");
        const auto missing_binary = efi_fixture.path / "usr-sbin-efibootmgr";
        const auto packaged_binary = efi_fixture.path / "usr-bin-efibootmgr";
        save(missing_binary, "#!/bin/sh\nexit 1\n");
        save(packaged_binary, "#!/bin/sh\nexit 0\n");
        check(chmod(packaged_binary.c_str(), 0755) == 0,
              "mock modern EFI utility can be made executable");
        check(efibootmgr_executable({missing_binary, packaged_binary}) == packaged_binary,
              "EFI verifier finds /usr/bin when /usr/sbin is absent");
        check(chmod(packaged_binary.c_str(), 0644) == 0,
              "mock EFI utility permission can be removed");
        check(efibootmgr_executable({missing_binary, packaged_binary}).empty(),
              "EFI verifier refuses to treat non-executable files as firmware tools");
        check(chmod(missing_binary.c_str(), 0755) == 0,
              "mock legacy EFI utility can be made executable");
        check(efibootmgr_executable({packaged_binary, missing_binary}) == missing_binary,
              "EFI verifier accepts an older /usr/sbin binary when /usr/bin is absent");
        check(!boot_registered(std::string(R"(BootOrder: 0005,0007
)") + firmware.substr(firmware.find("Boot0007"))),
              "BOOT cannot be a secondary boot entry");
        check(!boot_registered(firmware + firmware.substr(firmware.find("Boot0007"))),
              "duplicate BOOT firmware options are refused");
        check(!boot_registered(R"(BootOrder: 0007
Boot0007 BOOT Boot Manager HD(1,GPT,1234)/File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)
)"), "inactive BOOT firmware options are refused");
        auto temporal = catalogue("packaging/temporal-resources.json", 54);
        auto established = catalogue("packaging/filesystem-resources.json", 5);
        auto additional = catalogue("packaging/filesystem-additional.json", 18);
        check(temporal.size() == 54, "temporal cardinality");
        check(established.size() == 5, "established cardinality");
        check(additional.size() == 18, "additional cardinality");
        const auto ntfs = *std::find_if(additional.begin(), additional.end(),
                                       [](const Entry &e) { return e.id == "ntfs"; });
        for (auto version : {"1.0.15", "1.0.16"}) {
            auto release = sample_release(ntfs, version, false);
            rejects([&]() { (void)release_asset(ntfs, release.ptr, false, true); },
                    "NTFS native installer with a FUSE dependency");
        }
        for (auto version : {"1.0.14", "1.0.17"}) {
            auto release = sample_release(ntfs, version, false);
            check(release_asset(ntfs, release.ptr, false, true).version == version,
                  "FUSE-free NTFS native installer");
        }
        TempDir module_fixture(fs::temp_directory_path(), "native-module-");
        const auto source = module_fixture.path / "provider.c";
        const auto module = module_fixture.path / "provider.so";
        const auto compile_provider = [&](const char *declaration) {
            save(source, declaration);
            required({"/usr/bin/cc", "-shared", "-fPIC", "-nostdlib",
                      "-o", module.string(), source.string()});
        };
        compile_provider("void infiltrator_filesystem_api_v2(void) {}\n");
        check(exports_common_filesystem_api(module), "Common V2 function export");
        compile_provider("void infiltrator_filesystem_api_v1(void) {}\n");
        check(!exports_common_filesystem_api(module), "obsolete filesystem export refused");
        compile_provider("void infiltrator_filesystem_api_v2(void) {}\n"
                         "void infiltrator_filesystem_ntfs_v1(void) {}\n");
        check(!exports_common_filesystem_api(module), "mixed filesystem exports refused");
        compile_provider("void infiltrator_filesystem_ntfs_v1(void) {}\n"
                         "void infiltrator_filesystem_api_v2(void) {}\n");
        check(!exports_common_filesystem_api(module), "mixed exports refused in any order");
        compile_provider("extern void infiltrator_filesystem_ntfs_v1(void);\n"
                         "void infiltrator_filesystem_api_v2(void) { infiltrator_filesystem_ntfs_v1(); }\n");
        check(!exports_common_filesystem_api(module), "obsolete filesystem import refused");
        compile_provider("int infiltrator_filesystem_api_v2;\n");
        check(!exports_common_filesystem_api(module), "filesystem data symbol refused");
        compile_provider("extern void infiltrator_filesystem_api_v2(void);\n"
                         "void run(void) { infiltrator_filesystem_api_v2(); }\n");
        check(!exports_common_filesystem_api(module), "undefined filesystem import refused");
        check(infiltrator_native::build_queue.size() == 16, "smart queue cardinality");
        for (auto slug : {"filesystem-fat12", "filesystem-fat16", "filesystem-fat32",
                          "filesystem-exfat", "filesystem-ofs", "filesystem-ntfs",
                          "filesystem-ffs", "filesystem-hfs", "files", "clock-chinese-ke",
                          "calendar-byzantine", "clock-babylonian-hours", "calendar-indian",
                          "calendar-swedish-historical", "calendar-gregorian",
                          "calendar-islamic-umalqura"})
            check(infiltrator_native::queue_candidate(slug), "smart queue membership");
        check(infiltrator_native::queue_candidate("filesystem-ext2") &&
              infiltrator_native::queue_candidate("calendar-armenian-traditional") &&
              !infiltrator_native::queue_candidate("boot"),
              "all selected provider families are eligible; BOOT is not a parallel build");
        check(infiltrator_native::max_parallel_builds == 16, "parallel build limit");
        check(established[0].slug() == "filesystem-fat12", "FAT12 identity");
        check(application("system-monitor").repo == "System-Monitor", "system monitor identity");
        check(package_install_command("/tmp/native.deb") ==
                  Strings({"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1",
                           "install", "-y", "--no-remove", "--reinstall", "/tmp/native.deb"}),
              "Native DEB transactions wait for APT locks and prohibit removals");
        const auto boot = application("boot");
        check(native_installer_command(boot, false, false, "/tmp/boot.run") ==
                  Strings{"/tmp/boot.run"},
              "BOOT native RUN must execute with no arguments or secondary deployment mode");
        check(native_installer_command(application("files"), false, false, "/tmp/files.run") ==
                  Strings({"/tmp/files.run", "--system-package-mode"}),
              "other native application options are preserved");

        auto boot_release = sample_release(boot, "0.1.456", false);
        check(release_asset(boot, boot_release.ptr, false, false).url.find(
                  "boot-0.1.456-native-installer.run") != std::string::npos,
              "BOOT accepts the stable native-run release with verified asset identity");
        rejects([&]() {
            auto prerelease = sample_release(boot, "0.1.456", false);
            json_object_object_add(prerelease.ptr, "prerelease", json_object_new_boolean(1));
            (void)release_asset(boot, prerelease.ptr, false, false);
        }, "BOOT prerelease installer");
        rejects([&]() {
            auto old_boot = sample_release(boot, "0.1.49", false);
            (void)release_asset(boot, old_boot.ptr, false, false);
        }, "unsupported native BOOT release");
        rejects([&]() {
            auto preview = sample_release(boot, "0.1.452", false, "BOOT-0.1.452-amd64.run");
            (void)release_asset(boot, preview.ptr, false, false);
        }, "staging-only BOOT executable");
        rejects([&]() {
            auto no_deb = sample_release(boot, "0.1.452", false);
            json_object_array_del_idx(field(no_deb.ptr, "assets"), 1, 1);
            (void)release_asset(boot, no_deb.ptr, false, false);
        }, "BOOT release without independently published generic package");
        json_object_object_add(boot_release.ptr, "draft", json_object_new_boolean(1));
        rejects([&]() { (void)release_asset(boot, boot_release.ptr, false, false); },
                "unpublished BOOT installer");
        check(application("defragmenter").resource == "/Programs/defragmenter",
              "defragmenter destination");
        // A missing optional Defragmenter field must never invalidate other apps.
        Asset app_version;
        app_version.version = "1.0.243";
        check(application_package_matches(application("system-monitor"), app_version,
            "install ok installed\t1.0.243"), "System Monitor Debian identity");
        check(application_package_matches(application("system-monitor"), app_version,
            "install ok installed\t1.0.243\t"), "trailing empty optional field");
        check(!application_package_matches(application("system-monitor"), app_version,
            "install ok installed\t1.0.242"), "reject stale application version");
        check(!application_package_matches(application("system-monitor"), app_version,
            "deinstall ok config-files\t1.0.243"), "reject uninstalled package");
        app_version.version = "0.2.66";
        check(application_package_matches(application("calculator"), app_version,
            "install ok installed\t0.2.66"), "Calculator native package identity");
        check(!application_package_matches(application("files"), app_version,
            "install ok installed\t0.2.66"), "reject generic Files package");
        check(application_package_matches(application("files"), app_version,
            "install ok installed\t0.2.66+native1"), "Files trained native suffix");
        check(application_package_matches(application("defragmenter"), app_version,
            "install ok installed\t0.2.66+native1"), "Defragmenter trained native suffix");

        {
            const auto calendar = application("calendar");
            check(calendar.repo == "Calendar" && calendar.package == "calendar" &&
                  calendar.resource == "/usr/libexec/calendar-about", "Calendar applet identity");
            auto legacy = sample_release(calendar, "1.0.130", false,
                                         "calendar-1.0.130-local-folder.run");
            rejects([&]() { (void)release_asset(calendar, legacy.ptr, false, false); },
                    "Calendar release that still invokes the Python scanner");
            auto release = sample_release(calendar, "1.0.131", false,
                                          "calendar-1.0.131-local-folder.run");
            auto asset = release_asset(calendar, release.ptr, false, false);
            check(asset.version == "1.0.131" &&
                  asset.url == "https://github.com/Infiltrator-Projects/Calendar/releases/download/"
                               "v1.0.131/calendar-1.0.131-local-folder.run",
                  "Calendar release installer");
            check(application_package_matches(calendar, asset,
                  "install ok installed\t1.0.131+nativepgo1"), "Calendar native Debian identity");
            for (auto version : {"1.0.131", "1.0.130+nativepgo1", "1.0.131+nativepgo1bad"})
                check(!application_package_matches(calendar, asset,
                      std::string("install ok installed\t") + version),
                      "Reject generic, stale and malformed Calendar versions");
            json_object_object_add(release.ptr, "target_commitish", json_object_new_string("main"));
            rejects([&]() { (void)release_asset(calendar, release.ptr, false, false); },
                    "unpinned Calendar release");
            auto wrong = sample_release(calendar, "1.0.131", false);
            rejects([&]() { (void)release_asset(calendar, wrong.ptr, false, false); },
                    "wrong Calendar installer filename");
        }
        rejects([&]() { (void)application("unrecognised"); }, "application");

        // Both the new network-only two-asset release and immutable legacy
        // releases must be accepted; arbitrary extras and missing DEBs must not.
        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto *assets = field(release.ptr, "assets");
            json_object_array_del_idx(assets, 2, 1);
            check(release_asset(temporal[0], release.ptr, true, false).version == "1.0.5",
                  "two-asset temporal release");
            json_object_array_del_idx(assets, 1, 1);
            rejects([&]() { (void)release_asset(temporal[0], release.ptr, true, false); },
                    "missing temporal generic DEB");
        }

        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto asset = release_asset(temporal[0], release.ptr, true, false);
            check(asset.size == 123 && asset.sha == std::string(64, 'a') &&
                  asset.source_revision == std::string(40, 'b'),
                  "temporal digest and size");
            json_object_object_add(release.ptr, "draft", json_object_new_boolean(1));
            rejects([&]() { (void)release_asset(temporal[0], release.ptr, true, false); },
                    "draft release");
        }
        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto *assets = field(release.ptr, "assets");
            json_object *run = json_object_array_get_idx(assets, 0);
            json_object_object_add(run, "digest", json_object_new_string("sha256:INVALID"));
            rejects([&]() { (void)release_asset(temporal[0], release.ptr, true, false); },
                    "asset SHA-256");
        }
        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto *assets = field(release.ptr, "assets");
            json_object *run = json_object_array_get_idx(assets, 0);
            json_object_object_add(run, "browser_download_url",
                                   json_object_new_string("https://attacker.example/script.run"));
            rejects([&]() { (void)release_asset(temporal[0], release.ptr, true, false); },
                    "asset owner");
        }
        {
            auto old_release = sample_release(established[0], "0.1.23", false);
            rejects([&]() { (void)release_asset(established[0], old_release.ptr, false, true); },
                    "reject legacy FAT12 release with forbidden FUSE dependencies");
            auto release = sample_release(established[0], "0.1.24", false);
            auto asset = release_asset(established[0], release.ptr, false, true);
            check(asset.version == "0.1.24", "minimum permitted FAT12 release");
        }
        {
            auto it = std::find_if(established.begin(), established.end(),
                         [](const Entry &e) { return e.id == "exfat"; });
            check(it != established.end(), "exFAT manifest identity");
            auto release = sample_release(*it, "0.1.2", false);
            rejects([&]() { (void)release_asset(*it, release.ptr, false, true); },
                    "known-broken exFAT release");
        }
        {
            setenv("INFILTRATOROS_SELECTED_FILESYSTEMS", "not-a-filesystem", 1);
            rejects([&]() { list(established, 5, "INFILTRATOROS_SELECTED_FILESYSTEMS", true); },
                    "filesystem selection");
            unsetenv("INFILTRATOROS_SELECTED_FILESYSTEMS");
        }
        {
            std::string base = "/tmp/infiltratoros-native-unit-XXXXXX";
            std::vector<char> writable(base.begin(), base.end());
            writable.push_back(0);
            char *name = mkdtemp(writable.data());
            if (!name) throw std::runtime_error("Cannot create disposable test directory");
            fs::path path(name);
            Status status(path, "test-driver");
            status.write("queued", 2, "Waiting", "Native status");
            check(read_file(path / "test-driver-install.status").find("state=queued\n") == 0,
                  "atomic status contract");
            save(path / "sha.txt", "abc");
            check(digest(path / "sha.txt") ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                  "OpenSSL SHA-256 contract");
            fs::remove_all(path);
        }
        std::cout << "Native C++ provider contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
