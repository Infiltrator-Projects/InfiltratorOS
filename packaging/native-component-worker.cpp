// SPDX-License-Identifier: GPL-3.0-or-later
// Native C++ installation handoff. InfiltratorOS does not own component
// builds: it downloads, verifies, invokes, and reports each provider's RUN.
#define _DEFAULT_SOURCE 1
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <json-c/json.h>
#include <map>
#include <memory>
#include <openssl/evp.h>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <curl/curl.h>
#include "native-build-queue.h"
#include "native-build-resources.h"
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;
static const fs::path ROOT("/Temporary/InfiltratorOS");
static constexpr size_t MAX_JSON = 4u * 1024u * 1024u;
static constexpr size_t MAX_ASSET = 512u * 1024u * 1024u;

struct Document {
    json_object *ptr = nullptr;
    explicit Document(json_object *obj) : ptr(obj) {
        if (!ptr) throw std::runtime_error("Invalid JSON");
    }
    Document(Document &&other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
    Document(const Document &) = delete;
    ~Document() { if (ptr) json_object_put(ptr); }
};
static Document parse(const std::string &data) {
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
static json_object *field(json_object *object, const char *name) {
    json_object *value = nullptr;
    if (!object || json_object_get_type(object) != json_type_object ||
        !json_object_object_get_ex(object, name, &value))
        throw std::runtime_error(std::string("Missing JSON field: ") + name);
    return value;
}
static std::string str(json_object *object, const char *name) {
    auto *value = field(object, name);
    if (json_object_get_type(value) != json_type_string)
        throw std::runtime_error(std::string("Invalid JSON text: ") + name);
    return json_object_get_string(value);
}
static bool boolean(json_object *object, const char *name) {
    auto *value = field(object, name);
    if (json_object_get_type(value) != json_type_boolean)
        throw std::runtime_error(std::string("Invalid JSON boolean: ") + name);
    return json_object_get_boolean(value);
}
static size_t integer(json_object *object, const char *name) {
    auto *value = field(object, name);
    if (json_object_get_type(value) != json_type_int ||
        json_object_get_int64(value) <= 0 ||
        json_object_get_int64(value) > static_cast<int64_t>(MAX_ASSET))
        throw std::runtime_error(std::string("Invalid JSON asset size: ") + name);
    return static_cast<size_t>(json_object_get_int64(value));
}
static std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
static std::string trim(const std::string &s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
static std::string environment(const char *name, std::string fallback = "") {
    const char *value = getenv(name);
    return value ? std::string(value) : fallback;
}
static std::string read_file(const fs::path &path, size_t limit = MAX_JSON) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > limit)
        throw std::runtime_error("Missing or oversized file: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
static void save(const fs::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << text))
        throw std::runtime_error("Cannot write " + path.string());
    output.close();
    if (chmod(path.c_str(), 0644) != 0) throw std::runtime_error("Cannot chmod saved file");
}
static Strings lines(const std::string &text) {
    std::istringstream source(text);
    Strings result;
    std::string line;
    while (std::getline(source, line))
        if (!trim(line).empty()) result.push_back(trim(line));
    return result;
}
static std::string joined(const Strings &entries) {
    std::string text;
    for (auto &item : entries) text += item + "\n";
    return text;
}
static Strings tabs(const std::string &text) {
    std::istringstream in(trim(text));
    Strings values;
    std::string part;
    while (std::getline(in, part, '\t')) values.push_back(part);
    return values;
}
static int execute(const Strings &args, std::string *capture = nullptr, const fs::path &log = {}) {
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
static std::string output(const Strings &args) {
    std::string value;
    return execute(args, &value) == 0 ? trim(value) : "";
}
static void required(const Strings &args, const fs::path &log = {}) {
    if (execute(args, nullptr, log) != 0)
        throw std::runtime_error("Independent native command failed: " + args[0]);
}
static std::string digest(const fs::path &path) {
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
struct Entry {
    std::string id, name, repo, package, resource, kind;
    std::string slug() const { return lowercase(repo); }
};
static std::vector<Entry> catalogue(const fs::path &path, size_t expected = 0) {
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
static void list(const std::vector<Entry> &entries, size_t count, const char *selector,
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
struct Asset {
    std::string version, url, sha;
    size_t size = 0;
    std::string source_revision;
};
static constexpr const char *MINIMUM_BOOT_RELEASE = "0.1.50";
// A published stable release is required; release classification alone does not
// establish qualification for physical UEFI boot.

static Asset release_asset(const Entry &entry, json_object *meta, bool calendar_or_clock,
                           bool filesystem) {
    if (boolean(meta, "draft") || boolean(meta, "prerelease"))
        throw std::runtime_error("Unpublished native component release");
    const std::string tag = str(meta, "tag_name");
    if (!std::regex_match(tag, std::regex("v[0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?")))
        throw std::runtime_error("Invalid native component version");
    std::string source;
    if (calendar_or_clock || filesystem || entry.id == "calendar") {
        source = str(meta, "target_commitish");
        if (!std::regex_match(source, std::regex("[a-f0-9]{40}")))
            throw std::runtime_error("Provider is missing immutable source identity");
    }
    Asset result;
    result.version = tag.substr(1);
    if (entry.id == "boot" &&
        execute({"/usr/bin/dpkg", "--compare-versions", result.version, "ge", MINIMUM_BOOT_RELEASE}) != 0)
        throw std::runtime_error("BOOT native installer release is older than the accepted installation contract");
    // Older Calendar RUNs invoke the Python scanner during a native build.
    if (entry.id == "calendar" &&
        execute({"/usr/bin/dpkg", "--compare-versions", result.version, "ge", "1.0.131"}) != 0)
        throw std::runtime_error("Native Calendar requires version 1.0.131 or newer");
    if (filesystem && entry.id == "fat12") {
        // FAT12 releases up to 0.1.23 shipped a forbidden FUSE host and
        // package dependencies. Refuse them BEFORE downloading or invoking
        // a native installer so no machine can acquire those dependencies
        // through InfiltratorOS component installation.
        int major = 0, minor = 0, patch = 0;
        if (sscanf(tag.c_str(), "v%d.%d.%d", &major, &minor, &patch) != 3 ||
            (major == 0 && (minor < 1 || (minor == 1 && patch < 24))))
            throw std::runtime_error(
                "Refusing FAT12 release containing forbidden FUSE dependencies; "
                "requires FAT12 0.1.24 or newer");
    }
    if (filesystem && entry.id == "ntfs" &&
        (result.version == "1.0.15" || result.version == "1.0.16"))
        throw std::runtime_error(
            "Refusing an NTFS native RUN that installs FUSE; requires NTFS 1.0.17 or newer");
    if (filesystem && entry.id == "exfat") {
        int major = 0, minor = 0, patch = 0;
        if (sscanf(tag.c_str(), "v%d.%d.%d", &major, &minor, &patch) != 3 ||
            (major == 0 && minor == 1 && patch < 3))
            throw std::runtime_error("Refusing known-broken exFAT native RUN");
    }
    const std::string run_prefix = entry.id == "defragmenter" ? entry.repo : entry.slug();
    const std::string run_suffix = entry.id == "calendar" || entry.id == "defragmenter"
        ? "-local-folder.run" : "-native-installer.run";
    const std::string run_name = run_prefix + "-" + result.version + run_suffix;
    const std::string prefix = "https://github.com/Infiltrator-Projects/" +
                               entry.repo + "/releases/download/" + tag + "/";
    auto *assets = field(meta, "assets");
    if (json_object_get_type(assets) != json_type_array)
        throw std::runtime_error("Native release has no asset array");
    std::set<std::string> names;
    for (size_t i = 0; i < json_object_array_length(assets); ++i) {
        json_object *item = json_object_array_get_idx(assets, i);
        std::string name = str(item, "name");
        std::string url = str(item, "browser_download_url");
        std::string sha = str(item, "digest");
        size_t size = integer(item, "size");
        if (!std::regex_match(name, std::regex("[A-Za-z0-9_.+-]+")) ||
            url != prefix + name || !std::regex_match(sha, std::regex("sha256:[a-f0-9]{64}")) ||
            !names.insert(name).second)
            throw std::runtime_error("Untrusted native release asset");
        if (name == run_name) result = {tag.substr(1), url, sha.substr(7), size, source};
    }
    if (result.url.empty()) throw std::runtime_error("Native RUN asset is missing");
    if (calendar_or_clock) {
        // New releases upload only the RUN and generic DEB. Immutable older
        // releases may also carry their historical source ZIP.
        const std::set<std::string> required_names{
            run_name, entry.package + "_" + result.version + "_amd64.deb"};
        auto legacy_names = required_names;
        legacy_names.insert(entry.slug() + "-" + result.version + "-source.zip");
        if (names != required_names && names != legacy_names)
            throw std::runtime_error("Unqualified temporal release assets");
    }
    if (filesystem && !names.count(entry.package + "_" + result.version + "_amd64.deb"))
        throw std::runtime_error("Filesystem native release is missing its generic package");
    // BOOT must publish both its hardware-native source installer and a
    // separately identifiable generic Debian package. A staging-only preview
    // (for example BOOT-<version>-amd64.run) is never a native installer.
    if (entry.id == "boot" &&
        !names.count(entry.package + "_" + result.version + "_amd64.deb"))
        throw std::runtime_error("BOOT release is missing its generic Debian package");
    return result;
}
static Document release(const Entry &entry) {
    const std::string directory=environment("INFILTRATOROS_RELEASE_DIR");
    if(!directory.empty())
        return parse(read_file(fs::path(directory)/(entry.repo+".json")));
    // Standalone worker invocations still use authenticated publisher
    // metadata. No runtime REST request or GitHub account is required.
    Document catalogue=parse(fetch("https://raw.githubusercontent.com/Infiltrator-Projects/Infiltrator-Repository/main/catalogue/native-installers-v4.json"));
    auto *meta=field(field(catalogue.ptr,"releases"),entry.repo.c_str());
    return parse(json_object_to_json_string_ext(meta,JSON_C_TO_STRING_PLAIN));
}
static void download(const Asset &asset,const fs::path &target) {
    FILE *file=fopen(target.c_str(),"wb+");
    if(!file)throw std::runtime_error("Cannot create installer");
    Transfer sink;
    sink.file=file;
    sink.limit=asset.size;
    bool closed=false;
    try {
        transfer(asset.url,sink);
        const int close_status=fclose(file);
        closed=true;
        if(close_status)throw std::runtime_error("Cannot close native installer");
        if(sink.length!=asset.size || digest(target)!=asset.sha)
            throw std::runtime_error("Installer digest/length mismatch");
        if(chmod(target.c_str(),0700)!=0)
            throw std::runtime_error("Cannot make installer executable");
    } catch(...) {
        if(!closed)fclose(file);
        fs::remove(target);
        throw;
    }
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
    ~Lock() { flock(fd, LOCK_UN); close(fd); }
};
static fs::path stage_dir() {
    const std::string runtime = environment("INFILTRATOROS_INSTALL_RUNTIME");
    if (runtime.empty()) return ROOT;
    const fs::path path = fs::canonical(runtime);
    if (path.parent_path() != ROOT ||
        path.filename().string().rfind("native-worker.", 0) != 0)
        throw std::runtime_error("Invalid native run directory");
    return path;
}
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
static bool regular(const fs::path &p) {
    return fs::is_regular_file(fs::symlink_status(p));
}
static std::string pkg_query(const std::string &name, const std::string &format) {
    return output({"/usr/bin/dpkg-query", "-W", "-f=" + format, name});
}
static bool exports_common_filesystem_api(const fs::path &path) {
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
static bool provider_current(const Entry &entry, const Asset &asset) {
    const std::string fmt = std::string("$") + "{Status}\t$" + "{Version}\t$" +
                            "{X-Infiltrator-Native-Profile}\t$" + "{X-Infiltrator-PGO}\t$" +
                            "{X-Infiltrator-Source-Revision}\t$" + "{X-Infiltrator-Resource-SHA256}";
    auto status = tabs(pkg_query(entry.package, fmt));
    if (status.size() != 6 || status[0] != "install ok installed" ||
        status[1] != asset.version + "+native1" || status[2] != "aggressive" ||
        status[3] != "trained-two-pass" || asset.source_revision.empty() ||
        status[4] != asset.source_revision) return false;
    fs::path manual = fs::path("/System/Resources/Documentation") / (entry.repo + ".pdf");
    for (const auto &f : {fs::path(entry.resource), manual}) {
        if (!regular(f) || output({"/usr/bin/dpkg-query", "-S", f.string()}) !=
                            entry.package + ": " + f.string()) return false;
    }
    if (status[5] != digest(entry.resource)) return false;
    std::ifstream pdf(manual, std::ios::binary);
    char magic[5];
    if (!pdf.read(magic, 5) || std::memcmp(magic, "%PDF-", 5) != 0) return false;
    std::string verification;
    return execute({"/usr/bin/dpkg", "--verify", entry.package}, &verification) == 0 &&
           trim(verification).empty() &&
           (entry.slug().rfind("filesystem-", 0) != 0 ||
            exports_common_filesystem_api(entry.resource));
}
static Entry application(const std::string &slug) {
    Entry e;
    e.id = slug;
    e.kind = "application";
    if (slug == "system-monitor") {
        e.repo = "System-Monitor"; e.package = "system-monitor"; e.resource = "/Programs/system-monitor";
    } else if (slug == "calculator") {
        e.repo = "Calculator"; e.package = "calculator"; e.resource = "/Programs/calculator";
    } else if (slug == "calendar") {
        e.repo = "Calendar"; e.package = "calendar"; e.resource = "/usr/libexec/calendar-about";
    } else if (slug == "files") {
        e.repo = "Files"; e.package = "files"; e.resource = "/Programs/files";
    } else if (slug == "defragmenter") {
        e.repo = "Defragmenter"; e.package = "defragmenter"; e.resource = "/Programs/defragmenter";
    } else if (slug == "boot") {
        e.repo = "Boot"; e.package = "infiltrator-boot";
        e.resource = "/boot/efi/EFI/InfiltratorOS/BOOT/BOOTX64.EFI";
    } else throw std::runtime_error("Unknown native application");
    e.name = e.repo;
    return e;
}
static Strings required_paths(const std::string &slug) {
    if (slug == "boot") return {};
    if (slug == "system-monitor") return {"/usr/share/doc/system-monitor/BUILD-INFO"};
    if (slug == "calculator")
        return {"/System/Resources/Documentation/Calculator.pdf",
                "/usr/share/applications/net.ssmith.calculator.desktop",
                "/usr/share/icons/hicolor/scalable/apps/calculator.svg",
                "/usr/share/metainfo/calculator.metainfo.xml"};
    if (slug == "calendar")
        return {"/usr/share/cinnamon/applets/calendar@the-infiltratr/metadata.json",
                "/usr/lib/x86_64-linux-gnu/libcalendar.so.0.0.0",
                "/usr/lib/x86_64-linux-gnu/girepository-1.0/Calendar-1.0.typelib",
                "/System/Resources/Documentation/Calendar.pdf"};
    if (slug == "files") return {"/usr/share/applications/files.desktop"};
    return {"/Programs/defragmenter-test-media",
            "/System/Commands/Defragmenter/defragmenter-operation-engine",
            "/System/Commands/Defragmenter/defragmenter-mapper",
            "/System/Resources/Documentation/Defragmenter.pdf"};
}
static bool application_proof(const Entry &entry) {
    // The published Calculator RUN validates its own PGO training.
    // Require a package-owned manual as well as the executable and OS
    // attestation; independent package-owned PGO evidence is still needed.
    if (entry.id == "calculator") {
        const auto manual = "/System/Resources/Documentation/Calculator.pdf";
        return regular(manual) &&
               output({"/usr/bin/dpkg-query", "-S", manual}) ==
                   std::string("calculator: ") + manual;
    }
    if (entry.id == "calendar") {
        const fs::path proof = "/usr/share/doc/calendar/BUILD-INFO";
        if (!regular(proof)) return false;
        const auto data = read_file(proof);
        return data.find("Build mode: local hardware-native PGO-trained ") != std::string::npos &&
               data.find("-march=native") != std::string::npos &&
               data.find("-fprofile-use") != std::string::npos;
    }
    if (entry.id == "defragmenter") {
        auto fields = tabs(pkg_query("defragmenter", std::string("$") +
            "{Status}\t$" + "{Version}\t$" + "{X-Defragmenter-Build}"));
        return fields.size() == 3 && fields[2] == "native";
    }
    fs::path proof = entry.id == "system-monitor" ?
        "/usr/share/doc/system-monitor/BUILD-INFO" :
        "/System/Resources/Documentation/Files-BUILD-INFO";
    if (entry.id == "files" && !regular(proof))
        proof = "/System/Resources/Documentation/Files/BUILD-INFO";
    if (!regular(proof)) return false;
    auto data = read_file(proof);
    return data.find("Profile: aggressive") != std::string::npos &&
           data.find("PGO: trained two-pass profile") != std::string::npos &&
           data.find("-march=native") != std::string::npos;
}
// Query only mandatory Debian fields. dpkg-query returns an empty custom field
// for Calculator/System Monitor/Files, and the existing tabs()/trim() helper
// legitimately drops its trailing separator. Demanding three fields caused a
// fully installed application to be falsely marked as failed.
static bool application_package_matches(const Entry &entry, const Asset &asset,
                                        const std::string &package_record) {
    const auto fields = tabs(package_record);
    if (fields.size() != 2 || fields[0] != "install ok installed") return false;
    if (entry.id == "calendar") {
        const std::string prefix = asset.version + "+nativepgo";
        if (fields[1].rfind(prefix, 0) != 0) return false;
        const auto revision = fields[1].substr(prefix.size());
        return !revision.empty() && revision[0] != '0' &&
               revision.find_first_not_of("0123456789") == std::string::npos;
    }
    const std::string suffix =
        entry.id == "files" || entry.id == "defragmenter" ? "+native1" : "";
    return fields[1] == asset.version + suffix;
}
// The independent BOOT RUN owns all ESP writes and NVRAM changes.
// InfiltratorOS only verifies its fixed destination and selected boot option.
static fs::path installed_boot_image() {
    fs::path found;
    for (const char *mount : {"/boot/efi", "/efi", "/boot"}) {
        if (output({"/usr/bin/findmnt", "-rnM", mount, "-t", "vfat", "-o", "FSTYPE"}) != "vfat")
            continue;
        const fs::path directory = fs::path(mount) / "EFI/InfiltratorOS/BOOT";
        if (fs::is_symlink(directory) || fs::is_symlink(directory.parent_path()) ||
            fs::is_symlink(directory.parent_path().parent_path()))
            continue;
        const auto image = directory / "BOOTX64.EFI";
        if (!regular(image) || !regular(directory / "boot.conf")) continue;
        if (!found.empty()) return {}; // Never accept two plausible BOOT installations.
        found = image;
    }
    return found;
}
// Firmware must launch trusted Shim when Secure Boot is enabled. BOOT's
// signed second stage is still BOOTX64.EFI in the fixed directory.
// Match the firmware device path to the mounted ESP, not just its filename.
static bool firmware_partition_matches(const std::string &line, const std::string &partuuid) {
    if (partuuid.empty()) return false;
    const auto lower = lowercase(line), expected = lowercase(partuuid);
    const auto start = lower.find("hd(");
    if (start == std::string::npos) return false;
    const auto end = lower.find(')', start);
    if (end == std::string::npos) return false;
    std::istringstream input(lower.substr(start + 3, end - start - 3));
    std::string partition, scheme, signature, offset, size, extra;
    if (!std::getline(input, partition, ',') || !std::getline(input, scheme, ',') ||
        !std::getline(input, signature, ',') || !std::getline(input, offset, ',') ||
        !std::getline(input, size, ',') || std::getline(input, extra, ',')) return false;
    if (partition.empty() || partition.find_first_not_of("0123456789") != std::string::npos)
        return false;
    if (scheme == "gpt")
        return std::regex_match(expected, std::regex("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) &&
               signature == expected;
    if (scheme != "mbr" || !std::regex_match(expected, std::regex("[0-9a-f]{8}-[0-9a-f]{2}")) ||
        !std::regex_match(signature, std::regex("0x[0-9a-f]{1,8}"))) return false;
    try {
        return std::stoul(partition) == std::stoul(expected.substr(9), nullptr, 16) &&
            std::stoul(signature, nullptr, 16) == std::stoul(expected.substr(0, 8), nullptr, 16);
    } catch (const std::exception &) { return false; }
}
static std::string esp_partition_identity(const fs::path &image) {
    const auto mount = image.parent_path().parent_path().parent_path().parent_path();
    const auto source = output({"/usr/bin/findmnt", "-rnM", mount.string(), "-t", "vfat", "-o", "SOURCE"});
    if (source.rfind("/dev/", 0) != 0 || source.find('\n') != std::string::npos)
        return {};
    return output({"/usr/bin/lsblk", "--noheadings", "--nodeps", "--output", "PARTUUID", "--", source});
}
static bool boot_registered(const std::string &firmware, bool allow_shim = false,
                            const std::string &partuuid = {}) {
    std::string first, matching;
    unsigned int count = 0;
    for (const auto &line : lines(firmware)) {
        const auto lower = lowercase(line);
        if (lower.rfind("bootorder:", 0) == 0) {
            const auto order = trim(lower.substr(10));
            first = order.substr(0, order.find(','));
        }
        const bool direct = lower.find("file(\\efi\\infiltratoros\\boot\\bootx64.efi)") != std::string::npos;
        const bool shim = allow_shim &&
            (lower.find("file(\\efi\\infiltratoros\\boot\\shimx64.efi)") != std::string::npos ||
             lower.find("file(\\efi\\infiltratoros\\boot\\shimx64.efi.signed)") != std::string::npos);
        if (lower.size() < 9 || lower.rfind("boot", 0) != 0 ||
            (!direct && !shim))
            continue;
        if (line[8] != '*' || (!partuuid.empty() && !firmware_partition_matches(line, partuuid)))
            return false;
        const auto candidate = lower.substr(4, 4);
        if (candidate.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
        matching = candidate;
        ++count;
    }
    return count == 1 && first == matching;
}
// Read the UEFI SecureBoot variable, whose first four bytes are efivarfs
// attributes, followed by the 0/1 data byte. Unknown values stay restrictive.
// efivarfs is a non-seekable firmware interface, not an ordinary disk file.
// Consume the four-byte attributes sequentially before reading the flag.
static bool secure_boot_flag(std::istream &stream, bool attributes) {
    if (attributes) stream.ignore(4);
    char flag = 0;
    return bool(stream.get(flag)) && flag == 1;
}
static int secure_boot_state() {
    const fs::path modern =
        "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c";
    const fs::path legacy =
        "/sys/firmware/efi/vars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c/data";
    const fs::path path = fs::exists(modern) ? modern : legacy;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return -1;
    if (secure_boot_flag(stream, path == modern)) return 1;
    // Reopen the nonseekable variable to distinguish disabled from invalid.
    std::ifstream disabled(path, std::ios::binary);
    if (path == modern) disabled.ignore(4);
    char flag = 1;
    return disabled.get(flag) && flag == 0 ? 0 : -1;
}
// UEFI utilities are installed under /usr/bin on modern Debian/Ubuntu but
// under /usr/sbin on some other systems. Resolve only standard absolute
// locations: never misreport a missing executable as a missing firmware entry.
static fs::path efibootmgr_executable(const std::vector<fs::path> &candidates = {
    "/usr/bin/efibootmgr", "/usr/sbin/efibootmgr",
    "/bin/efibootmgr", "/sbin/efibootmgr"}) {
    for (const auto &candidate : candidates)
        if (access(candidate.c_str(), X_OK) == 0 && fs::is_regular_file(candidate))
            return candidate;
    return {};
}
// Verify and attest the EFI executable set separately from editable boot.conf,
// logs and firmware variables. This is file-deployment evidence, not a claim
// that UEFI can mount every filesystem or that a future V2 bridge is complete.
static std::string boot_efi_inventory(const fs::path &image) {
    std::map<std::string, std::string> hashes;
    const auto directory = image.parent_path();
    auto add = [&](const fs::path &file, bool executable) {
        if (!regular(file) || fs::file_size(file) == 0)
            throw std::runtime_error("Missing or unsafe BOOT EFI file: " + file.string());
        if (executable) {
            std::ifstream input(file, std::ios::binary);
            char magic[2]{};
            if (!input.read(magic, 2) || magic[0] != 'M' || magic[1] != 'Z')
                throw std::runtime_error("Invalid BOOT EFI executable header: " + file.string());
        }
        hashes.emplace(file.lexically_relative(directory).generic_string(), digest(file));
    };
    add(image, true);
    for (const auto &file : fs::directory_iterator(directory)) {
        const auto name = lowercase(file.path().filename().string());
        if (file.path() != image &&
            (lowercase(file.path().extension().string()) == ".efi" || name == "shimx64.efi.signed"))
            add(file.path(), true);
    }
    const auto drivers = directory / "drivers_x64";
    if (fs::is_symlink(drivers) || !fs::is_directory(drivers))
        throw std::runtime_error("BOOT EFI driver directory is missing or unsafe");
    for (const auto &file : fs::directory_iterator(drivers)) {
        const auto extension = lowercase(file.path().extension().string());
        if (extension == ".efi" || extension == ".so") add(file.path(), extension == ".efi");
    }
    // The present BOOT native RUN builds and requests all six EFI drivers.
    // This contract must be revised in tandem with BOOT's future V2 transition.
    for (const auto *name : {"ext2", "ext4", "reiserfs", "iso9660", "hfs", "btrfs"})
        if (!hashes.count(std::string("drivers_x64/") + name + "_x64.efi"))
            throw std::runtime_error(std::string("BOOT EFI driver missing: ") + name);
    std::string inventory;
    for (const auto &[name, hash] : hashes) inventory += hash + "  " + name + "\n";
    return inventory;
}
static bool efi_signature_present(const fs::path &image) {
    if (!regular(image)) return false;
    std::string report;
    if (execute({"/usr/bin/sbverify", "--list", image.string()}, &report) != 0) return false;
    const auto lower = lowercase(report);
    return lower.find("signature 1") != std::string::npos &&
           lower.find("no signature table") == std::string::npos;
}

static bool native_boot_current(const Asset &asset, bool evidence, std::string *reason) {
    auto fail = [&](const char *message) {
        if (reason) *reason = message;
        return false;
    };
    const auto image = installed_boot_image();
    if (image.empty()) return fail("BOOTX64.EFI or boot.conf is missing from the fixed ESP path");
    const auto manager = efibootmgr_executable();
    if (manager.empty())
        return fail("Cannot verify BOOT firmware: efibootmgr is missing from standard system paths");
    std::string firmware;
    const int rc = execute({manager.string(), "-v"}, &firmware);
    if (rc != 0)
        return fail("Cannot verify BOOT firmware: efibootmgr -v failed");
    const int secure = secure_boot_state();
    if (secure < 0) return fail("Cannot establish UEFI Secure Boot state");
    bool allow_shim = false;
    if (secure == 1) {
        for (const char *name : {"shimx64.efi", "shimx64.efi.signed"})
            if (regular(image.parent_path() / name)) allow_shim = true;
    }
    const auto esp = esp_partition_identity(image);
    if (esp.empty()) return fail("Cannot establish the mounted ESP partition identity");
    if (!boot_registered(firmware, allow_shim, esp))
        return fail("BOOT's fixed EFI entry is absent, duplicated, inactive, not first, or points at another ESP");
    if (secure == 1) {
        // Presence of a parseable signature is not proof of firmware DB,
        // DBX or MOK trust. Never modify firmware or assert a boot test.
        if (!efi_signature_present(image))
            return fail("Secure Boot BOOTX64.EFI signature cannot be established");
        const auto lower_firmware = lowercase(firmware);
        for (const char *name : {"shimx64.efi", "shimx64.efi.signed"}) {
            const std::string path = "file(\\efi\\infiltratoros\\boot\\" + std::string(name) + ")";
            if (lower_firmware.find(path) != std::string::npos &&
                !efi_signature_present(image.parent_path() / name))
                return fail("Secure Boot Shim signature cannot be established");
        }
    }
    std::string inventory;
    try { inventory = boot_efi_inventory(image); }
    catch (const std::exception &error) { if (reason) *reason = error.what(); return false; }
    if (!evidence) return true;
    const auto attestation = ROOT / "native-verified-boot.json";
    if (!regular(attestation)) return fail("BOOT native installation attestation is absent");
    try {
        Document proof = parse(read_file(attestation));
        if (str(proof.ptr, "version") != asset.version ||
            str(proof.ptr, "installer_sha256") != asset.sha ||
            str(proof.ptr, "program_sha256") != digest(image) ||
            str(proof.ptr, "efi_inventory") != inventory)
            return fail("BOOT release attestation differs from its installed EFI image");
    } catch (...) {
        return fail("BOOT native installation attestation cannot be verified");
    }
    return true;
}
static bool application_current(const Entry &entry, const Asset &asset,
                                bool evidence, std::string *reason = nullptr) {
    auto fail = [&](const std::string &detail) {
        if (reason) *reason = detail;
        return false;
    };
    if (entry.id == "boot") return native_boot_current(asset, evidence, reason);
    const std::string identity = pkg_query(entry.package, std::string("$") +
        "{Status}\t$" + "{Version}");
    if (!application_package_matches(entry, asset, identity))
        return fail("Installed Debian package identity/version differs from verified release");
    if (!regular(entry.resource) || (entry.id != "boot" && access(entry.resource.c_str(), X_OK) != 0))
        return fail("Installed application executable is absent or not executable: " +
                    entry.resource);
    if (output({"/usr/bin/dpkg-query", "-S", entry.resource}) !=
        entry.package + ": " + entry.resource)
        return fail("Installed executable is not owned by the expected Debian package");
    if (!application_proof(entry))
        return fail("Independent native optimisation evidence is missing or invalid");
    for (const auto &p : required_paths(entry.id)) {
        if (!regular(p)) return fail("Expected application resource is missing: " + p);
        if (entry.id == "calendar" && output({"/usr/bin/dpkg-query", "-S", p}) !=
                                      entry.package + ": " + p)
            return fail("Expected Calendar resource is not owned by its package: " + p);
    }
    if (evidence) {
        fs::path file = ROOT / ("native-verified-" + entry.id + ".json");
        if (!regular(file)) return fail("InfiltratorOS native attestation is absent");
        try {
            Document proof = parse(read_file(file));
            if (str(proof.ptr, "version") != asset.version ||
                str(proof.ptr, "installer_sha256") != asset.sha ||
                str(proof.ptr, "program_sha256") != digest(entry.resource))
                return fail("Native attestation does not match the installed executable");
        } catch (...) { return fail("Native attestation cannot be verified"); }
    }
    std::string verification;
    if (execute({"/usr/bin/dpkg", "--verify", entry.package},
                &verification) != 0 ||
        !trim(verification).empty())
        return fail("dpkg verification rejected an installed application file");
    return true;
}
static void verify_install(const Entry &entry, const Asset &asset, bool application_mode) {
    if (!application_mode) {
        if (!provider_current(entry, asset))
            throw std::runtime_error("Independent provider did not install its verified native SO and PDF"
                                     " with the required module export");
        return;
    }
    std::string verification_failure;
    if (!application_current(entry, asset, false, &verification_failure))
        throw std::runtime_error("Independent application installation contract failed: " +
                                 verification_failure);
    json_object *proof = json_object_new_object();
    json_object_object_add(proof, "version", json_object_new_string(asset.version.c_str()));
    json_object_object_add(proof, "installer_sha256", json_object_new_string(asset.sha.c_str()));
    std::string value = digest(entry.id == "boot" ? installed_boot_image() : fs::path(entry.resource));
    json_object_object_add(proof, "program_sha256", json_object_new_string(value.c_str()));
    if (entry.id == "boot")
        json_object_object_add(proof, "efi_inventory",
                               json_object_new_string(boot_efi_inventory(installed_boot_image()).c_str()));
    std::string content = json_object_to_json_string_ext(proof, JSON_C_TO_STRING_PLAIN);
    json_object_put(proof);
    TempDir staging(ROOT, "verified-native-");
    fs::path record = staging.path / "attestation";
    save(record, content);
    fs::rename(record, ROOT / ("native-verified-" + entry.id + ".json"));
}
// Only the independent provider owns its RUN arguments. BOOT deliberately
// receives none: it always builds and installs to its sole EFI destination.
static Strings native_installer_command(const Entry &entry, bool temporal,
                                        bool filesystem, const fs::path &run) {
    Strings command{run.string()};
    if (temporal || filesystem)
        command.insert(command.end(), {"--profile", "aggressive", "--system-package-mode"});
    else if (entry.id != "defragmenter" && entry.id != "calendar" && entry.id != "boot")
        command.push_back("--system-package-mode");
    return command;
}
// Providers own native compilation; the orchestrator owns package transactions.
// APT waits for the actual Debian package-manager locks. Do not delete lock
// files or use a check-then-dpkg sequence which races external package tools.
static Strings package_install_command(const fs::path &package, bool calendar = false) {
    Strings command{"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "install",
                    "-y", "--no-remove", "--reinstall"};
    // Preserve Calendar's independent historical version-reset contract.
    if (calendar) command.push_back("--allow-downgrades");
    command.push_back(package.string());
    return command;
}
static fs::path native_package_from(const fs::path &folder, const Entry &entry,
                                    const Asset &asset) {
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
    const auto version = output({"/usr/bin/dpkg-deb", "-f", package.string(), "Version"});
    if (entry.kind == "application"
            ? !application_package_matches(entry, asset, "install ok installed\t" + version)
            : version != asset.version + "+native1")
        throw std::runtime_error("Invalid component-built DEB version");
    return package;
}
// Validate the package before changing the installed system. The independent
// release owns compilation, while the OS owns the package transaction.
static fs::path staged_resource(const fs::path &root, const std::string &absolute) {
    const auto path = root / fs::path(absolute).relative_path();
    if (!regular(path)) throw std::runtime_error("Native DEB payload missing: " + absolute);
    const auto relative = fs::canonical(path).lexically_relative(fs::canonical(root));
    if (relative.empty() || *relative.begin() == "..")
        throw std::runtime_error("Native DEB resource escapes staging: " + absolute);
    return path;
}
static void verify_native_payload(const fs::path &package, const Entry &entry, const Asset &asset,
                                  const fs::path &scratch) {
    TempDir inspection(scratch, "verify-payload-");
    required({"/usr/bin/dpkg-deb", "-x", package.string(), inspection.path.string()});
    const auto resource = staged_resource(inspection.path, entry.resource);
    if (entry.kind == "application") {
        if (access(resource.c_str(), X_OK) != 0)
            throw std::runtime_error("Staged native application is not executable");
        for (const auto &path : required_paths(entry.id))
            (void)staged_resource(inspection.path, path);
        return;
    }
    const std::map<std::string, std::string> expected{
        {"X-Infiltrator-Native-Profile", "aggressive"},
        {"X-Infiltrator-PGO", "trained-two-pass"},
        {"X-Infiltrator-Source-Revision", asset.source_revision},
        {"X-Infiltrator-Resource-SHA256", digest(resource)}};
    for (const auto &[field_name, value] : expected)
        if (value.empty() || output({"/usr/bin/dpkg-deb", "-f", package.string(), field_name}) != value)
            throw std::runtime_error("Unqualified staged provider metadata: " + field_name);
    const auto manual = staged_resource(inspection.path,
        "/System/Resources/Documentation/" + entry.repo + ".pdf");
    std::ifstream pdf(manual, std::ios::binary);
    char magic[5];
    if (!pdf.read(magic, 5) || std::memcmp(magic, "%PDF-", 5) != 0)
        throw std::runtime_error("Staged provider manual is not a PDF");
    if (entry.slug().rfind("filesystem-", 0) == 0 && !exports_common_filesystem_api(resource))
        throw std::runtime_error("Staged provider does not export the canonical Common V2 API");
}
static bool install(const Entry &entry, Status &status, bool application_mode,
                    bool temporal, bool filesystem) {
    Document meta = release(entry);
    Asset asset = release_asset(entry, meta.ptr, temporal, filesystem);
    if (application_mode ? application_current(entry, asset, true) : provider_current(entry, asset))
        return true;
    if (application_mode)
        required({"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check"});
    TempDir directory(status.root, "native-provider-");
    fs::path run = directory.path / "native-installer.run";
    status.write("running", 20, "Downloading verified RUN", entry.repo);
    download(asset, run);
    status.write("running", 35, "Executing independent RUN", entry.repo);
    if (filesystem || temporal) {
        if (temporal) {
            // The temporal build-only contract refuses to install prerequisites.
            // Resolve the shared calendar/clock toolchain before invoking it.
            Strings dependencies{"build-essential", "binutils", "pkg-config", "jq",
                                 "ghostscript", "gettext", "libglib2.0-dev", "libicu-dev", "git"};
            Strings missing;
            for (const auto &dep : dependencies)
                if (pkg_query(dep, std::string("$") + "{Status}") != "install ok installed")
                    missing.push_back(dep);
            if (!missing.empty()) {
                Strings command{"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1",
                                "install", "-y", "--no-remove", "--no-install-recommends"};
                command.insert(command.end(), missing.begin(), missing.end());
                required(command, status.root / (entry.slug() + "-dependencies.log"));
            }
        }
        // Build the provider's native DEB once, then retain it while APT waits.
        // This also covers providers outside the parallel compilation queue.
        const auto folder = directory.path / "package";
        fs::create_directory(folder);
        auto command = native_installer_command(entry, temporal, filesystem, run);
        command.insert(command.end(), {"--build-only", "--output", folder.string()});
        required(command);
        const auto package = native_package_from(folder, entry, asset);
        verify_native_payload(package, entry, asset, folder);
        status.write("running", 85, "Installing native package", "Waiting for Debian package manager");
        required(package_install_command(package), status.root / (entry.slug() + "-package.log"));
    } else {
        required(native_installer_command(entry, temporal, filesystem, run));
    }
    verify_install(entry, asset, application_mode);
    return false;
}
static int individual(const Entry &entry, bool app, bool temporal, bool filesystem) {
    if (geteuid() != 0) throw std::runtime_error("Native installation requires root");
    fs::create_directories(ROOT);
    const auto stage = stage_dir();
    const auto status_dir = stage / (stage == ROOT ? "" : "status");
    fs::create_directories(status_dir);
    Status status(status_dir, app ? entry.id : entry.slug());
    try {
        Lock owner(ROOT / ((app ? entry.id : entry.slug()) + ".lock"));
        status.write("queued", 2, "Waiting for package lock", entry.repo);
        Lock global(ROOT / "native-install.lock");
        setenv("DEBIAN_FRONTEND", "noninteractive", 1);
        setenv("TMPDIR", status_dir.c_str(), 1);
        status.write("running", 5, "Checking native release", entry.repo);
        bool already = install(entry, status, app, temporal, filesystem);
        status.write("complete", 100, already ? "Already installed" : "Installed",
                     "Native package and required files verified");
    } catch (const std::exception &e) {
        status.write("failed", 100, "Native installation failed", e.what());
        throw;
    }
    return 0;
}
struct Job {
    Entry entry;
    Asset asset;
    fs::path run, folder, package;
};
static Strings build_only_arguments(const Entry &entry, const fs::path &folder,
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
    fs::create_directory(private_home);
    fs::create_directory(private_tmp);
    arguments.insert(arguments.end(), {"/usr/bin/env", "MAKEFLAGS=-j" + slots,
                      "CMAKE_BUILD_PARALLEL_LEVEL=" + slots,
                      "DEB_BUILD_OPTIONS=parallel=" + slots,
                      "HOME=" + private_home.string(), "TMPDIR=" + private_tmp.string(),
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
    arguments.push_back(job.run.string());
    const auto options = build_only_arguments(job.entry, job.folder, slots);
    arguments.insert(arguments.end(), options.begin(), options.end());
    if (execute(arguments, nullptr, logs / (job.entry.slug() + "-build.log")) != 0)
        throw std::runtime_error("Native build failed: " + job.entry.slug());
    job.package = native_package_from(job.folder, job.entry, job.asset);
    if (!job.asset.url.empty()) {
        verify_native_payload(job.package, job.entry, job.asset, job.folder);
        fs::remove(job.run);
    }
}
static void build_packages(std::vector<Job> &jobs, const fs::path &statuses,
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
static Strings application_build_dependencies(const Entry &e) {
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
static int smart_queue(const std::vector<Entry> &entries) {
    if (geteuid() != 0) throw std::runtime_error("Smart queue requires root");
    fs::path stage = stage_dir();
    fs::path statuses = stage / "status";
    if (stage == ROOT || environment("INFILTRATOROS_RELEASE_DIR").empty())
        throw std::runtime_error("Smart queue requires a native run snapshot");
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
    std::vector<std::unique_ptr<Lock>> locks;
    for (const auto &slug : selected)
        if (infiltrator_native::queue_candidate(slug))
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
                const bool app = e.kind == "application";
                const bool temporal = e.kind == "calendar" || e.kind == "clock";
                status.write("queued", 2, "Smart build queued", "Awaiting native package lock");
                Document meta = release(e);
                Asset asset = release_asset(e, meta.ptr, temporal, !app && !temporal);
                if (app ? application_current(e, asset, true) : provider_current(e, asset)) {
                    status.write("complete", 100, "Already installed", "Native package verified");
                    completed.insert(e.slug());
                    continue;
                }
                if (!can_build(e)) {
                    auto missing = missing_packages(build_dependency_packages(e));
                    if (!missing.empty()) {
                        status.write("running", 8, "Preparing build prerequisites",
                                     "Serial APT transaction before parallel compilation");
                        Strings command{"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1",
                            "install", "-y", "--no-remove", "--no-install-recommends"};
                        command.insert(command.end(), missing.begin(), missing.end());
                        required(command, statuses / (e.slug() + "-dependencies.log"));
                    }
                }
                if (!can_build(e))
                    throw std::runtime_error("Native prerequisites unavailable; refusing unscheduled build");
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
                const bool app = job.entry.kind == "application";
                // Use the same lock-aware transaction for every queued DEB.
                required(package_install_command(job.package, app && job.entry.id == "calendar"),
                         statuses / (job.entry.slug() + "-package.log"));
                verify_install(job.entry, job.asset, app);
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
static void discover(const std::vector<Entry> &extra, const fs::path &snapshot) {
    if (geteuid() != 0) throw std::runtime_error("Native discovery requires root");
    const fs::path directory = fs::canonical(snapshot);
    if (directory.parent_path().parent_path() != ROOT ||
        directory.parent_path().filename().string().rfind("native-worker.", 0) != 0)
        throw std::runtime_error("Invalid additional filesystem snapshot directory");
    Document pending=parse(read_file(directory/"optional-pending.json"));
    Strings available,missing;
    std::set<std::string> requested;
    const auto manifest = directory.parent_path() / "selected.list";
    if (fs::is_regular_file(manifest))
        for (const auto &slug : lines(read_file(manifest))) requested.insert(slug);
    for(const auto &e:extra) {
        if (!requested.empty() && !requested.count(e.slug())) continue;
        try {
            auto file=directory/(e.repo+".json");
            if(!fs::is_regular_file(file)) {
                auto *reason=field(pending.ptr,e.repo.c_str());
                if(json_object_get_type(reason)!=json_type_string)
                    throw std::runtime_error("Invalid optional provider status");
                throw std::runtime_error(json_object_get_string(reason));
            }
            Document meta=parse(read_file(file));
            (void)release_asset(e,meta.ptr,false,true);
            available.push_back(e.slug());
        } catch(const std::exception &error) {
            missing.push_back(e.repo+": "+error.what());
        }
    }
    save(directory.parent_path() / "filesystems-optional.list", joined(available));
    save(directory.parent_path() / "filesystems-pending.list", joined(missing));
    std::cout << "Filesystem publication: " << available.size() << " installable, "
              << missing.size() << " pending\n";
}
int main(int argc, char **argv) {
    try {
        curl_global_init(CURL_GLOBAL_DEFAULT);
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
            if (argc == 2 && std::string(argv[1]) == "--list") {
                list(entries, entries.size(), "INFILTRATOROS_SELECTED_TEMPORAL", false);
                return 0;
            }
            if (argc != 2) throw std::runtime_error("Usage: install-temporal-resources SLUG");
            auto found = std::find_if(entries.begin(), entries.end(),
                          [&](const Entry &e) { return e.slug() == argv[1]; });
            if (found == entries.end()) throw std::runtime_error("Unknown signed temporal provider");
            return individual(*found, false, true, false);
        }
        if (name == "install-filesystem-resources") {
            auto established = catalogue(directory / "filesystem-resources.json");
            auto extra = catalogue(directory / "filesystem-additional.json");
            auto all = established;
            all.insert(all.end(), extra.begin(), extra.end());
            if (argc == 2 && std::string(argv[1]) == "--list") {
                list(all, established.size(), "INFILTRATOROS_SELECTED_FILESYSTEMS", true);
                return 0;
            }
            if (argc == 2 && std::string(argv[1]) == "--list-all") {
                list(all, all.size(), "INFILTRATOROS_SELECTED_FILESYSTEMS", true);
                return 0;
            }
            if (argc == 3 && std::string(argv[1]) == "--discover-additional") {
                discover(extra, argv[2]);
                return 0;
            }
            if (argc != 2) throw std::runtime_error("Usage: install-filesystem-resources SLUG");
            auto found = std::find_if(all.begin(), all.end(),
                          [&](const Entry &e) { return e.slug() == argv[1]; });
            if (found == all.end()) throw std::runtime_error("Unknown signed filesystem");
            return individual(*found, false, false, true);
        }
        if (name.rfind("install-", 0) == 0)
            return individual(application(name.substr(8)), true, false, false);
        throw std::runtime_error("Unknown native worker");
    } catch (const std::exception &error) {
        std::cerr << "InfiltratorOS native provider: " << error.what() << "\n";
        return 1;
    }
}
