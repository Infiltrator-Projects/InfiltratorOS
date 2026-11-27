// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_RUNTIME_H
#define INFILTRATOROS_NATIVE_WORKER_RUNTIME_H
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace infiltrator_native::worker {
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;
extern const fs::path ROOT;
inline constexpr std::size_t MAX_JSON = 4u * 1024u * 1024u;
inline constexpr std::size_t MAX_ASSET = 512u * 1024u * 1024u;
std::string lowercase(std::string s);
std::string trim(const std::string &s);
std::string environment(const char *name, std::string fallback = "");
std::string read_file(const fs::path &path, std::size_t limit = MAX_JSON);
void save(const fs::path &path, const std::string &text);
Strings lines(const std::string &text);
std::string joined(const Strings &entries);
Strings tabs(const std::string &text);
int execute(const Strings &args, std::string *capture = nullptr, const fs::path &log = {});
std::string output(const Strings &args);
void required(const Strings &args, const fs::path &log = {});
std::string digest(const fs::path &path);
struct TempDir {
    fs::path path;
    TempDir(const fs::path &base, std::string prefix) {
        std::string pattern = (base / (prefix + "XXXXXX")).string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        char *result = mkdtemp(writable.data());
        if (!result) throw std::runtime_error("Cannot create secure native temporary directory");
        path = result;
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    // Cleanup must not throw during exception unwinding and terminate a worker.
    ~TempDir() { std::error_code error; if (!path.empty()) fs::remove_all(path, error); }
};
struct Lock {
    int fd;
    explicit Lock(const fs::path &name) {
        fd = open(name.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (fd < 0) throw std::runtime_error("Cannot open native lock");
        while (flock(fd, LOCK_EX) != 0) {
            if (errno == EINTR) continue;
            close(fd);
            throw std::runtime_error("Cannot acquire native lock");
        }
    }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
    ~Lock() { flock(fd, LOCK_UN); close(fd); }
};

fs::path stage_dir();
struct Status {
    fs::path root, path;
    Status(const fs::path &folder, const std::string &slug)
        : root(folder), path(folder / (slug + "-install.status")) {}
    void write(std::string state, int progress, std::string label, std::string detail) {
        fs::create_directories(root);
        for (char &ch : label) if (ch == '\n' || ch == '\r') ch = ' ';
        for (char &ch : detail) if (ch == '\n' || ch == '\r') ch = ' ';
        TempDir tmp(root, "status-");
        auto file = tmp.path / "status";
        save(file, "state=" + state + "\nprogress=" + std::to_string(progress) +
                   "\nstage=" + label + "\ndetail=" + detail + "\n");
        fs::rename(file, path);
    }
};

bool regular(const fs::path &p);
std::string pkg_query(const std::string &name, const std::string &format);

} // namespace infiltrator_native::worker
#endif
