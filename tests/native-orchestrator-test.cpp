// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real supervisor process creation and reaping in an isolated fixture.
// No root privileges, package-manager operations, downloads or ESP writes.
#define main native_orchestrator_entrypoint
#include "../packaging/native-orchestrator.cpp"
#undef main
#include <sys/stat.h>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    char temporary[] = "/tmp/infiltrator-supervisor.XXXXXX";
    const char *root = mkdtemp(temporary);
    if (!root) return 1;
    const fs::path stage(root);
    try {
        fs::create_directory(stage / "status");
        bool rejected = false;
        try { (void)output_of({"/usr/bin/head", "-c", "2000000", "/dev/zero"}); }
        catch (const std::runtime_error &) { rejected = true; }
        int child_status = 0;
        errno = 0;
        check(rejected && waitpid(-1, &child_status, WNOHANG) == -1 && errno == ECHILD,
              "Oversized provider output must fail and reap its child");
        const auto worker = stage / "install-filesystem-resources";
        auto script = [&](const std::string &body) {
            write_text(worker, "#!/bin/sh\n" + body + "\n");
            if (chmod(worker.c_str(), 0700) != 0)
                throw std::runtime_error("Cannot make fixture worker executable");
        };
        script("test \"$1\" = --catalogue && printf 'filesystem-fat12\\nfilesystem-ntfs\\n'");
        setenv("INFILTRATOROS_SELECTED_FILESYSTEMS", "ntfs", 1);
        const auto inventory = catalogue(stage, "install-filesystem-resources", "inventory.list");
        check(inventory == Strings({"filesystem-fat12", "filesystem-ntfs"}),
              "enumeration must not depend on requested subset");
        check(selection("INFILTRATOROS_SELECTED_FILESYSTEMS", inventory, true) ==
              Strings({"filesystem-ntfs"}), "selection applied once to sealed inventory");
        unsetenv("INFILTRATOROS_SELECTED_FILESYSTEMS");
        script("exit 9");
        launch(stage, "filesystem-resources", "filesystem-fat12");
        check(job_state(stage, "filesystem-fat12") == "failed",
              "A worker exiting before its terminal status must fail");
        const auto boot_worker = stage / "install-boot";
        write_text(boot_worker, "#!/bin/sh\nprintf 'state=complete\\nprogress=100\\nstage=Ready\\ndetail=\\n' > \"$(dirname \"$0\")/status/boot-install.status\"\n");
        if (chmod(boot_worker.c_str(), 0700) != 0)
            throw std::runtime_error("Cannot make BOOT fixture worker executable");
        launch(stage, "boot");
        check(job_state(stage, "boot") == "complete" && job_state(stage, "filesystem-fat12") == "failed",
              "BOOT completes independently of failed filesystem worker");
        script("kill -KILL $$");
        launch(stage, "filesystem-resources", "filesystem-ntfs");
        check(job_state(stage, "filesystem-ntfs") == "failed",
              "A killed worker must not leave the run indefinitely queued");
        script("printf 'state=complete\\nprogress=100\\nstage=Ready\\ndetail=\\n' > \"$(dirname \"$0\")/status/$1-install.status\"");
        launch(stage, "filesystem-resources", "filesystem-fat12");
        check(job_state(stage, "filesystem-fat12") == "complete",
              "A successful worker must retain its verified outcome");
        check(job_state(stage, "filesystem-ntfs") == "failed",
              "Other component statuses are independent");
        script("exit 2");
        launch(stage, "filesystem-resources", "filesystem-fat12", true,
               {"filesystem-fat12", "filesystem-ntfs"});
        check(job_state(stage, "filesystem-fat12") == "failed" &&
              job_state(stage, "filesystem-ntfs") == "failed",
              "A smart queue crash must terminate every unresolved member");
        const Strings app_batch{"system-monitor", "calculator", "calendar", "files",
                                "defragmenter", "filesystem-fat12", "clock-binary"};
        fs::remove(stage / "status/boot-install.status");
        write_text(boot_worker, "#!/bin/sh\nset -eu\n"
            "printf 'state=complete\\nprogress=100\\nstage=Ready\\ndetail=\\n' > \"$(dirname \"$0\")/status/boot-install.status\"\n");
        const auto app_worker = stage / "install-system-monitor";
        write_text(app_worker, "#!/bin/sh\nset -eu\n"
            "[ \"$INFILTRATOROS_SMART_BUILD_QUEUE\" = 1 ]\n"
            "grep -qx state=complete \"$(dirname \"$0\")/status/boot-install.status\"\n"
            "for slug in system-monitor calculator calendar files defragmenter filesystem-fat12 clock-binary; do\n"
            " printf 'state=complete\\nprogress=100\\nstage=Ready\\ndetail=\\n' > \"$(dirname \"$0\")/status/$slug-install.status\"\n"
            "done\n");
        if (chmod(app_worker.c_str(), 0700) != 0)
            throw std::runtime_error("Cannot make application queue leader executable");
        dispatch_components(stage, {"system-monitor", "calculator", "calendar", "files", "defragmenter"},
                            {"clock-binary"}, {"filesystem-fat12"}, app_batch);
        check(job_state(stage, "boot") == "complete", "BOOT clean deployment precedes provider queue");
        for (const auto &slug : app_batch)
            check(job_state(stage, slug) == "complete", "application-led queue member supervised");

        write_text(stage / "status/filesystem-fat12-install.status",
                   "state=complete\nprogress=0\nstage=Fake\ndetail=\n");
        check(job_state(stage, "filesystem-fat12") != "complete",
              "Supervisor rejects contradictory complete status");
        write_text(stage / "status/filesystem-fat12-install.status", "state=complete\n");
        check(job_state(stage, "filesystem-fat12") != "complete",
              "Supervisor rejects truncated complete status");
        fs::remove_all(stage);
        std::cout << "Native worker supervision contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        return 1;
    }
}
