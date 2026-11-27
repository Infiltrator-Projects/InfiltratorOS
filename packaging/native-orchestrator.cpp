// SPDX-License-Identifier: GPL-3.0-or-later
// InfiltratorOS owns orchestration, not independent component builds.
// The native dispatcher validates one run, snapshots the available release
// catalogue, and launches independently owned native installers.
#include <algorithm>
#include <iterator>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "native-build-queue.h"
#include "installer-status.h"

namespace fs = std::filesystem;
using Strings = std::vector<std::string>;
static const fs::path STATUS_ROOT("/Temporary/InfiltratorOS");

static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}
static std::string trim(const std::string &value) {
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
static std::string read_text(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
static void write_text(const fs::path &path, const std::string &value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << value)) throw std::runtime_error("Cannot write " + path.string());
}
static Strings lines(const std::string &content) {
    Strings result;
    std::string line;
    for (char c : content) {
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) result.push_back(line);
            line.clear();
        } else line.push_back(c);
    }
    if (!line.empty()) result.push_back(line);
    return result;
}
static std::string joined(const Strings &items) {
    std::string result;
    for (const auto &s : items) result += s + "\n";
    return result;
}
static int run(const Strings &args, const fs::path &out = {},
               bool reset_selection = false, bool launcher = false,
               bool smart_queue = false) {
    if (args.empty()) throw std::runtime_error("Empty command");
    pid_t child = fork();
    if (child < 0) throw std::runtime_error("fork failed");
    if (child == 0) {
        if (!out.empty()) {
            int fd = open(out.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd < 0) _exit(126);
            if (dup2(fd, STDOUT_FILENO) < 0) _exit(126);
            if (dup2(fd, STDERR_FILENO) < 0) _exit(126);
            close(fd);
        }
        if (reset_selection) {
            unsetenv("INFILTRATOROS_SELECTED_FILESYSTEMS");
            unsetenv("INFILTRATOROS_SELECTED_TEMPORAL");
        }
        if (launcher) {
            setenv("INFILTRATOROS_PROGRESS_MODE", "none", 1);
            unsetenv("INFILTRATOROS_FAT_PAIR");
            unsetenv("INFILTRATOROS_SMART_BUILD_QUEUE");
            unsetenv("INFILTRATOROS_REINSTALL_BOOT");
            if (args.size() > 4 && fs::path(args[4]).filename() == "install-boot")
                setenv("INFILTRATOROS_REINSTALL_BOOT", "1", 1);
            if (smart_queue) setenv("INFILTRATOROS_SMART_BUILD_QUEUE", "1", 1);
        }
        std::vector<char *> argv;
        for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("waitpid failed");
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
static std::string output_of(const Strings &args, bool reset_selection = false) {
    int descriptors[2];
    if (pipe(descriptors) != 0) throw std::runtime_error("pipe failed");
    pid_t child = fork();
    if (child < 0) {
        close(descriptors[0]); close(descriptors[1]);
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(126);
        close(descriptors[1]);
        if (reset_selection) {
            unsetenv("INFILTRATOROS_SELECTED_FILESYSTEMS");
            unsetenv("INFILTRATOROS_SELECTED_TEMPORAL");
        }
        std::vector<char *> argv;
        for (const auto &item : args) argv.push_back(const_cast<char *>(item.c_str()));
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        _exit(127);
    }
    close(descriptors[1]);
    std::string result;
    char buffer[4096];
    const char *error = nullptr;
    for (;;) {
        const ssize_t n = read(descriptors[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { error = "Provider list read failed"; break; }
        if (n == 0) break;
        if (result.size() + static_cast<size_t>(n) > 1024 * 1024) {
            error = "Provider list exceeded 1 MiB"; break;
        }
        try { result.append(buffer, static_cast<size_t>(n)); }
        catch (...) { error = "Cannot retain provider list"; break; }
    }
    close(descriptors[0]);
    if (error) (void)kill(child, SIGKILL);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited != child) throw std::runtime_error("Provider enumeration wait failed");
    if (error) throw std::runtime_error(error);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("Provider enumeration failed: " + args.front());
    return result;
}
static Strings catalogue(const fs::path &stage, const char *worker, const char *manifest) {
    auto entries = lines(output_of({(stage / worker).string(), "--catalogue"}, true));
    if (entries.empty() || std::set<std::string>(entries.begin(), entries.end()).size() != entries.size())
        throw std::runtime_error(std::string(worker) + ": catalogue incomplete");
    write_text(stage / manifest, joined(entries));
    return entries;
}
static Strings selection(const char *variable, const Strings &available, bool strip_fs = false) {
    const char *environment = std::getenv(variable);
    if (!environment) return available;
    std::set<std::string> requested, known;
    std::string input(environment);
    size_t start = 0;
    while (start <= input.size()) {
        size_t end = input.find(',', start);
        std::string token = lower(trim(input.substr(start, end == std::string::npos ? end : end - start)));
        if (!token.empty()) requested.insert(token);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    for (const auto &slug : available)
        known.insert(strip_fs && slug.rfind("filesystem-", 0) == 0 ? slug.substr(11) : slug);
    for (const auto &name : requested)
        if (!known.count(name)) throw std::runtime_error(std::string(variable) + ": unknown component " + name);
    Strings selected;
    for (const auto &slug : available) {
        std::string key = strip_fs && slug.rfind("filesystem-", 0) == 0 ? slug.substr(11) : slug;
        if (requested.count(key)) selected.push_back(slug);
    }
    return selected;
}
// The dispatcher runs outside the Debian transaction and owns each worker
// until waitpid returns. A process exit is authoritative; an orphaned status
// file is not. The smart leader owns every member of its shared build batch.
static std::string job_state(const fs::path &stage, const std::string &slug) {
    InstallerStatus status{};
    const auto path = stage / "status" / (slug + "-install.status");
    if (!installer_status_read(path.c_str(), &status)) return {};
    return status.state;
}
static void failed_job(const fs::path &stage, const std::string &slug,
                       const std::string &reason) {
    const auto target = stage / "status" / (slug + "-install.status");
    const auto temporary = target.string() + ".supervisor";
    write_text(temporary, "state=failed\nprogress=100\nstage=Native worker failed\ndetail=" +
                           reason + "\n");
    fs::rename(temporary, target);
}
static void launch(const fs::path &stage, const std::string &launcher,
                   std::string slug = "", bool smart = false,
                   const Strings &batch = {}) {
    const std::string name = slug.empty() ? launcher : slug;
    Strings arguments{"/usr/bin/timeout", "--signal=TERM", "--kill-after=30s",
                      "86300s", (stage / ("install-" + launcher)).string()};
    if (!slug.empty()) arguments.push_back(slug);
    const Strings affected = smart ? batch : Strings{name};
    for (const auto &job : affected) {
        const auto status = stage / "status" / (job + "-install.status");
        write_text(status, "state=queued\nprogress=0\nstage=Queued for native installation\ndetail=Waiting for supervised worker\n");
    }
    std::cout << "Installing " << name << (smart ? " (parallel build queue)" : "") << std::endl;
    int result = run(arguments, stage / "status" / (name + "-install.log"), false, true, smart);
    bool reported_failure = false;
    for (const auto &job : affected) {
        const auto state = job_state(stage, job);
        if (state == "failed") reported_failure = true;
        else if (state != "complete") {
            failed_job(stage, job, "Worker exited without a terminal status (exit " + std::to_string(result) + ")");
            reported_failure = true;
        }
    }
    // A nonzero group result is expected if one member failed. If every
    // member claimed success but the process failed, fail closed on its leader.
    if (result != 0 && !reported_failure)
        failed_job(stage, name, "Worker exited unsuccessfully (exit " + std::to_string(result) + ")");
}
// BOOT owns the one clean ESP deployment. Run it after release preflight
// and before the component queue, not after any provider could stage an EFI
// module. Ordinary hosted providers remain independently installable even
// when BOOT fails; future firmware publication must require BOOT success.
static void dispatch_components(const fs::path &canonical, const Strings &apps,
                                const Strings &temporal, const Strings &chosen,
                                const Strings &selected) {
    launch(canonical, "boot");
    Strings queue;
    for (const auto &slug : selected)
        if (infiltrator_native::queue_candidate(slug)) queue.push_back(slug);
    const bool smart = !queue.empty();
    auto queued = [smart](const std::string &slug) {
        return smart && infiltrator_native::queue_candidate(slug);
    };
    for (const auto &s : apps)
        if (!queued(s)) launch(canonical, s);
    for (const auto &s : temporal)
        if (!queued(s)) launch(canonical, "temporal-resources", s);
    if (smart) {
        const auto &first = queue.front();
        if (first.rfind("filesystem-", 0) == 0)
            launch(canonical, "filesystem-resources", first, true, queue);
        else if (first.rfind("calendar-", 0) == 0 || first.rfind("clock-", 0) == 0)
            launch(canonical, "temporal-resources", first, true, queue);
        else
            launch(canonical, first, "", true, queue);
    }
    for (const auto &s : chosen)
        if (!queued(s)) launch(canonical, "filesystem-resources", s);
}

static void dispatch(const fs::path &stage) {
    const fs::path canonical = fs::canonical(stage);
    if (canonical.parent_path() != STATUS_ROOT ||
        canonical.filename().string().rfind("native-worker.", 0) != 0 ||
        !fs::is_directory(canonical / "status"))
        throw std::runtime_error("Invalid native installation stage");

    Strings temporal = catalogue(canonical, "install-temporal-resources", "providers.list");
    Strings established = catalogue(canonical, "install-filesystem-resources", "filesystems.list");

    auto full_fs = lines(output_of({(canonical / "install-filesystem-resources").string(), "--catalogue-all"}, true));
    if (full_fs.empty() || std::set<std::string>(full_fs.begin(),full_fs.end()).size()!=full_fs.size() ||
        !std::all_of(established.begin(), established.end(), [&](const auto &slug) {
            return std::find(full_fs.begin(), full_fs.end(), slug) != full_fs.end();
        }))
        throw std::runtime_error("Incomplete full filesystem catalogue");
    write_text(canonical/"filesystems-all.list",joined(full_fs));
    // Select from the sealed package inventory before checking publication.
    Strings application_inventory;
    for (const auto slug : infiltrator_native::queued_applications)
        application_inventory.emplace_back(slug);
    Strings apps = selection("INFILTRATOROS_SELECTED_APPLICATIONS", application_inventory);
    temporal = selection("INFILTRATOROS_SELECTED_TEMPORAL", temporal);
    Strings chosen = selection("INFILTRATOROS_SELECTED_FILESYSTEMS", full_fs, true);
    Strings selected = apps;
    selected.emplace_back("boot");
    selected.insert(selected.end(), temporal.begin(), temporal.end());
    selected.insert(selected.end(), chosen.begin(), chosen.end());
    const fs::path manifest = canonical / ".selected.list.tmp";
    write_text(manifest, joined(selected));
    fs::rename(manifest, canonical / "selected.list");
    const fs::path log = canonical / "native-release-preparation.log";
    int code = run({(canonical / "prepare-native-releases").string(),
                    (canonical / "release-json").string(),
                    (canonical / "providers.list").string(),
                    (canonical / "filesystems.list").string(),
                    (canonical / "filesystems-all.list").string(),
                    (canonical / "selected.list").string()}, log);
    if (code != 0) {
        std::cerr << read_text(log);
        throw std::runtime_error("Native release preparation failed");
    }
    setenv("INFILTRATOROS_RELEASE_DIR", (canonical / "release-json").c_str(), 1);
    const fs::path discovery = canonical / "optional-filesystem-discovery.log";
    code = run({(canonical / "install-filesystem-resources").string(),
                "--discover-additional", (canonical / "release-json").string()}, discovery);
    const fs::path optional_path = canonical / "filesystems-optional.list";
    const fs::path pending_path = canonical / "filesystems-pending.list";
    if (code != 0) {
        std::ofstream out(discovery, std::ios::app);
        out << "Additional filesystem discovery failed; established drivers remain enabled.\n";
        write_text(optional_path, "");
        auto all = lines(output_of({(canonical / "install-filesystem-resources").string(), "--catalogue-all"}, true));
        Strings missing;
        for (const auto &s : all)
            if (std::find(established.begin(), established.end(), s) == established.end() &&
                std::find(chosen.begin(), chosen.end(), s) != chosen.end())
                missing.push_back(s);
        write_text(pending_path, joined(missing));
    }
    if (!fs::is_regular_file(optional_path) || !fs::is_regular_file(pending_path))
        throw std::runtime_error("Missing filesystem discovery manifests");
    Strings optional = lines(read_text(optional_path));
    std::set<std::string> names(optional.begin(), optional.end());
    if (optional.size() > full_fs.size() || names.size() != optional.size())
        throw std::runtime_error("Invalid optional filesystem catalogue");
    for (const auto &item : optional) {
        if (item.rfind("filesystem-", 0) != 0 ||
            std::find(established.begin(), established.end(), item) != established.end())
            throw std::runtime_error("Invalid optional filesystem entry: " + item);
    }
    if (fs::file_size(pending_path)) {
        std::cout << "Some independent filesystem modules await published releases.\n";
        write_text(STATUS_ROOT / "filesystems-pending.list", read_text(pending_path));
    }
    // Missing releases are reported by their own workers. They do not
    // prevent unrelated applications, filesystem modules or BOOT from running.
    // The manifest is now authoritative. The UI can change from release
    // preparation to measured component progress without guessing durations.
    const auto phase = canonical / ".phase.dispatcher";
    write_text(phase, "components\n");
    fs::rename(phase, canonical / "phase");
    setenv("INFILTRATOROS_INSTALL_RUNTIME", canonical.c_str(), 1);
    dispatch_components(canonical, apps, temporal, chosen, selected);
}
int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: orchestrate-native-install STAGE");
        dispatch(argv[1]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "InfiltratorOS dispatcher: " << error.what() << "\n";
        return 1;
    }
}
