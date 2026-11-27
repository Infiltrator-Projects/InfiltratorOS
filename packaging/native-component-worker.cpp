// SPDX-License-Identifier: GPL-3.0-or-later
// Independent providers own compilation and filesystem semantics. This worker
// owns admission, verification and package transactions.
#define _DEFAULT_SOURCE 1
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <grp.h>
#include <infiltratr/filesystem_module_v2.h>
#include <iomanip>
#include <iostream>
#include <istream>
#include <iterator>
#include <json-c/json.h>
#include <linux/capability.h>
#include <linux/mount.h>
#include <map>
#include <memory>
#include <openssl/evp.h>
#include <optional>
#include <regex>
#include <sched.h>
#include <set>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "native-build-queue.h"
#include "native-build-resources.h"

namespace infiltrator_native::worker {
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;
constexpr std::size_t MAX_JSON = 4u * 1024u * 1024u;
constexpr std::size_t MAX_ASSET = 512u * 1024u * 1024u;

// Process, file, lock and status primitives

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
std::string environment(const char *name, std::string fallback = "") {
    const char *value = getenv(name);
    return value ? std::string(value) : fallback;
}
std::string read_file(const fs::path &path, size_t limit = MAX_JSON) {
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
int execute(const Strings &args, std::string *capture = nullptr, const fs::path &log = {}) {
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
void required(const Strings &args, const fs::path &log = {}) {
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

struct Status {
    fs::path root, path;
    Status(const fs::path &folder, const std::string &slug)
        : root(folder), path(folder / (slug + "-install.status")) {}
    void write(std::string state, int progress, std::string label, std::string detail) {
        fs::create_directories(root);
        // Match the parser's field bounds so a long diagnostic cannot
        // invalidate an otherwise authoritative terminal record.
        auto sanitize = [](std::string &value, std::size_t maximum) {
            for (char &ch : value) if (ch == '\n' || ch == '\r' || ch == '\0') ch = ' ';
            if (value.size() > maximum) {
                value.resize(maximum - 3);
                value += "...";
            }
        };
        sanitize(label, 255);
        sanitize(detail, 1023);
        TempDir tmp(root, "status-");
        auto file = tmp.path / "status";
        save(file, "state=" + state + "\nprogress=" + std::to_string(progress) +
                   "\nstage=" + label + "\ndetail=" + detail + "\n");
        fs::rename(file, path);
    }
};

// Component catalogue and authenticated release transfer

struct Document {
    json_object *ptr = nullptr;
    explicit Document(json_object *obj) : ptr(obj) {
        if (!ptr) throw std::runtime_error("Invalid JSON");
    }
    Document(Document &&other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
    Document(const Document &) = delete;
    ~Document() { if (ptr) json_object_put(ptr); }
};

struct Entry {
    std::string id, name, repo, package, resource, kind;
    std::string slug() const { return lowercase(repo); }
};

struct Asset {
    std::string version, url, sha;
    size_t size = 0;
};

Document parse(const std::string &data) {
    json_tokener *token = json_tokener_new();
    if (!token) throw std::runtime_error("Cannot allocate JSON parser");
    json_object *result = json_tokener_parse_ex(token, data.data(), static_cast<int>(data.size()));
    auto error = json_tokener_get_error(token);
    json_tokener_free(token);
    if (error != json_tokener_success || !result) {
        if (result) json_object_put(result);
        throw std::runtime_error("Invalid native release JSON");
    }
    return Document(result);
}
json_object *field(json_object *object, const char *name) {
    json_object *value = nullptr;
    if (!object || json_object_get_type(object) != json_type_object ||
        !json_object_object_get_ex(object, name, &value))
        throw std::runtime_error(std::string("Missing JSON field: ") + name);
    return value;
}
std::string str(json_object *object, const char *name) {
    auto *value = field(object, name);
    if (json_object_get_type(value) != json_type_string)
        throw std::runtime_error(std::string("Invalid JSON text: ") + name);
    return json_object_get_string(value);
}
struct Transfer {
    std::string data;
    FILE *file = nullptr;
    size_t length = 0;
    size_t limit = MAX_JSON;
    bool too_large = false;
};
static size_t receive(char *buf, size_t width, size_t count, void *context) {
    auto &sink = *static_cast<Transfer *>(context);
    size_t n = width * count;
    if (n > sink.limit || sink.length > sink.limit - n) {
        sink.too_large = true;
        return 0;
    }
    sink.length += n;
    if (sink.file) return fwrite(buf, 1, n, sink.file);
    sink.data.append(buf, n);
    return n;
}
static void transfer(const std::string &url, Transfer &result,
                     long timeout = 120L) {
    if (url.rfind("https://", 0) != 0) throw std::runtime_error("Unsafe native download URL");
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (result.file) {
            rewind(result.file);
            if (ftruncate(fileno(result.file), 0)) throw std::runtime_error("Cannot reset download");
        } else result.data.clear();
        result.length = 0;
        result.too_large = false;
        CURL *curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Cannot initialise libcurl");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "InfiltratorOS-CXX/1");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        CURLcode code = curl_easy_perform(curl);
        long http = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        curl_easy_cleanup(curl);
        if (result.too_large) throw std::runtime_error("Native download exceeds maximum size");
        if (code == CURLE_OK && http >= 200 && http < 300) {
            if (result.file) fflush(result.file);
            return;
        }
        if (http >= 400 && http < 500 && http != 408 && http != 429)
            throw std::runtime_error("GitHub native download HTTP " + std::to_string(http));
        if (attempt == 3)
            throw std::runtime_error("GitHub native download failed after retries");
        std::this_thread::sleep_for(std::chrono::seconds(1 << attempt));
    }
}
static std::string fetch(const std::string &url, long timeout = 120L) {
    Transfer result;
    transfer(url, result, timeout);
    return result.data;
}
std::vector<Entry> catalogue(const fs::path &path, size_t expected = 0) {
    Document root = parse(read_file(path));
    if (json_object_get_type(root.ptr) != json_type_array ||
        json_object_array_length(root.ptr) == 0 ||
        (expected && json_object_array_length(root.ptr) != expected))
        throw std::runtime_error("Native component catalogue has unexpected size");
    std::set<std::string> ids;
    std::vector<Entry> result;
    for (size_t i = 0; i < json_object_array_length(root.ptr); ++i) {
        auto *item = json_object_array_get_idx(root.ptr, i);
        Entry e{str(item, "id"), str(item, "name"), str(item, "repository"),
                str(item, "package"), str(item, "resource"), ""};
        if (auto *kind = json_object_object_get(item, "kind"))
            e.kind = json_object_get_string(kind);
        if (!std::regex_match(e.repo, std::regex("[A-Za-z][A-Za-z0-9-]+")) ||
            !std::regex_match(e.slug(), std::regex("(?:calendar|clock|filesystem)-[a-z0-9-]+")) ||
            !ids.insert(e.slug()).second || e.package != "infiltrator-" + e.slug() ||
            e.resource.empty() || e.resource[0] != '/')
            throw std::runtime_error("Untrusted native provider catalogue");
        result.push_back(std::move(e));
    }
    return result;
}
static std::set<std::string> selection(const char *env_name) {
    std::set<std::string> wanted;
    std::istringstream in(environment(env_name));
    std::string s;
    while (std::getline(in, s, ','))
        if (!trim(s).empty()) wanted.insert(lowercase(trim(s)));
    return wanted;
}
void list(const std::vector<Entry> &entries, size_t count, const char *selector,
                 bool by_id) {
    auto wanted = selection(selector);
    std::set<std::string> known;
    for (const auto &e : entries) known.insert(by_id ? e.id : e.slug());
    for (const auto &item : wanted)
        if (!known.count(item)) throw std::runtime_error("Unknown requested component: " + item);
    for (size_t i = 0; i < count; ++i)
        if (!getenv(selector) || wanted.count(by_id ? entries[i].id : entries[i].slug()))
            std::cout << entries[i].slug() << "\n";
}
static std::string verified_redirect_tag(const std::string &prefix, long status, const std::string &target) {
    if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)
        return {};
    if (target.rfind(prefix, 0) != 0) return {};
    const auto tag = target.substr(prefix.size());
    if (!std::regex_match(tag, std::regex("v[0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?")))
        return {};
    return tag;
}
static std::string live_latest_tag(const std::string &repository) {
    if (!std::regex_match(repository, std::regex("[A-Za-z][A-Za-z0-9-]+")))
        throw std::runtime_error("Invalid release repository identity");
    const std::string base = "https://github.com/Infiltrator-Projects/" + repository;
    const std::string url = base + "/releases/latest";
    const std::string prefix = base + "/releases/tag/";
    for (int attempt = 0; attempt != 4; ++attempt) {
        CURL *curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Cannot initialise GitHub latest-release check");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "InfiltratorOS-native/1");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        CURLcode result = curl_easy_perform(curl);
        long status = 0;
        char *final_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &final_url);
        const std::string target = final_url ? final_url : "";
        curl_easy_cleanup(curl);
        // The first authenticated GitHub redirect is authoritative; the
        // final release HTML is unnecessary and may be throttled.
        const auto tag = result == CURLE_OK ? verified_redirect_tag(prefix, status, target) : "";
        if (!tag.empty()) return tag;
        if (result == CURLE_OK && status >= 300 && status < 400)
            throw std::runtime_error(repository + ": invalid latest release redirect");
        if (status >= 400 && status < 500 && status != 408 && status != 429)
            throw std::runtime_error(repository + ": live latest release HTTP " +
                                     std::to_string(status));
        if (attempt == 3)
            throw std::runtime_error(repository + ": live latest release could not be verified (HTTP " + std::to_string(status) + ", " + curl_easy_strerror(result) + ")");
        std::this_thread::sleep_for(std::chrono::seconds(1 << attempt));
    }
    throw std::runtime_error(repository + ": live release lookup unavailable");
}

// The repository's latest stable release is the only authority. Read the
// digest published by GitHub for its exact native installer. No shared
// release metadata, version thresholds, or installed-version lookup.
static Asset native_asset(const Entry &entry, const std::string &tag,
                          const std::string &page) {
    if (!std::regex_match(tag, std::regex("v[0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?")))
        throw std::runtime_error("Invalid GitHub release tag");
    const auto version = tag.substr(1);
    const auto prefix = entry.id == "defragmenter" ? entry.repo : entry.slug();
    const std::string suffix = entry.id == "calendar" || entry.id == "defragmenter"
        ? "-local-folder.run" : "-native-installer.run";
    const auto name = prefix + "-" + version + suffix;
    const auto path = "/Infiltrator-Projects/" + entry.repo + "/releases/download/" + tag + "/" + name;
    if (page.find("href=\"" + path + "\"") == std::string::npos &&
        page.find("href=\"https://github.com" + path + "\"") == std::string::npos)
        throw std::runtime_error("Native installer not published for " + entry.repo);
    const std::regex control(R"(<clipboard-copy\b[^>]*>)");
    const std::regex digest_value(R"re(\bvalue="sha256:([a-f0-9]{64})")re");
    const auto label = "aria-label=\"Copy to clipboard digest for " + name + "\"";
    std::string sha;
    for (std::sregex_iterator it(page.begin(),page.end(),control), end; it!=end; ++it) {
        const auto attributes=it->str();
        if (attributes.find(label)==std::string::npos) continue;
        std::smatch match;
        if (!std::regex_search(attributes,match,digest_value) || !sha.empty())
            throw std::runtime_error("Invalid or duplicate installer digest");
        sha=match[1].str();
    }
    if (sha.empty()) throw std::runtime_error("GitHub has not published the native installer digest");
    return {version,"https://github.com"+path,sha,0};
}
static Asset release(const Entry &entry) {
    const auto tag=live_latest_tag(entry.repo);
    const std::string base="https://github.com/Infiltrator-Projects/"+entry.repo;
    return native_asset(entry,tag,fetch(base+"/releases/expanded_assets/"+tag));
}
void download(const Asset &asset,const fs::path &target) {
    FILE *file=fopen(target.c_str(),"wb+");
    if(!file)throw std::runtime_error("Cannot create installer");
    Transfer sink;
    sink.file=file;
    sink.limit=asset.size ? asset.size : MAX_ASSET;
    bool closed=false;
    try {
        transfer(asset.url,sink);
        const int close_status=fclose(file);
        closed=true;
        if(close_status)throw std::runtime_error("Cannot close native installer");
        if(!sink.length || (asset.size && sink.length!=asset.size) || digest(target)!=asset.sha)
            throw std::runtime_error("Installer digest/length mismatch");
        if(chmod(target.c_str(),0700)!=0)
            throw std::runtime_error("Cannot make installer executable");
    } catch(...) {
        if(!closed)fclose(file);
        fs::remove(target);
        throw;
    }
}
Entry application(const std::string &slug) {
    for (const auto &contract : infiltrator_native::application_contracts) {
        if (contract.slug != slug) continue;
        return {slug, std::string(contract.repository), std::string(contract.repository),
                std::string(contract.package), std::string(contract.resource), "application"};
    }
    throw std::runtime_error("Unknown native application");
}
// Common V2 and installed provider verification

bool exports_common_filesystem_api(const fs::path &path) {
    // Inspect the installed ELF without loading executable provider code into
    // the root installer. Consumer-specific capabilities are checked by each
    // consumer, including the separate firmware validator in BOOT.
    std::ifstream module(path, std::ios::binary);
    Elf64_Ehdr header{};
    if (!module.read(reinterpret_cast<char *>(&header), sizeof(header)) ||
        std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_machine != EM_X86_64 || header.e_type != ET_DYN)
        return false;

    std::istringstream symbols(output({"/usr/bin/env", "LC_ALL=C", "/usr/bin/readelf",
                                      "--dyn-syms", "--wide", "--", path.string()}));
    bool found_v2 = false;
    for (std::string line; std::getline(symbols, line);) {
        std::istringstream fields(line);
        std::string number, value, size, type, binding, visibility, section, name;
        if (!(fields >> number >> value >> size >> type >> binding >> visibility >> section >> name))
            continue;
        // Symbol version annotations are not part of the exported API name.
        const auto annotation = name.find('@');
        if (annotation != std::string::npos) name.resize(annotation);
        constexpr std::string_view prefix = "infiltrator_filesystem_";
        if (name.rfind(prefix, 0) == 0 && name.size() >= prefix.size() + 3 &&
            name.compare(name.size() - 3, 3, "_v1") == 0)
            return false;
        if (type == "FUNC" && (binding == "GLOBAL" || binding == "WEAK") &&
            (visibility == "DEFAULT" || visibility == "PROTECTED") &&
            section.find_first_not_of("0123456789") == std::string::npos &&
            section != "0" && name == "infiltrator_filesystem_api_v2")
            found_v2 = true;
    }
    return found_v2;
}
// Never execute provider callbacks in the root orchestration process.
// A re-executed child inspects a sealed module copy after dropping privileges.
int validate_common_child(const fs::path &path, const std::string &id) {
    if (!ifs_fs_format_id_valid(id.c_str())) return 1;
    const int input = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (input < 0) return 1;
    struct stat st{};
    if (fstat(input, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > 256LL * 1024 * 1024) { close(input); return 1; }
    const int module = memfd_create("infiltrator-common-v2", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (module < 0) { close(input); return 1; }
    bool valid = true;
    char buffer[65536];
    off_t copied = 0;
    while (copied < st.st_size && valid) {
        const auto count = read(input, buffer, static_cast<std::size_t>(
            std::min<off_t>(sizeof(buffer), st.st_size - copied)));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { valid = false; break; }
        ssize_t written = 0;
        while (written < count) {
            const auto n = write(module, buffer + written, static_cast<std::size_t>(count - written));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { valid = false; break; }
            written += n;
        }
        copied += count;
    }
    close(input);
    if (!valid || copied != st.st_size || fchmod(module, 0555) ||
        fcntl(module, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) != 0) {
        close(module); return 1;
    }
    if (geteuid() == 0 && (setgroups(0, nullptr) || setgid(65534) || setuid(65534))) {
        close(module); return 1;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) { close(module); return 1; }
    const struct rlimit cpu{5, 5}, memory{256ULL * 1024 * 1024, 256ULL * 1024 * 1024}, core{0, 0};
    if (setrlimit(RLIMIT_CPU, &cpu) || setrlimit(RLIMIT_AS, &memory) || setrlimit(RLIMIT_CORE, &core)) {
        close(module); return 1;
    }
    alarm(10);
    const auto fdpath = "/proc/self/fd/" + std::to_string(module);
    void *library = dlopen(fdpath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) { close(module); return 1; }
    void *symbol = dlsym(library, IFS_FS_COMMON_EXPORT_V2);
    IfsFsModuleApiEntryV2 entry = nullptr;
    static_assert(sizeof(entry) == sizeof(symbol), "Function pointer representation");
    std::memcpy(&entry, &symbol, sizeof(entry));
    const auto *api = entry ? entry() : nullptr;
    const bool accepted = ifs_fs_api_v2_valid(api, id.c_str());
    dlclose(library);
    close(module);
    return accepted ? 0 : 1;
}
bool validate_common_descriptor(const fs::path &path, const std::string &id) {
    if (!exports_common_filesystem_api(path)) return false;
    const auto worker = fs::read_symlink("/proc/self/exe");
    return execute({"/usr/bin/timeout", "--foreground", "--kill-after=2s", "12s", worker.string(),
                    "--validate-common-v2", path.string(), id}) == 0;
}
// Debian package identity and recovery
static bool dpkg_verified(const std::string &package) {
    std::string verification;
    return execute({"/usr/bin/dpkg", "--verify", package}, &verification) == 0 && trim(verification).empty();
}

// Providers own native compilation; the orchestrator owns package transactions.
// APT waits for the actual Debian package-manager locks. Do not delete lock
// files or use a check-then-dpkg sequence which races external package tools.
Strings package_install_command(const fs::path &package, bool calendar = false) {
    Strings command{"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "install",
                    "-y", "--no-remove", "--reinstall"};
    // Preserve Calendar's independent historical version-reset contract.
    if (calendar) command.push_back("--allow-downgrades");
    command.push_back(package.string());
    return command;
}
fs::path native_package_from(const fs::path &folder, const Entry &entry) {
    std::vector<fs::path> packages;
    for (const auto &item : fs::directory_iterator(folder))
        if (item.path().extension() == ".deb") packages.push_back(item.path());
    if (packages.size() != 1 || !regular(packages.front()))
        throw std::runtime_error("Native build produced no unique regular DEB");
    const auto package = packages.front();
    const std::map<std::string, std::string> expected{
        {"Package", entry.package},
        {"Architecture", "amd64"}};
    for (const auto &[name, value] : expected)
        if (output({"/usr/bin/dpkg-deb", "-f", package.string(), name}) != value)
            throw std::runtime_error("Invalid component-built DEB identity");
    return package;
}
// Debian package replacement recovery. Repack the currently installed
// package before touching it, so existing locally compiled versions do not
// depend on generic downloads. This is package-file recovery, not a rollback
// of arbitrary maintainer-script effects or the whole operating system.
struct PreviousPackage {
    fs::path folder, package;
    std::string version, architecture, sha;
};
static std::optional<PreviousPackage> capture_previous_package(const Entry &entry,
                                                               const fs::path &recovery) {
    const std::string format = std::string("$") + "{Status}\t$" + "{Version}\t$" + "{Architecture}";
    const auto identity = tabs(pkg_query(entry.package, format));
    if (identity.empty() || identity[0].rfind("deinstall ", 0) == 0 ||
        identity[0].rfind("purge ", 0) == 0) return std::nullopt;
    if (identity.size() != 3 || identity[0] != "install ok installed")
        throw std::runtime_error("Existing package is not in a recoverable installed state: " + entry.package);
    TempDir snapshot(recovery, "rollback-" + entry.slug() + "-");
    required({"/bin/sh", "-c", "cd -- \"$1\" && exec /usr/bin/dpkg-repack \"$2\"",
              "native-rollback", snapshot.path.string(), entry.package},
             recovery / (entry.slug() + "-rollback-capture.log"));
    std::vector<fs::path> packages;
    for (const auto &item : fs::directory_iterator(snapshot.path))
        if (item.path().extension() == ".deb" && regular(item.path())) packages.push_back(item.path());
    if (packages.size() != 1 ||
        output({"/usr/bin/dpkg-deb", "-f", packages.front().string(), "Package"}) != entry.package ||
        output({"/usr/bin/dpkg-deb", "-f", packages.front().string(), "Version"}) != identity[1] ||
        output({"/usr/bin/dpkg-deb", "-f", packages.front().string(), "Architecture"}) != identity[2] ||
        tabs(pkg_query(entry.package, format)) != identity)
        throw std::runtime_error("Cannot verify previous package recovery archive");
    PreviousPackage previous{snapshot.path, packages.front(), identity[1], identity[2], digest(packages.front())};
    save(snapshot.path / "SHA256", previous.sha + "  " + previous.package.filename().string() + "\n");
    snapshot.path.clear(); // Keep the archive until install or recovery is verified.
    return previous;
}
void replace_deb_with_recovery(const Entry &entry, const fs::path &package,
                                      const fs::path &recovery,
                                      const std::function<void()> &verify) {
    auto previous = capture_previous_package(entry, recovery);
    auto discard = [&] {
        if (previous) {
            std::error_code ec;
            fs::remove_all(previous->folder, ec);
        }
    };
    try {
        required(package_install_command(package, entry.id == "calendar"),
                 recovery / (entry.slug() + "-package.log"));
        verify();
    } catch (const std::exception &error) {
        const std::string cause = error.what();
        if (!previous)
            throw std::runtime_error(cause + "; no previous installed package to restore");
        try {
            if (digest(previous->package) != previous->sha)
                throw std::runtime_error("Previous package archive digest changed");
            required(package_install_command(previous->package, true),
                     recovery / (entry.slug() + "-rollback-restore.log"));
            const auto installed = tabs(pkg_query(entry.package, std::string("$") +
                "{Status}\t$" + "{Version}\t$" + "{Architecture}"));
            if (installed != Strings{"install ok installed", previous->version, previous->architecture})
                throw std::runtime_error("Restored package identity/version differs");
            if (!dpkg_verified(entry.package))
                throw std::runtime_error("Restored package failed dpkg file verification");
        } catch (const std::exception &restore_error) {
            throw std::runtime_error(cause + "; previous package recovery FAILED: " + restore_error.what() +
                                     "; recovery DEB retained at " + previous->package.string());
        }
        discard();
        throw std::runtime_error(cause + "; previous package restored and verified");
    }
    discard();
}

// Build isolation and resource admission

// Isolate trusted component build-only code from accidental host writes.
// A private mount namespace is mandatory: no unrestricted build fallback.
// Read-only mounts protect dpkg, APT, /System, the ESP and all other host
// filesystems; only this job's scratch, private temp and shared-memory areas
// are writable. This is not a sandbox for arbitrary hostile root code.
static void isolate_mount_writes(const fs::path &folder) {
    if (geteuid() != 0 || !folder.is_absolute() || fs::canonical(folder) != folder ||
        folder == "/" || folder == "/tmp" || folder == "/var/tmp" ||
        folder.string().rfind("/tmp/", 0) == 0 || folder.string().rfind("/var/tmp/", 0) == 0)
        throw std::runtime_error("Unsafe native build isolation directory");
    struct stat st{};
    if (lstat(folder.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0 || (st.st_mode & 022))
        throw std::runtime_error("Native build isolation requires root-owned private scratch");
    const auto tmp = folder / "tmp", shm = folder / "shm";
    fs::create_directory(tmp);
    fs::create_directory(shm);
    if (chmod(tmp.c_str(), 01777) || chmod(shm.c_str(), 01777))
        throw std::runtime_error("Cannot establish private temporary directories");
    auto checked = [](int result, const char *operation) {
        if (result != 0) throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
    };
    checked(unshare(CLONE_NEWNS), "Cannot create isolated build mount namespace");
    checked(mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr),
            "Cannot privatise build mount propagation");
    checked(mount(folder.c_str(), folder.c_str(), nullptr, MS_BIND, nullptr), "Cannot bind private scratch");
    checked(mount(tmp.c_str(), "/tmp", nullptr, MS_BIND, nullptr), "Cannot isolate /tmp");
    std::vector<fs::path> writable{folder, "/tmp"};
    if (fs::is_directory("/var/tmp") && !fs::is_symlink("/var/tmp")) {
        checked(mount(tmp.c_str(), "/var/tmp", nullptr, MS_BIND, nullptr), "Cannot isolate /var/tmp");
        writable.emplace_back("/var/tmp");
    }
    if (fs::is_directory("/dev/shm") && !fs::is_symlink("/dev/shm")) {
        checked(mount(shm.c_str(), "/dev/shm", nullptr, MS_BIND, nullptr), "Cannot isolate /dev/shm");
        writable.emplace_back("/dev/shm");
    }
    struct mount_attr attrs{};
    attrs.attr_set = MOUNT_ATTR_RDONLY;
    checked(static_cast<int>(syscall(SYS_mount_setattr, AT_FDCWD, "/", AT_RECURSIVE, &attrs, sizeof(attrs))),
            "Cannot protect host mounts during build");
    attrs.attr_set = 0;
    attrs.attr_clr = MOUNT_ATTR_RDONLY;
    for (const auto &path : writable)
        checked(static_cast<int>(syscall(SYS_mount_setattr, AT_FDCWD, path.c_str(), 0, &attrs, sizeof(attrs))),
                "Cannot restore private scratch write access");
    // No mount capability is needed after the namespace is established.
    checked(prctl(PR_CAPBSET_DROP, CAP_SYS_ADMIN, 0, 0, 0), "Cannot drop build mount capability");
    struct __user_cap_header_struct header{};
    struct __user_cap_data_struct data[2]{};
    header.version = _LINUX_CAPABILITY_VERSION_3;
    checked(static_cast<int>(syscall(SYS_capget, &header, data)), "Cannot read build capabilities");
    constexpr auto bit = 1U << (CAP_SYS_ADMIN % 32);
    data[CAP_SYS_ADMIN / 32].effective &= ~bit;
    data[CAP_SYS_ADMIN / 32].permitted &= ~bit;
    data[CAP_SYS_ADMIN / 32].inheritable &= ~bit;
    checked(static_cast<int>(syscall(SYS_capset, &header, data)), "Cannot restrict build capabilities");
    checked(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0), "Cannot prevent build privilege elevation");
}
int isolated_build_entry(int argc, char **argv) {
    if (argc < 4 || argv[3][0] != '/') return 1;
    isolate_mount_writes(argv[2]);
    execv(argv[3], argv + 3);
    return 1;
}

struct Job {
    Entry entry;
    Asset asset;
    fs::path run, folder, package;
};

Strings build_only_arguments(const Entry &entry, const fs::path &folder,
                                    const std::string &slots) {
    if (entry.kind == "application") {
        if (entry.id == "calendar")
            return {"--build-only", "--output-dir", folder.string()};
        if (!infiltrator_native::queue_application(entry.id))
            throw std::runtime_error("Application has no parallel build contract");
        Strings args{"--system-package-mode"};
        if (entry.id == "system-monitor" || entry.id == "calculator")
            args.insert(args.end(), {"--jobs", slots});
        args.insert(args.end(), {"--build-only", "--output", folder.string()});
        return args;
    }
    Strings args{"--profile", "aggressive"};
    if (entry.kind == "calendar" || entry.kind == "clock")
        args.insert(args.end(), {"--jobs", slots});
    args.insert(args.end(), {"--system-package-mode", "--build-only", "--output", folder.string()});
    return args;
}
static void build(Job &job, const fs::path &logs, const std::vector<int> &cpus) {
    // Fetch only admitted jobs, rather than retaining all RUNs before builds.
    if (!job.asset.url.empty()) {
        Status(logs, job.entry.slug()).write("running", 20, "Downloading native RUN", "SHA-256 release verification");
        download(job.asset, job.run);
    }
    const auto slots = std::to_string(std::max<std::size_t>(1, cpus.size()));
    Strings arguments;
    if (!cpus.empty()) {
        std::string mask;
        for (int cpu : cpus) {
            if (!mask.empty()) mask += ',';
            mask += std::to_string(cpu);
        }
        arguments = {"/usr/bin/taskset", "--cpu-list", mask};
    }
    const auto private_home = job.folder / "home";
    const auto private_tmp = job.folder / "tmp";
    const auto private_runtime = job.folder / "runtime";
    fs::create_directory(private_home);
    fs::create_directory(private_tmp);
    fs::create_directory(private_runtime);
    if (chmod(private_runtime.c_str(), 0700) != 0)
        throw std::runtime_error("Cannot protect private XDG runtime");
    arguments.insert(arguments.end(), {"/usr/bin/env", "-u", "DISPLAY", "-u", "WAYLAND_DISPLAY",
                      "-u", "DBUS_SESSION_BUS_ADDRESS", "-u", "XAUTHORITY", "MAKEFLAGS=-j" + slots,
                      "CMAKE_BUILD_PARALLEL_LEVEL=" + slots,
                      "DEB_BUILD_OPTIONS=parallel=" + slots,
                      "HOME=" + private_home.string(), "TMPDIR=" + private_tmp.string(),
                      "XDG_RUNTIME_DIR=" + private_runtime.string(),
                      "XDG_CACHE_HOME=" + (private_home / ".cache").string(),
                      "XDG_CONFIG_HOME=" + (private_home / ".config").string(),
                      "XDG_DATA_HOME=" + (private_home / ".local/share").string()});
    if (job.entry.id == "calculator") {
        // Common's font helper supports a private staging root. GUI training
        // reads already provisioned system fonts without modifying them.
        const auto font_root = job.folder / "font-root";
        fs::create_directory(font_root);
        arguments.push_back("INFILTRATR_NATIVE_ROOT=" + font_root.string());
        arguments.push_back("INFILTRATR_EXTENDED_FONT_SOURCE_DIR=/System/Resources/Fonts");
    }
    // Released Calendar and Defragmenter scripts query getconf for job counts.
    // Scope the reported count to this build's allocation, not the host.
    // The private directory is root-owned and removed with the run.
    {
        const auto bin = job.folder / "scheduler-bin";
        fs::create_directory(bin);
        const auto shim = bin / "getconf";
        save(shim, "#!/bin/sh\nif [ \"$#\" -eq 1 ] && [ \"$1\" = _NPROCESSORS_ONLN ]; then\n"
                   "  printf '%s\\n' " + slots + "\nelse\n  exec /usr/bin/getconf \"$@\"\nfi\n");
        if (chmod(shim.c_str(), 0700) != 0)
            throw std::runtime_error("Cannot set private build CPU-count helper executable");
        const auto apt_guard = bin / "apt-get";
        save(apt_guard, "#!/bin/sh\necho 'APT is forbidden during native build-only execution' >&2\nexit 125\n");
        if (chmod(apt_guard.c_str(), 0700) != 0)
            throw std::runtime_error("Cannot install private package-manager guard");
        arguments.push_back("PATH=" + bin.string() + ":" +
            environment("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"));
    }
    if (!job.asset.url.empty()) {
        arguments.push_back(fs::read_symlink("/proc/self/exe").string());
        arguments.push_back("--isolated-build");
        arguments.push_back(job.folder.string());
    }
    arguments.push_back(job.run.string());
    const auto options = build_only_arguments(job.entry, job.folder, slots);
    arguments.insert(arguments.end(), options.begin(), options.end());
    if (execute(arguments, nullptr, logs / (job.entry.slug() + "-build.log")) != 0)
        throw std::runtime_error("Native build failed: " + job.entry.slug());
    job.package = native_package_from(job.folder, job.entry);
    if (!job.asset.url.empty()) {
        fs::remove(job.run);
    }
}
void build_packages(std::vector<Job> &jobs, const fs::path &statuses,
                           std::map<std::string, std::string> &failures) {
    using namespace infiltrator_native;
    std::ofstream timings(statuses / "smart-build-timings.log");
    if (!timings) throw std::runtime_error("Cannot open native build timing log");
    timings << "Build admission uses available CPUs and RAM. Selected builds: " << jobs.size() << '\n';
    const auto started = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    };
    struct Active {
        std::size_t job;
        BuildReservation resources;
        std::future<void> result;
    };
    std::vector<Active> active;
    std::size_t next = 0;
    std::optional<std::chrono::steady_clock::time_point> blocked;
    std::uint64_t disk_budget = 0, memory_budget = 0;
    bool have_disk_budget = false;
    while (next < jobs.size() || !active.empty()) {
        for (auto it = active.begin(); it != active.end();) {
            if (it->result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            const auto slug = jobs[it->job].entry.slug();
            try {
                it->result.get();
                timings << elapsed() << "ms finished " << slug << std::endl;
            } catch (const std::exception &error) {
                failures[slug] = error.what();
                Status(statuses, slug).write("failed", 100, "Native build failed", error.what());
                timings << elapsed() << "ms failed " << slug << ": " << error.what() << std::endl;
            }
            it = active.erase(it);
        }
        if (next < jobs.size()) {
            auto resources = build_resources(allowed_build_cpus());
            sample_build_activity(resources, statuses);
            if (!have_disk_budget || active.empty()) {
                disk_budget = resources.disk_mib;
                memory_budget = resources.memory_mib;
                have_disk_budget = true;
            }
            resources.disk_capacity_mib = disk_budget;
            resources.capacity_mib = std::min(resources.capacity_mib, memory_budget);
            std::vector<BuildReservation> reservations;
            for (const auto &job : active) reservations.push_back(job.resources);
            std::optional<BuildReservation> reservation;
            std::size_t candidate = next;
            // A temporarily blocked large job must not strand capacity for
            // another independent build. Only pending entries are reordered.
            for (; candidate < jobs.size(); ++candidate) {
                const auto &entry = jobs[candidate].entry;
                const auto kind = entry.kind == "application" ? BuildKind::files :
                    entry.id == "exfat" ? BuildKind::exfat : BuildKind::module;
                reservation = reserve_build(resources, reservations, kind, jobs.size() - next);
                if (reservation) break;
            }
            if (reservation) {
                if (candidate != next) std::swap(jobs[next], jobs[candidate]);
                const auto &entry = jobs[next].entry;
                blocked.reset();
                const auto job = next++;
                const auto count = std::to_string(active.size() + 1);
                const auto slots = std::max<std::size_t>(1, reservation->cpus.size());
                Status(statuses, entry.slug()).write("running", 35, "Compiling native component",
                    count + " builds active, " + std::to_string(slots) + " CPU slots for this build");
                timings << elapsed() << "ms started " << entry.slug() << "; active=" << count
                        << "; cpu_slots=" << slots << '/' << resources.cpu_budget
                        << "; reserved_mib=" << reservation->memory_mib
                        << "; memory_budget_mib=" << resources.memory_mib
                         << "; disk_budget_mib=" << resources.disk_mib << std::endl;
                auto result = std::async(std::launch::async, [&, job, cpus = reservation->cpus] {
                    build(jobs[job], statuses, cpus);
                });
                active.push_back({job, std::move(*reservation), std::move(result)});
                continue;
            }
        }
        if (active.empty() && next < jobs.size()) {
            const auto now = std::chrono::steady_clock::now();
            if (!blocked) blocked = now;
            Status(statuses, jobs[next].entry.slug()).write("queued", 10,
                "Waiting for build resources", "CPU, memory or temporary storage unavailable");
            if (now - *blocked >= std::chrono::seconds(60)) {
                const auto slug = jobs[next++].entry.slug();
                failures[slug] = "Insufficient build resources after bounded wait";
                Status(statuses, slug).write("failed", 100, "Native build not admitted", failures[slug]);
                blocked.reset();
                continue;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            blocked.reset();
            if (!active.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// Build prerequisite policy

Strings application_build_dependencies(const Entry &e) {
    if (e.id == "system-monitor")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "pkg-config", "libgtk-3-dev"};
    if (e.id == "calculator")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "debhelper", "libgtk-4-dev",
                "pkg-config", "xvfb", "xauth", "tar", "gzip", "ca-certificates", "curl",
                "fontconfig", "python3", "python3-reportlab"};
    if (e.id == "calendar")
        return {"build-essential", "binutils", "dpkg-dev", "debhelper", "desktop-file-utils",
                "gettext", "gir1.2-glib-2.0-dev", "gjs", "libglib2.0-dev", "libgtk-3-dev",
                "libicu-dev", "nodejs", "pkg-config", "xz-utils"};
    if (e.id == "defragmenter")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "python3",
                "gir1.2-gtk-3.0", "libgtk-3-dev", "policykit-1", "ca-certificates",
                "desktop-file-utils", "udisks2", "util-linux", "libsqlite3-dev", "libssl-dev"};
    if (e.id == "files")
        return {"build-essential", "binutils", "dpkg-dev", "cmake", "ninja-build", "pkg-config",
                "xvfb", "xauth", "dbus-daemon", "libgtk-4-dev", "libarchive-dev"};
    throw std::runtime_error("No application build dependency contract");
}
static Strings missing_packages(const Strings &packages) {
    Strings missing;
    for (const auto &pkg : packages)
        if (pkg_query(pkg, std::string("$") + "{Status}") != "install ok installed")
            missing.push_back(pkg);
    return missing;
}
static Strings build_dependency_packages(const Entry &e) {
    if (e.kind == "application") return application_build_dependencies(e);
    Strings packages{"build-essential", "binutils", "dpkg-dev"};
    if (e.kind == "calendar" || e.kind == "clock")
        packages.insert(packages.end(), {"pkg-config", "jq", "ghostscript", "gettext", "git",
                                         "libglib2.0-dev", "libicu-dev"});
    if (e.slug().rfind("filesystem-", 0) == 0)
        packages.insert(packages.end(), {"cmake", "git", "pkg-config", "libssl-dev", "libsqlite3-dev"});
    if (e.id == "exfat")
        packages.insert(packages.end(), {"exfatprogs", "dosfstools"});
    if (e.id == "btrfs") packages.push_back("btrfs-progs");
    if (e.id == "xfs") packages.push_back("xfsprogs");
    if (e.id == "ext2" || e.id == "ext3" || e.id == "ext4")
        packages.push_back("e2fsprogs");
    if (e.id == "fat12" || e.id == "fat16" || e.id == "fat32")
        packages.push_back("dosfstools");
    if (e.id == "minix") packages.push_back("util-linux");
    if (e.id == "ntfs")
        packages.insert(packages.end(), {"autoconf", "automake", "libtool", "libgcrypt20-dev"});
    return packages;
}
static bool can_build(const Entry &e) {
    if (!missing_packages(build_dependency_packages(e)).empty()) return false;
    if (e.kind == "application") {
        if (e.id == "files")
            return execute({"pkg-config", "--exists", "gtk4 >= 4.10", "libarchive"}) == 0;
        if (e.id == "system-monitor")
            return execute({"pkg-config", "--exists", "gtk+-3.0 >= 3.22"}) == 0;
        if (e.id == "calculator")
            return execute({"pkg-config", "--exists", "gtk4"}) == 0;
        if (e.id == "calendar")
            return execute({"/usr/bin/dpkg-checkbuilddeps", "-d",
                "debhelper-compat (= 13), desktop-file-utils, gettext, gir1.2-glib-2.0-dev, gjs, libglib2.0-dev, libgtk-3-dev, libicu-dev, nodejs, pkg-config, xz-utils", "-c", "", "/dev/null"}) == 0;
        return true;
    }
    if ((e.kind == "calendar" || e.kind == "clock") &&
        execute({"pkg-config", "--exists", "glib-2.0", "icu-i18n"}) != 0)
        return false;
    if (e.slug().rfind("filesystem-", 0) == 0 &&
        execute({"/usr/bin/pkg-config", "--exists", "openssl", "sqlite3"}) != 0)
        return false;
    if (e.id == "ntfs") {
        const std::string status = std::string("$") + "{Status}";
        if (pkg_query("libntfs-3g89t64", status) != "install ok installed" &&
            pkg_query("libntfs-3g89", status) != "install ok installed")
            return false;
    }
    return true;
}
// Dependency transactions run before any admitted build can start.
void prepare_build_dependencies(const Entry &entry, Status &status) {
    if (can_build(entry)) return;
    const auto missing = missing_packages(build_dependency_packages(entry));
    if (!missing.empty()) {
        status.write("running", 8, "Preparing build prerequisites",
                     "Serial APT transaction before parallel compilation");
        Strings command{"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1",
            "install", "-y", "--no-remove", "--no-install-recommends"};
        command.insert(command.end(), missing.begin(), missing.end());
        required(command, status.root / (entry.slug() + "-dependencies.log"));
    }
    if (!can_build(entry))
        throw std::runtime_error("Native prerequisites unavailable; refusing unscheduled build");
}

// Unified installation lifecycle

static int run_selected_builds(const std::vector<Entry> &chosen, const fs::path &statuses) {
    std::vector<std::unique_ptr<Lock>> locks;
    std::set<std::string> slugs;
    for (const auto &entry : chosen)
        if (!slugs.insert(entry.slug()).second || !infiltrator_native::queue_candidate(entry.slug()))
            throw std::runtime_error("Invalid or duplicate build selection");
    for (const auto &slug : slugs)
        locks.emplace_back(std::make_unique<Lock>(ROOT / (slug + ".lock")));
    Lock global(ROOT / "native-install.lock");
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    setenv("TMPDIR", statuses.c_str(), 1);
    TempDir scratch(statuses, "smart-build-");
    std::vector<Job> jobs;
    std::set<std::string> completed;
    std::map<std::string, std::string> failures;
    try {
        for (const auto &e : chosen) {
            Status status(statuses, e.slug());
            try {
                status.write("running", 2, "Resolving independent installer", e.repo);
                Asset asset = release(e);
                prepare_build_dependencies(e, status);
                Job job{e, asset, scratch.path / (e.slug() + ".run"), scratch.path / e.slug(), {}};
                status.write("queued", 15, "Native build prepared", "Awaiting resource admission");
                fs::create_directory(job.folder);
                jobs.push_back(job);
            } catch (const std::exception &error) {
                failures[e.slug()] = error.what();
                status.write("failed", 100, "Native preparation failed", error.what());
            }
        }
        build_packages(jobs, statuses, failures);
        for (auto &job : jobs) {
            if (failures.count(job.entry.slug())) continue;
            Status status(statuses, job.entry.slug());
            try {
                status.write("running", 85, "Installing native package", "Serial package transaction");
                // Use the same lock-aware transaction for every queued DEB.
                replace_deb_with_recovery(job.entry, job.package, statuses,
                    [&] {
                        if (pkg_query(job.entry.package, "${Status}") != "install ok installed")
                            throw std::runtime_error("Package manager did not report successful installation");
                    });
                status.write("complete", 100, "Native installation complete", "Verified installed package");
                completed.insert(job.entry.slug());
            } catch (const std::exception &error) {
                failures[job.entry.slug()] = error.what();
                status.write("failed", 100, "Native package installation failed", error.what());
            }
        }
        if (!failures.empty()) {
            std::ostringstream summary;
            summary << failures.size() << " independent native job(s) failed";
            for (const auto &[slug, error] : failures)
                summary << "\n" << slug << ": " << error;
            throw std::runtime_error(summary.str());
        }
    } catch (const std::exception &error) {
        for (const auto &e : chosen)
            if (!completed.count(e.slug()) && !failures.count(e.slug()))
                Status(statuses, e.slug()).write("failed", 100, "Smart queue interrupted", error.what());
        throw;
    }
    return 0;
}

int smart_queue(const std::vector<Entry> &entries) {
    if (geteuid() != 0) throw std::runtime_error("Smart queue requires root");
    fs::path stage = stage_dir();
    if (stage == ROOT) throw std::runtime_error("Smart queue requires an installation stage");
    const auto selection_order = lines(read_file(stage / "selected.list"));
    std::set<std::string> selected(selection_order.begin(), selection_order.end());
    if (selection_order.size() != selected.size()) throw std::runtime_error("Duplicate selected component");
    std::vector<Entry> chosen;
    for (const auto &slug : selection_order)
        if (infiltrator_native::queue_candidate(slug)) {
            auto it = std::find_if(entries.begin(), entries.end(),
                                   [&](const Entry &e) { return e.slug() == slug; });
            if (it == entries.end()) throw std::runtime_error("Unknown smart queue candidate");
            chosen.push_back(*it);
        }
    if (chosen.empty()) throw std::runtime_error("No selected smart build candidates");
    return run_selected_builds(chosen, stage / "status");
}
// BOOT alone is a direct installer: it owns the EFI/NVRAM transaction.
// All ordinary applications and providers, even a one-job invocation,
// enter the same isolated build and Debian recovery pipeline.
static int install_boot(const Entry &entry, const fs::path &statuses) {
    Status status(statuses, entry.slug());
    Lock owner(ROOT / (entry.slug() + ".lock"));
    Lock global(ROOT / "native-install.lock");
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    setenv("TMPDIR", statuses.c_str(), 1);
    try {
        status.write("running", 5, "Checking native BOOT release", entry.repo);
        const auto asset = release(entry);
        required({"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check"});
        TempDir directory(statuses, "native-boot-");
        const auto run = directory.path / "native-installer.run";
        status.write("running", 20, "Downloading verified BOOT RUN", entry.repo);
        download(asset, run);
        status.write("running", 35, "Executing independent BOOT RUN", entry.repo);
        required({run.string()}, statuses / "boot-native.log");
        status.write("complete", 100, "Installed", "Independent BOOT installer completed");
    } catch (const std::exception &error) {
        status.write("failed", 100, "Native BOOT installation failed", error.what());
        throw;
    }
    return 0;
}
int individual(const Entry &entry) {
    if (geteuid() != 0) throw std::runtime_error("Native installation requires root");
    fs::create_directories(ROOT);
    const auto stage = stage_dir();
    const auto statuses = stage == ROOT ? stage : stage / "status";
    fs::create_directories(statuses);
    if (entry.id == "boot" && entry.kind == "application") return install_boot(entry, statuses);
    return run_selected_builds({entry}, statuses);
}

// Command dispatch

int native_component_worker_entrypoint(int argc, char **argv) {
    try {
        if (argc >= 4 && std::string_view(argv[1]) == "--isolated-build")
            return isolated_build_entry(argc, argv);
        if (argc == 4 && std::string_view(argv[1]) == "--validate-common-v2")
            return validate_common_child(argv[2], argv[3]);
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("Cannot initialise native release networking");
        struct CurlLifetime { ~CurlLifetime() { curl_global_cleanup(); } } networking;
        (void)networking;
        const std::string name = fs::path(argv[0]).filename().string();
        const fs::path directory = fs::path(argv[0]).parent_path();
        if (environment("INFILTRATOROS_SMART_BUILD_QUEUE") == "1") {
            const std::string leader = name.rfind("install-", 0) == 0 && argc == 1 ? name.substr(8) :
                (name == "install-filesystem-resources" || name == "install-temporal-resources") &&
                    argc == 2 ? argv[1] : "";
            if (!infiltrator_native::queue_candidate(leader))
                throw std::runtime_error("Invalid smart build launcher");
            auto entries = catalogue(directory / "filesystem-resources.json");
            auto additional = catalogue(directory / "filesystem-additional.json");
            auto temporal = catalogue(directory / "temporal-resources.json");
            entries.insert(entries.end(), additional.begin(), additional.end());
            entries.insert(entries.end(), temporal.begin(), temporal.end());
            for (const auto app : infiltrator_native::queued_applications)
                entries.push_back(application(std::string(app)));
            return smart_queue(entries);
        }
        if (name == "install-temporal-resources") {
            auto entries = catalogue(directory / "temporal-resources.json");
            if (argc == 2 && std::string(argv[1]) == "--catalogue") {
                for (const auto &entry : entries) std::cout << entry.slug() << '\n';
                return 0;
            }
            if (argc == 2 && std::string(argv[1]) == "--list") {
                list(entries, entries.size(), "INFILTRATOROS_SELECTED_TEMPORAL", false);
                return 0;
            }
            if (argc != 2) throw std::runtime_error("Usage: install-temporal-resources SLUG");
            auto found = std::find_if(entries.begin(), entries.end(),
                          [&](const Entry &e) { return e.slug() == argv[1]; });
            if (found == entries.end()) throw std::runtime_error("Unknown signed temporal provider");
            return individual(*found);
        }
        if (name == "install-filesystem-resources") {
            auto established = catalogue(directory / "filesystem-resources.json");
            auto extra = catalogue(directory / "filesystem-additional.json");
            auto all = established;
            all.insert(all.end(), extra.begin(), extra.end());
            if (argc == 2 && (std::string_view(argv[1]) == "--catalogue" ||
                              std::string_view(argv[1]) == "--catalogue-all")) {
                const auto &inventory = std::string_view(argv[1]) == "--catalogue" ? established : all;
                for (const auto &entry : inventory) std::cout << entry.slug() << '\n';
                return 0;
            }
            if (argc == 2 && std::string(argv[1]) == "--list") {
                list(all, established.size(), "INFILTRATOROS_SELECTED_FILESYSTEMS", true);
                return 0;
            }
            if (argc == 2 && std::string(argv[1]) == "--list-all") {
                list(all, all.size(), "INFILTRATOROS_SELECTED_FILESYSTEMS", true);
                return 0;
            }
            if (argc != 2) throw std::runtime_error("Usage: install-filesystem-resources SLUG");
            auto found = std::find_if(all.begin(), all.end(),
                          [&](const Entry &e) { return e.slug() == argv[1]; });
            if (found == all.end()) throw std::runtime_error("Unknown signed filesystem");
            return individual(*found);
        }
        if (name.rfind("install-", 0) == 0)
            return individual(application(name.substr(8)));
        throw std::runtime_error("Unknown native worker");
    } catch (const std::exception &error) {
        std::cerr << "InfiltratorOS native provider: " << error.what() << "\n";
        return 1;
    }
}

} // namespace infiltrator_native::worker

#ifndef INFILTRATOROS_NATIVE_WORKER_TEST
int main(int argc, char **argv) {
    return infiltrator_native::worker::native_component_worker_entrypoint(argc, argv);
}
#endif
