// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "runtime.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <openssl/evp.h>
#include <sstream>
#include <signal.h>
#include <sys/wait.h>
namespace infiltrator_native::worker {
const fs::path ROOT("/Temporary/InfiltratorOS");

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
std::string trim(const std::string &s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string environment(const char *name, std::string fallback) {
    const char *value = getenv(name);
    return value ? std::string(value) : fallback;
}
std::string read_file(const fs::path &path, size_t limit) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > limit)
        throw std::runtime_error("Missing or oversized file: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
void save(const fs::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << text))
        throw std::runtime_error("Cannot write " + path.string());
    output.close();
    if (chmod(path.c_str(), 0644) != 0) throw std::runtime_error("Cannot chmod saved file");
}
Strings lines(const std::string &text) {
    std::istringstream source(text);
    Strings result;
    std::string line;
    while (std::getline(source, line))
        if (!trim(line).empty()) result.push_back(trim(line));
    return result;
}
std::string joined(const Strings &entries) {
    std::string text;
    for (auto &item : entries) text += item + "\n";
    return text;
}
Strings tabs(const std::string &text) {
    std::istringstream in(trim(text));
    Strings values;
    std::string part;
    while (std::getline(in, part, '\t')) values.push_back(part);
    return values;
}
int execute(const Strings &args, std::string *capture, const fs::path &log) {
    if (args.empty()) throw std::runtime_error("Empty native command");
    // Prepare argv before forking. Build workers call this concurrently.
    std::vector<char *> argv;
    for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    int pipes[2] = {-1, -1};
    if (capture && pipe2(pipes, O_CLOEXEC)) throw std::runtime_error("Cannot create output pipe");
    pid_t child = fork();
    if (child < 0) {
        if (capture) { close(pipes[0]); close(pipes[1]); }
        throw std::runtime_error("Cannot fork native command");
    }
    if (child == 0) {
        if (capture) {
            close(pipes[0]);
            if (dup2(pipes[1], STDOUT_FILENO) < 0) _exit(126);
            close(pipes[1]);
        } else if (!log.empty()) {
            int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
                _exit(126);
            close(fd);
        }
        if (argv.front()[0] == '/') execv(argv.front(), argv.data());
        else execvp(argv.front(), argv.data());
        _exit(127);
    }
    const char *capture_error = nullptr;
    if (capture) {
        close(pipes[1]);
        char buffer[8192];
        for (;;) {
            const ssize_t n = read(pipes[0], buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) { capture_error = "Cannot read command output"; break; }
            if (n == 0) break;
            if (capture->size() + static_cast<size_t>(n) > MAX_JSON) {
                capture_error = "Command output exceeds 4 MiB"; break;
            }
            try { capture->append(buffer, static_cast<size_t>(n)); }
            catch (...) { capture_error = "Cannot retain command output"; break; }
        }
        close(pipes[0]);
        // Reap the producer even when output is invalid or oversized.
        if (capture_error) (void)kill(child, SIGKILL);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("Cannot wait for native command");
    }
    if (capture_error) throw std::runtime_error(capture_error);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
std::string output(const Strings &args) {
    std::string value;
    return execute(args, &value) == 0 ? trim(value) : "";
}
void required(const Strings &args, const fs::path &log) {
    if (execute(args, nullptr, log) != 0)
        throw std::runtime_error("Independent native command failed: " + args[0]);
}
std::string digest(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read file for SHA-256");
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot initialise SHA-256");
    char data[65536];
    while (stream.read(data, sizeof(data)) || stream.gcount()) {
        if (EVP_DigestUpdate(context, data, static_cast<size_t>(stream.gcount())) != 1) {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("Cannot hash native installer");
        }
    }
    if (stream.bad()) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("I/O error reading file for SHA-256");
    }
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (EVP_DigestFinal_ex(context, bytes, &length) != 1 || length != 32) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("Cannot finish SHA-256");
    }
    EVP_MD_CTX_free(context);
    std::ostringstream out;
    for (unsigned i = 0; i < length; ++i)
        out << std::hex << std::setfill('0') << std::setw(2) << unsigned(bytes[i]);
    return out.str();
}
fs::path stage_dir() {
    const std::string runtime = environment("INFILTRATOROS_INSTALL_RUNTIME");
    if (runtime.empty()) return ROOT;
    const fs::path path = fs::canonical(runtime);
    if (path.parent_path() != ROOT ||
        path.filename().string().rfind("native-worker.", 0) != 0)
        throw std::runtime_error("Invalid native run directory");
    return path;
}
bool regular(const fs::path &p) {
    return fs::is_regular_file(fs::symlink_status(p));
}
std::string pkg_query(const std::string &name, const std::string &format) {
    return output({"/usr/bin/dpkg-query", "-W", "-f=" + format, name});
}

} // namespace infiltrator_native::worker
