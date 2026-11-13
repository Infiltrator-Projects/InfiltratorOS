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
    auto sampled = build_resources(cpus, proc / "meminfo", proc / "membership", groups);
    check(sampled.cpu_budget == 4 && sampled.memory_mib == 2560,
          "parent cgroup quota and memory headroom limit host resources");
    auto files = reserve_build(sampled, {}, BuildKind::files, 1);
    check(files.has_value() && !reserve_build(sampled, {*files}, BuildKind::module, 1),
          "the heavier Files build consumes more of the available RAM");

    save(groups / "parent" / "memory.current", "3758096384\n");
    sampled = build_resources(cpus, proc / "meminfo", proc / "membership", groups);
    auto alone = reserve_build(sampled, {}, BuildKind::module, 2);
    check(alone.has_value() && !reserve_build(sampled, {*alone}, BuildKind::module, 1),
          "live memory pressure prevents another start but allows serial progress");
}
int main() {
    try {
        check_build_resources();
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
        check(!infiltrator_native::queue_candidate("filesystem-ext2"),
              "do not admit unrelated independent providers");
        check(established[0].slug() == "filesystem-fat12", "FAT12 identity");
        check(application("system-monitor").repo == "System-Monitor", "system monitor identity");
        const auto boot = application("boot");
        const Asset boot_version{"0.1.47", "", "", 0};
        check(application_package_matches(boot, boot_version, "install ok installed\t0.1.47+native1"),
              "BOOT accepts its locally compiled package");
        check(!application_package_matches(boot, boot_version, "install ok installed\t0.1.47"),
              "BOOT rejects the portable package as native evidence");
        auto boot_release = sample_release(boot, "0.1.47", false);
        check(release_asset(boot, boot_release.ptr, false, false).url.find("boot-0.1.47-native-installer.run") != std::string::npos,
              "BOOT resolves its independent source installer");
        rejects([&]() {
            auto old_boot = sample_release(boot, "0.1.46", false);
            (void)release_asset(boot, old_boot.ptr, false, false);
        }, "BOOT release without support for the installed EXT4 shared object");
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

        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto asset = release_asset(temporal[0], release.ptr, true, false);
            check(asset.size == 123 && asset.sha == std::string(64, 'a'),
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
            fs::copy_file("packaging/filesystem-resources.json", path / "filesystem-resources.json");
            fs::copy_file("packaging/filesystem-additional.json", path / "filesystem-additional.json");
            save(path / "selected.list", "boot\nfiles\nfilesystem-fat16\nfilesystem-hfs\n");
            auto selected = boot_filesystems(path);
            check(selected.size() == 2 && selected[0].id == "fat16" && selected[1].id == "hfs",
                  "BOOT uses only selected filesystem providers");
            Status(path, "filesystem-fat16").write("complete", 100, "Installed", "");
            Status(path, "filesystem-hfs").write("failed", 100, "Failed", "");
            wait_for_boot_filesystems(path, selected);
            check(provider_state(path, selected[0]) == "complete" &&
                  provider_state(path, selected[1]) == "failed", "BOOT waits for terminal provider states");
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
