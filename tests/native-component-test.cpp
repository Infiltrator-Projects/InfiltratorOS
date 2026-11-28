// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the production native C++ installation worker without downloading
// or invoking independent component builds. All tests are non-destructive.
#define INFILTRATOROS_NATIVE_WORKER_TEST 1
#include "../packaging/native-component-worker.cpp"
#include "../packaging/native-build-resources.h"
#include "../packaging/native-build-queue.h"
#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <sys/wait.h>

using namespace infiltrator_native::worker;

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
static void check_build_resources() {
    using namespace infiltrator_native;
    std::vector<int> cpus;
    // Admission is limited by resources and pending work, not by the old
    // 16-job pilot. Include high-capacity machines and sparse CPU masks.
    for (int cpu = 0; cpu < 64; ++cpu) cpus.push_back(cpu * 2);
    const BuildResources roomy{cpus, cpus.size(), 128 * 1024, false, 128 * 1024};
    std::vector<BuildReservation> active;
    std::set<int> assigned;
    for (std::size_t i = 0; i < cpus.size(); ++i) {
        const auto kind = i == 1 ? BuildKind::files :
            i == 2 ? BuildKind::exfat : BuildKind::module;
        auto job = reserve_build(roomy, active, kind, cpus.size() - i);
        check(job.has_value(), "resource-rich machine can admit 64 simultaneous jobs");
        check(job->cpus.size() == 1 && assigned.insert(job->cpus.front()).second,
              "concurrent jobs get distinct CPUs from the allowed affinity mask");
        active.push_back(*job);
    }
    check(active.size() == 64, "no artificial 16-build cap");
    check(!reserve_build(roomy, active, BuildKind::module, 1), "CPU slots are not oversubscribed");
    active.pop_back();
    check(reserve_build(roomy, active, BuildKind::module, 1).has_value(),
          "a finished build frees its CPU and memory reservation");
    const BuildResources ram_limited{cpus, cpus.size(), 24 * 768, false, 24 * 768};
    active.clear();
    for (int i = 0; i < 24; ++i) {
        auto job = reserve_build(ram_limited, active, BuildKind::module, 64 - i);
        check(job.has_value(), "RAM permits 24 jobs despite former ceiling");
        active.push_back(*job);
    }
    check(!reserve_build(ram_limited, active, BuildKind::module, 1),
          "RAM limits further admissions even when CPU capacity remains");
    check(!allowed_build_cpus().empty(), "actual Linux CPU affinity is discoverable");

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
    check(!reserve_build(congested, {}, BuildKind::module, 2),
          "I/O pressure pauses admission even with no active build");
    check(sampled.cpu_budget == 4 && sampled.memory_mib == 2560,
          "parent cgroup quota and memory headroom limit host resources");
    auto files = reserve_build(sampled, {}, BuildKind::files, 1);
    check(files.has_value() && !reserve_build(BuildResources{cpus, 4, 0}, {*files}, BuildKind::module, 1),
          "the heavier Files build consumes more of the available RAM");

    save(groups / "parent" / "memory.current", "3758096384\n");
    sampled = build_resources(cpus, proc / "meminfo", proc / "membership", groups, proc / "absent-io");
    check(!reserve_build(sampled, {}, BuildKind::module, 2),
          "live memory starvation cannot be bypassed by a serial start");
    auto busy = roomy;
    busy.idle_cpus = std::vector<int>{2, 6};
    auto job = reserve_build(busy, {}, BuildKind::module, 1);
    check(job && job->cpus == std::vector<int>({2, 6}), "last build can use spare idle CPUs");
    busy.disk_mib = 0;
    check(!reserve_build(busy, {}, BuildKind::module, 1), "disk exhaustion blocks admission");
    busy.disk_mib = 1024;
    busy.disk_capacity_mib = 512;
    busy.idle_cpus.reset();
    check(job && !reserve_build(busy, {*job}, BuildKind::module, 1), "disk reservations enforced");
    const CpuTicks before{{0, {100, 10}}, {2, {100, 50}}};
    const CpuTicks after{{0, {200, 15}}, {2, {200, 100}}};
    auto idle = idle_build_cpus({0, 2}, before, after);
    check(idle && *idle == std::vector<int>{2}, "interval CPU load filters busy CPUs");
}
static int rollback_fixture(const fs::path &base) {
    // Only run against an intentionally disposable CI host, with a unique
    // synthetic package that cannot conflict with an Infiltrator application.
    const std::string name = "infiltratoros-ci-rollback-fixture";
    const std::string path = "/usr/share/" + name + "/value";
    if (geteuid() != 0 || !pkg_query(name, "${Status}").empty()) return 1;
    TempDir fixture(base, "native-rollback-test-");
    auto package = [&](const std::string &version, const std::string &content) {
        const auto root = fixture.path / ("tree-" + version);
        fs::create_directories(root / "DEBIAN");
        const auto file = root / fs::path(path).relative_path();
        fs::create_directories(file.parent_path());
        save(file, content);
        save(root / "DEBIAN/control", "Package: " + name + "\nVersion: " + version +
            "\nArchitecture: amd64\nMaintainer: Test <test@example.invalid>\nDescription: rollback fixture\n");
        const auto deb = fixture.path / (name + "_" + version + "_amd64.deb");
        required({"/usr/bin/dpkg-deb", "--build", "--root-owner-group", root.string(), deb.string()});
        return deb;
    };
    int result = 1;
    try {
        const auto old = package("1.0.1", "previous\n");
        const auto next = package("1.0.2", "replacement\n");
        required({"/usr/bin/dpkg", "-i", old.string()});
        Entry entry{"fixture", "fixture", "InfiltratorOS-CI-Rollback-Fixture", name, path, "application"};
        bool restored = false;
        try {
            replace_deb_with_recovery(entry, next, fixture.path, [&] {
                check(read_file(path) == "replacement\n", "replacement installed before injected verification failure");
                throw std::runtime_error("injected post-install verification failure");
            });
        } catch (const std::exception &error) {
            restored = std::string(error.what()).find("previous package restored and verified") != std::string::npos;
            if (!restored) std::cerr << error.what() << '\n';
        }
        check(restored && pkg_query(name, "${Version}") == "1.0.1" &&
              read_file(path) == "previous\n", "failed replacement restores actual previous Debian package");
        std::cout << "Native Debian package rollback fixture passed\n";
        result = 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    if (execute({"/usr/bin/dpkg", "--purge", name}) != 0) result = 1;
    return result;
}
int main(int argc, char **argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--rollback-fixture")
        return rollback_fixture(argv[2]);
    if (argc == 4 && std::string_view(argv[1]) == "--validate-common-v2")
        return native_component_worker_entrypoint(argc, argv);
    try {
        check_build_resources();
        for (const auto &contract : infiltrator_native::application_contracts) {
            const auto entry = application(std::string(contract.slug));
            check(entry.slug() == contract.slug && entry.repo == contract.repository &&
                  entry.package == contract.package && entry.resource == contract.resource &&
                  infiltrator_native::queue_candidate(entry.slug()) == contract.queued,
                  "one application registry drives package and queue identities");
        }
        {
            TempDir temp(fs::temp_directory_path(), "native-status-bounds-");
            Status status(temp.path, "test");
            status.write("failed", 100, std::string(500, 's'), std::string(5000, 'd'));
            const auto record = read_file(status.path);
            check(record.find("stage=" + std::string(252, 's') + "...\n") != std::string::npos &&
                  record.find("detail=" + std::string(1020, 'd') + "...\n") != std::string::npos,
                  "oversized status diagnostics remain valid and bounded");
        }
        {
            std::string capture;
            check(execute({"/bin/sh", "-c", "printf verified"}, &capture) == 0 &&
                  capture == "verified", "Subprocess capture retains output");
            // Simulate an unrelated parallel download with a writable
            // descriptor open when the native worker launches a child.
            TempDir fixture(fs::temp_directory_path(), "native-fd-");
            const auto run = fixture.path / "pending.run";
            int fd = open(run.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0700);
            check(fd >= 3, "Writable installer descriptor fixture");
            const int result = execute({"/bin/sh", "-c",
                "test ! -e /proc/self/fd/$1", "fd-check", std::to_string(fd)});
            close(fd);
            check(result == 0, "Native child cannot inherit another installer's writable descriptor");
            const auto log = fixture.path / "exec.log";
            check(execute({"/nonexistent/infiltrator-native-command"}, nullptr, log) == 127 &&
                  read_file(log).find("Cannot execute native command") != std::string::npos,
                  "Failed exec records a diagnostic");
            rejects([&]() {
                std::string large;
                (void)execute({"/usr/bin/head", "-c", "5000000", "/dev/zero"}, &large);
            }, "Oversized subprocess capture must be rejected after reaping");
            int status = 0;
            errno = 0;
            check(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD,
                  "Oversized capture left an unreaped child");
        }
        check(installed_package_matches("install ok installed\t1.2.3", "1.2.3"),
              "Current generic release is skipped");
        check(installed_package_matches("install ok installed\t1.2.3+native1", "1.2.3"),
              "Current native release is skipped");
        check(installed_package_matches("install ok installed\t1.2.3+nativepgo12", "1.2.3"),
              "Current PGO release is skipped");
        check(!installed_package_matches("install ok installed\t1.2.30+native1", "1.2.3") &&
              !installed_package_matches("install ok installed\t1.2.2+native1", "1.2.3") &&
              !installed_package_matches("install ok unpacked\t1.2.3", "1.2.3") &&
              !installed_package_matches("", "1.2.3"),
              "Different, incomplete and absent versions are not skipped");
        const std::string sbat = "sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md\n"
            "boot,1,Infiltrator-Projects,boot,0.1.697,https://github.com/Infiltrator-Projects/Boot\n";
        check(boot_release_matches(sbat, "0.1.697") &&
              !boot_release_matches(sbat, "0.1.69") &&
              !boot_release_matches(sbat, "0.1.698"), "BOOT's own SBAT release identity");
        auto temporal = catalogue("packaging/temporal-resources.json", 54);
        auto established = catalogue("packaging/filesystem-resources.json", 5);
        auto additional = catalogue("packaging/filesystem-additional.json", 39);
        check(temporal.size() == 54, "temporal cardinality");
        check(established.size() == 5, "established cardinality");
        check(additional.size() == 39, "additional cardinality");
        const std::set<std::string> documentation_ids{"bcachefs", "nilfs2", "vmfs6"};
        std::set<std::string> observed_documentation;
        for (const auto &entry : additional) {
            if (entry.kind != "documentation") continue;
            check(entry.resource == "/System/Resources/Documentation/" + entry.repo + ".pdf",
                  "documentation-only provider has a PDF, not a shared-object resource");
            check(infiltrator_native::queue_candidate(entry.slug()),
                  "documentation-only component uses normal resource admission");
            const auto deps = build_dependency_packages(entry);
            check(std::find(deps.begin(), deps.end(), "build-essential") == deps.end(),
                  "documentation packaging does not require a compiler");
            observed_documentation.insert(entry.id);
        }
        check(observed_documentation == documentation_ids, "exact documentation-only inventory");
        // The two new released providers follow the same generic queue and
        // package/resource identity as all other independent filesystems.
        for (const auto *id : {"bfs", "efs"}) {
            const auto entry = std::find_if(additional.begin(), additional.end(),
                [&](const Entry &e) { return e.id == id; });
            const std::string slug = std::string("filesystem-") + id;
            const std::string name = id == std::string_view("bfs") ? "BFS" : "EFS";
            check(entry != additional.end() && entry->slug() == slug &&
                  entry->repo == "Filesystem-" + name &&
                  entry->package == "infiltrator-" + slug &&
                  entry->resource == "/System/Filesystems/" + name + ".so" &&
                  infiltrator_native::queue_candidate(slug),
                  "new filesystem uses the ordinary independent native queue");
        }
        for (const auto *id : {"cpm", "gfs2", "jfs", "f2fs"}) {
            const auto entry = std::find_if(additional.begin(), additional.end(),
                [&](const Entry &e) { return e.id == id; });
            check(entry != additional.end() && entry->kind != "documentation", "published runtime promoted");
            const std::string name = id == std::string_view("cpm") ? "CPM" : id == std::string_view("gfs2") ? "GFS2" : id == std::string_view("jfs") ? "JFS" : "F2FS";
            check(entry->resource == "/System/Filesystems/" + name + ".so", "native runtime resource");
            const auto deps = build_dependency_packages(*entry);
            check(std::find(deps.begin(), deps.end(), "build-essential") != deps.end(), "native compilation dependencies");
        }
        for (const auto *id : {"ocfs2", "vmfs"}) {
            const auto entry = std::find_if(additional.begin(), additional.end(),
                [&](const Entry &e) { return e.id == id; });
            check(entry != additional.end() && entry->kind != "documentation", "qualified runtime entry");
            const std::string name = id == std::string_view("ocfs2") ? "OCFS2" : "VMFS";
            check(entry->resource == "/System/Filesystems/" + name + ".so", "native shared-object path");
            const auto deps = build_dependency_packages(*entry);
            check(std::find(deps.begin(), deps.end(), "build-essential") != deps.end(), "runtime build prerequisites");
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
        check(!validate_common_descriptor(module, "fat12"), "symbol without a valid descriptor refused");
        const auto valid_module = module_fixture.path / "valid.so";
        const auto invalid_module = module_fixture.path / "invalid.so";
        const std::string fixture_source = "src/infiltratr-common/tests/filesystem_module_v2_native_fixture.c";
        required({"/usr/bin/cc", "-shared", "-fPIC", "-Isrc/infiltratr-common/include",
                  "-o", valid_module.string(), fixture_source});
        required({"/usr/bin/cc", "-shared", "-fPIC", "-Isrc/infiltratr-common/include",
                  "-DIFS_FIXTURE_INVALID_V2", "-o", invalid_module.string(), fixture_source});
        check(validate_common_descriptor(valid_module, "fat12"), "canonical Common V2 descriptor validated");
        check(!validate_common_descriptor(valid_module, "ntfs"), "wrong native format identity rejected");
        check(!validate_common_descriptor(invalid_module, "fat12"), "invalid claimed callback group rejected");
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
        for (const auto app : infiltrator_native::queued_applications) {
            check(infiltrator_native::queue_candidate(app), "all five applications are queued");
            const auto e = application(std::string(app));
            check(!application_build_dependencies(e).empty(), "application prerequisites declared");
            auto args = build_only_arguments(e, "/tmp/output", "2");
            if (app == "calendar")
                check(args == Strings({"--build-only", "--output-dir", "/tmp/output"}),
                      "Calendar build-only contract");
            else if (app == "calculator" || app == "system-monitor")
                check(args == Strings({"--system-package-mode", "--jobs", "2", "--build-only", "--output", "/tmp/output"}),
                      "explicit application CPU reservation");
            else
                check(args == Strings({"--system-package-mode", "--build-only", "--output", "/tmp/output"}),
                      "application-owned build-only contract");
        }
        {
            // Execute the actual scheduler and subprocess path with five
            // disposable packages; no host package installation is performed.
            TempDir fixture(fs::temp_directory_path(), "native-all-apps-");
            std::vector<Job> jobs;
            std::map<std::string, std::string> failures;
            const auto logs = fixture.path / "logs";
            fs::create_directory(logs);
            Asset asset;
            asset.version = "1.2.3";
            for (const auto slug : infiltrator_native::queued_applications) {
                const auto app = application(std::string(slug));
                const auto dir = fixture.path / app.slug();
                const auto package_tree = dir / "tree";
                fs::create_directories(package_tree / "DEBIAN");
                const auto version = app.id == "calendar" ? "1.2.3+nativepgo1" :
                    app.id == "files" || app.id == "defragmenter" ? "1.2.3+native1" : "1.2.3";
                save(package_tree / "DEBIAN/control", "Package: " + app.package +
                     "\nVersion: " + version + "\nArchitecture: amd64\n"
                     "Maintainer: Test <test@example.invalid>\nDescription: disposable scheduler fixture\n");
                required({"/usr/bin/dpkg-deb", "--build", "--root-owner-group",
                          package_tree.string(), (dir / "input.deb").string()});
                auto run = dir / "native.run";
                // The private output folder is the final option in each
                // supported contract. Check the actual CPU count as seen by
                // the released getconf-based Calendar/Defragmenter scripts.
                save(run, "#!/bin/sh\nset -eu\nout=''\n"
                     "while [ \"$#\" -gt 0 ]; do\n"
                     " case \"$1\" in --output|--output-dir) shift; out=$1;; esac\n shift\n done\n"
                     "[ -n \"$out\" ]\n"
                     "if [ -x \"$out/scheduler-bin/getconf\" ]; then\n"
                     " [ \"$(getconf _NPROCESSORS_ONLN)\" = \"$(printf '%s' \"$MAKEFLAGS\" | cut -c3-)\" ]\n"
                     "fi\n"
                     "cp \"$(dirname \"$0\")/input.deb\" \"$out/result.deb\"\n");
                if (chmod(run.c_str(), 0700) != 0)
                    throw std::runtime_error("Cannot make fixture RUN executable");
                const auto folder = dir / "output";
                fs::create_directory(folder);
                jobs.push_back({app, {}, run, folder, {}, true});
            }
            // Inject only the network lookup. Each actual scheduler admission
            // must precede its own lookup, which then runs the local RUN.
            build_packages(jobs, logs, failures, [&](const Entry &entry) {
                const auto status = read_file(logs / (entry.slug() + "-install.status"));
                check(status.find("stage=Resolving independent installer\n") != std::string::npos,
                      "lookup runs inside the admitted job");
                check(read_file(logs / "smart-build-timings.log").find("started " + entry.slug() + ";") != std::string::npos,
                      "scheduler reserves resources before lookup");
                return asset;
            });
            check(failures.empty(), "all five application native build paths execute successfully");
            // A current result must bypass both RUN execution and installation.
            const auto skipdir = fixture.path / "already-current";
            fs::create_directory(skipdir);
            std::vector<Job> current_jobs{{application("calculator"), {}, skipdir / "missing.run", skipdir, {}, true}};
            bool installation_called = false;
            build_packages(current_jobs, logs, failures, [](const Entry &) {
                return Asset{"1.2.3", "", "", 0, true};
            }, [&](Job &) { installation_called = true; });
            check(failures.empty() && !installation_called &&
                  read_file(logs / "calculator-install.status").find("state=complete\n") != std::string::npos &&
                  read_file(logs / "smart-build-timings.log").find("skipped calculator") != std::string::npos,
                  "Current component completes without running a missing installer");
            for (const auto &job : jobs) {
                check(regular(job.package), "scheduler returns verified real Debian package");
                auto wrong = job.entry;
                wrong.package = "not-the-component";
                rejects([&]() { (void)native_package_from(job.folder, wrong); },
                        "wrong component package identity");
            }
        }
        rejects([&]() { (void)build_only_arguments(application("boot"), "/tmp/output", "1"); },
                "BOOT parallel build-only invocation");
        auto calendar_install = package_install_command("/tmp/native.deb", true);
        check(std::find(calendar_install.begin(), calendar_install.end(), "--allow-downgrades") != calendar_install.end(),
              "Calendar historical version reset preserved");
        check(established[0].slug() == "filesystem-fat12", "FAT12 identity");
        check(application("system-monitor").repo == "System-Monitor", "system monitor identity");
        check(package_install_command("/tmp/native.deb") ==
                  Strings({"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1",
                           "install", "-y", "--no-remove", "--reinstall", "/tmp/native.deb"}),
              "Native DEB transactions wait for APT locks and prohibit removals");
        const auto boot = application("boot");
        check(!infiltrator_native::queue_candidate(boot.slug()),
              "BOOT alone remains an independent direct installer");


        const std::string tag="v0.1.456", name="boot-0.1.456-native-installer.run";
        const std::string page="<a href=\"/Infiltrator-Projects/Boot/releases/download/"+tag+"/"+name+
            "\">RUN</a><clipboard-copy value=\"sha256:"+std::string(64,'a')+
            "\" aria-label=\"Copy to clipboard digest for "+name+"\"></clipboard-copy>";
        const auto direct=native_asset(boot,tag,page);
        check(direct.sha==std::string(64,'a') && direct.version=="0.1.456" &&
              direct.url=="https://github.com/Infiltrator-Projects/Boot/releases/download/"+tag+"/"+name,
              "direct GitHub release asset and digest");
        rejects([&]() { (void)native_asset(boot,tag,page+page); }, "ambiguous digest");
        rejects([&]() { (void)native_asset(boot,"v0.1.457",page); }, "unrelated release asset");
        rejects([&]() { (void)native_asset(boot,tag,""); }, "missing native installer");
        check(application("defragmenter").resource == "/Programs/defragmenter",
              "defragmenter destination");
        const auto calendar = application("calendar");
        check(calendar.repo == "Calendar" && calendar.package == "calendar" &&
              calendar.resource == "/usr/libexec/calendar-about", "Calendar applet identity");
        rejects([&]() { (void)application("unrecognised"); }, "application");

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
