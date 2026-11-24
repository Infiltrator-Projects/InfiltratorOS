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
        const auto worker = stage / "install-filesystem-resources";
        auto script = [&](const std::string &body) {
            write_text(worker, "#!/bin/sh\n" + body + "\n");
            if (chmod(worker.c_str(), 0700) != 0)
                throw std::runtime_error("Cannot make fixture worker executable");
        };
        script("exit 9");
        launch(stage, "filesystem-resources", "filesystem-fat12");
        check(job_state(stage, "filesystem-fat12") == "failed",
              "A worker exiting before its terminal status must fail");
        check(failed_filesystem_prerequisite(stage, {"filesystem-fat12"}) == "filesystem-fat12",
              "A failed filesystem must block BOOT");
        script("kill -KILL $$");
        launch(stage, "filesystem-resources", "filesystem-ntfs");
        check(job_state(stage, "filesystem-ntfs") == "failed",
              "A killed worker must not leave the run indefinitely queued");
        script("printf 'state=complete\\nprogress=100\\nstage=Ready\\ndetail=\\n' > \"$(dirname \"$0\")/status/$1-install.status\"");
        launch(stage, "filesystem-resources", "filesystem-fat12");
        check(job_state(stage, "filesystem-fat12") == "complete",
              "A successful worker must retain its verified outcome");
        check(failed_filesystem_prerequisite(stage, {"filesystem-fat12"}).empty(),
              "Successful selected filesystem permits BOOT");
        check(failed_filesystem_prerequisite(stage, {"filesystem-fat12", "filesystem-ntfs"}) == "filesystem-ntfs",
              "All selected prerequisites must complete");
        script("exit 2");
        launch(stage, "filesystem-resources", "filesystem-fat12", true,
               {"filesystem-fat12", "filesystem-ntfs"});
        check(job_state(stage, "filesystem-fat12") == "failed" &&
              job_state(stage, "filesystem-ntfs") == "failed",
              "A smart queue crash must terminate every unresolved member");
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
