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
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <curl/curl.h>
#include "native-build-queue.h"
#include "native-build-resources.h"
#include "native-secure-boot.h"
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
    if (capture) {
        close(pipes[1]);
        char buffer[8192];
        ssize_t n = 0;
        while ((n = read(pipes[0], buffer, sizeof(buffer))) > 0) {
            if (capture->size() + static_cast<size_t>(n) > MAX_JSON) {
                close(pipes[0]);
                throw std::runtime_error("Command output exceeds 4 MiB");
            }
            capture->append(buffer, static_cast<size_t>(n));
        }
        close(pipes[0]);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("Cannot wait for native command");
    }
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
static void transfer(const std::string &url, Transfer &result, bool binary = false,
                     long timeout = 120L) {
    if (url.rfind("https://", 0) != 0) throw std::runtime_error("Unsafe native download URL");
    bool api = url.rfind("https://api.github.com/", 0) == 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (result.file) {
            rewind(result.file);
            if (ftruncate(fileno(result.file), 0)) throw std::runtime_error("Cannot reset download");
        } else result.data.clear();
        result.length = 0;
        result.too_large = false;
        CURL *curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Cannot initialise libcurl");
        struct curl_slist *headers = nullptr;
        if (api) {
            headers = curl_slist_append(headers, binary ?
                            "Accept: application/octet-stream" : "Accept: application/vnd.github+json");
            const std::string token = environment("GITHUB_TOKEN");
            if (!token.empty()) {
                std::string value = "Authorization: Bearer " + token;
                headers = curl_slist_append(headers, value.c_str());
            }
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
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
        curl_slist_free_all(headers);
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
    transfer(url, result, false, timeout);
    return result.data;
}
struct Entry {
    std::string id, name, repo, package, resource, kind;
    std::string slug() const { return lowercase(repo); }
};
static std::vector<Entry> catalogue(const fs::path &path, size_t expected) {
    Document root = parse(read_file(path));
    if (json_object_get_type(root.ptr) != json_type_array ||
        json_object_array_length(root.ptr) != expected)
        throw std::runtime_error("Native component catalogue has unexpected size");
    std::set<std::string> ids;
    std::vector<Entry> result;
    for (size_t i = 0; i < expected; ++i) {
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
};
static constexpr const char *MINIMUM_BOOT_RELEASE = "0.1.50";
// This target is not an approval to deploy the candidate: release_asset()
// refuses prereleases until the kernel has been promoted after qualification.
static constexpr const char *NATIVE_KERNEL_RELEASE = "0.1.8";

static Asset release_asset(const Entry &entry, json_object *meta, bool calendar_or_clock,
                           bool filesystem) {
    if (boolean(meta, "draft") || boolean(meta, "prerelease"))
        throw std::runtime_error("Unpublished native component release");
    const std::string tag = str(meta, "tag_name");
    if (!std::regex_match(tag, std::regex("v[0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?")))
        throw std::runtime_error("Invalid native component version");
    if (calendar_or_clock || filesystem || entry.id == "calendar") {
        std::string source = str(meta, "target_commitish");
        if (!std::regex_match(source, std::regex("[a-f0-9]{40}")))
            throw std::runtime_error("Provider is missing immutable source identity");
    }
    Asset result;
    result.version = tag.substr(1);
    if (entry.id == "boot" &&
        execute({"/usr/bin/dpkg", "--compare-versions", result.version, "ge", MINIMUM_BOOT_RELEASE}) != 0)
        throw std::runtime_error("BOOT requires version 0.1.50 or newer for automatic native kernel discovery and EXT4 upgrades");
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
    if (entry.id == "kernel" && result.version != NATIVE_KERNEL_RELEASE)
        throw std::runtime_error("Kernel release differs from the pinned conversion release");
    const std::string run_name = entry.id == "kernel"
        ? "InfiltratorOS-Kernel-" + result.version + "-amd64.run"
        : run_prefix + "-" + result.version + run_suffix;
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
        if (name == run_name) result = {tag.substr(1), url, sha.substr(7), size};
    }
    if (result.url.empty()) throw std::runtime_error("Native RUN asset is missing");
    if (calendar_or_clock && (names.size() != 3 ||
        !names.count(entry.slug() + "-" + result.version + "-source.zip") ||
        !names.count(entry.package + "_" + result.version + "_amd64.deb")))
        throw std::runtime_error("Unqualified temporal release assets");
    if (filesystem && !names.count(entry.package + "_" + result.version + "_amd64.deb"))
        throw std::runtime_error("Filesystem native release is missing its generic package");
    return result;
}
static Document release(const Entry &entry) {
    const std::string directory = environment("INFILTRATOROS_RELEASE_DIR");
    // The v3 catalogue has a fixed 63-repository contract shared with
    // earlier InfiltratorOS releases. Calendar resolves its own release.
    return parse(directory.empty() || entry.id == "calendar" || entry.id == "boot"
        ? fetch("https://api.github.com/repos/Infiltrator-Projects/" +
                entry.repo + "/releases/latest")
        : read_file(fs::path(directory) / (entry.repo + ".json")));
}
static std::string api_asset_url(const Asset &asset) {
    const std::string prefix = "https://github.com/Infiltrator-Projects/";
    if (asset.url.rfind(prefix, 0) != 0)
        throw std::runtime_error("Noncanonical alternate asset");
    std::smatch match;
    const std::string relative = asset.url.substr(prefix.size());
    if (!std::regex_match(relative, match, std::regex(
            "([A-Za-z0-9-]+)/releases/download/(v[0-9]+\\.[0-9]+\\.[0-9]+)/([A-Za-z0-9_.+-]+)")))
        throw std::runtime_error("Invalid alternate asset identity");
    std::string repo = match[1], tag = match[2], name = match[3];
    Document meta = parse(fetch("https://api.github.com/repos/Infiltrator-Projects/" +
                                repo + "/releases/tags/" + tag));
    if (str(meta.ptr, "tag_name") != tag || boolean(meta.ptr, "draft") ||
        boolean(meta.ptr, "prerelease"))
        throw std::runtime_error("Alternate asset resolved a different release");
    auto *assets = field(meta.ptr, "assets");
    int64_t id = 0;
    unsigned matches = 0;
    for (size_t i = 0; i < json_object_array_length(assets); ++i) {
        auto *item = json_object_array_get_idx(assets, i);
        if (str(item, "name") != name) continue;
        auto *identity = field(item, "id");
        if (str(item, "browser_download_url") != asset.url ||
            str(item, "digest") != "sha256:" + asset.sha || integer(item, "size") != asset.size ||
            json_object_get_type(identity) != json_type_int ||
            json_object_get_int64(identity) <= 0)
            throw std::runtime_error("Alternate asset does not match original SHA-256");
        ++matches;
        id = json_object_get_int64(identity);
    }
    if (matches != 1) throw std::runtime_error("Ambiguous GitHub asset identity");
    return "https://api.github.com/repos/Infiltrator-Projects/" + repo +
           "/releases/assets/" + std::to_string(id);
}
static void download(const Asset &asset, const fs::path &target) {
    for (int route = 0; route != 2; ++route) {
        const std::string address = route ? api_asset_url(asset) : asset.url;
        FILE *file = fopen(target.c_str(), "wb+");
        if (!file) throw std::runtime_error("Cannot create installer");
        Transfer sink;
        sink.file = file;
        sink.limit = asset.size;
        bool closed = false;
        try {
            transfer(address, sink, route != 0);
            if (fclose(file) != 0) {
                closed = true;
                throw std::runtime_error("Cannot close native installer");
            }
            closed = true;
            if (sink.length != asset.size || digest(target) != asset.sha)
                throw std::runtime_error("Installer digest/length mismatch");
            if (chmod(target.c_str(), 0700) != 0)
                throw std::runtime_error("Cannot make installer executable");
            return;
        } catch (const std::exception &error) {
            if (!closed) fclose(file);
            fs::remove(target);
            if (route != 0) throw;
            std::cerr << "Retrying exact GitHub asset API: " << error.what() << "\n";
        }
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
    ~TempDir() { if (!path.empty()) fs::remove_all(path); }
};
struct Lock {
    int fd;
    explicit Lock(const fs::path &name) {
        fd = open(name.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd < 0) throw std::runtime_error("Cannot open native lock");
        if (flock(fd, LOCK_EX) != 0) throw std::runtime_error("Cannot acquire native lock");
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
                            "{X-Infiltrator-Native-Profile}\t$" + "{X-Infiltrator-PGO}";
    auto status = tabs(pkg_query(entry.package, fmt));
    if (status.size() != 4 || status[0] != "install ok installed" ||
        status[1] != asset.version + "+native1" || status[2] != "aggressive" ||
        status[3] != "trained-two-pass") return false;
    fs::path manual = fs::path("/System/Resources/Documentation") / (entry.repo + ".pdf");
    for (const auto &f : {fs::path(entry.resource), manual}) {
        if (!regular(f) || output({"/usr/bin/dpkg-query", "-S", f.string()}) !=
                            entry.package + ": " + f.string()) return false;
    }
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
        e.resource = "/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI";
    } else throw std::runtime_error("Unknown native application");
    e.name = e.repo;
    return e;
}
static Strings required_paths(const std::string &slug) {
    if (slug == "boot") return {"/usr/sbin/infiltrator-boot-deploy", "/usr/lib/infiltrator-boot/deploy-core",
                                "/usr/lib/infiltrator-boot/validate-module",
                                "/usr/lib/infiltrator-boot/EFI/BOOT/boot.conf"};
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
            "/System/Commands/Defragmenter/ext4/ext-metadata-worker"};
}
static bool application_proof(const Entry &entry) {
    if (entry.id == "calculator") return true;
    if (entry.id == "boot") {
        const fs::path proof = "/usr/share/doc/infiltrator-boot/BUILD-INFO";
        if (!regular(proof)) return false;
        const auto data = read_file(proof);
        return data.find("Profile: aggressive") != std::string::npos &&
               data.find("-march=native -mgeneral-regs-only") != std::string::npos;
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
        entry.id == "files" || entry.id == "defragmenter" || entry.id == "boot" ? "+native1" : "";
    return fields[1] == asset.version + suffix;
}
static bool application_current(const Entry &entry, const Asset &asset,
                                bool evidence, std::string *reason = nullptr) {
    auto fail = [&](const std::string &detail) {
        if (reason) *reason = detail;
        return false;
    };
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
                entry.id == "calendar" ? &verification : nullptr) != 0 ||
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
    std::string value = digest(entry.resource);
    json_object_object_add(proof, "program_sha256", json_object_new_string(value.c_str()));
    std::string content = json_object_to_json_string_ext(proof, JSON_C_TO_STRING_PLAIN);
    json_object_put(proof);
    TempDir staging(ROOT, "verified-native-");
    fs::path record = staging.path / "attestation";
    save(record, content);
    fs::rename(record, ROOT / ("native-verified-" + entry.id + ".json"));
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
    Strings command_line{run.string()};
    if (temporal || filesystem) {
        command_line.insert(command_line.end(), {"--profile", "aggressive", "--system-package-mode"});
    } else if (entry.id != "defragmenter" && entry.id != "calendar") {
        command_line.push_back("--system-package-mode");
    }
    required(command_line);
    verify_install(entry, asset, application_mode);
    return false;
}
static std::vector<Entry> boot_filesystems(const fs::path &stage) {
    if (stage == ROOT || !regular(stage / "selected.list")) return {};
    auto catalogue_entries = catalogue(stage / "filesystem-resources.json", 5);
    auto additional = catalogue(stage / "filesystem-additional.json", 18);
    catalogue_entries.insert(catalogue_entries.end(), additional.begin(), additional.end());
    std::vector<Entry> selected;
    for (const auto &slug : lines(read_file(stage / "selected.list"))) {
        if (slug.rfind("filesystem-", 0) != 0) continue;
        auto entry = std::find_if(catalogue_entries.begin(), catalogue_entries.end(),
                                  [&](const Entry &e) { return e.slug() == slug; });
        if (entry == catalogue_entries.end())
            throw std::runtime_error("Unrecognised BOOT filesystem selection: " + slug);
        selected.push_back(*entry);
    }
    return selected;
}
static std::string provider_state(const fs::path &statuses, const Entry &provider) {
    const Status status(statuses, provider.slug());
    if (!regular(status.path)) return {};
    for (const auto &field : lines(read_file(status.path))) {
        if (field.rfind("state=", 0) == 0) return field.substr(6);
    }
    return {};
}
static void wait_for_boot_filesystems(const fs::path &statuses,
                                      const std::vector<Entry> &providers) {
    // Native filesystem workers need the same global package lock. Wait
    // outside that lock, after the dispatcher has launched every provider.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(23) +
                          std::chrono::minutes(50);
    while (std::any_of(providers.begin(), providers.end(), [&](const Entry &provider) {
        const auto state = provider_state(statuses, provider);
        return state != "complete" && state != "failed";
    })) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Filesystem workers did not finish in time for BOOT deployment");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
static Strings boot_deployment(const fs::path &stage, const fs::path &statuses,
                               const std::vector<Entry> &providers) {
    Strings command{(stage / "install-boot-option").string()};
    for (const auto &provider : providers) {
        if (provider_state(statuses, provider) != "complete") continue;
        const auto meta = release(provider);
        const auto asset = release_asset(provider, meta.ptr, false, true);
        if (!provider_current(provider, asset))
            throw std::runtime_error("Filesystem package changed before BOOT deployment: " + provider.package);
        const int result = execute({"/usr/lib/infiltrator-boot/validate-module", provider.resource});
        if (result == 1) {
            std::cout << "BOOT firmware profile unavailable for " << provider.resource << "\n";
            continue;
        }
        if (result != 0)
            throw std::runtime_error("Could not verify BOOT filesystem module: " + provider.resource);
        command.push_back(provider.resource);
    }
    return command;
}

static bool native_kernel_installed(const fs::path &system = "/System", uid_t owner = 0) {
    const std::string native = std::string("InfiltratorOS-") + NATIVE_KERNEL_RELEASE;
    const fs::path kernel = system / "Kernel";
    // The native helper refuses a user-writable path even when the files at
    // its end happen to be root-owned. Match that contract before declaring
    // the kernel installed; lstat also rejects replaced parent symlinks.
    for (const auto &path : {system, kernel, kernel / "Images", kernel / "Modules",
                            kernel / "Modules" / native, kernel / "Runtime",
                            kernel / "Runtime" / native}) {
        struct stat st{};
        if (lstat(path.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != owner ||
            (st.st_mode & 022))
            return false;
    }
    for (const auto &path : {kernel / "Images" / (native + ".img"),
                            kernel / "Modules" / native / "modules.dep.bin",
                            kernel / "Modules" / native / "modules.alias.bin",
                            kernel / "Runtime" / native / "modprobe",
                            kernel / "Runtime" / native / "verify-host-drivers"}) {
        struct stat st{};
        if (lstat(path.c_str(), &st) || !S_ISREG(st.st_mode) || st.st_uid != owner ||
            (st.st_mode & 022) || st.st_size <= 0)
            return false;
    }
    // Module indexes can point into nested directories. Checking just the
    // index files and top-level owner would still admit user-writable driver
    // files, even though the kernel later runs the helper with root access.
    std::error_code error;
    fs::recursive_directory_iterator it(kernel / "Modules" / native, error), end;
    if (error) return false;
    while (it != end) {
        struct stat st{};
        if (lstat(it->path().c_str(), &st) ||
            (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) ||
            st.st_uid != owner || (st.st_mode & 022))
            return false;
        it.increment(error);
        if (error) return false;
    }
    return access((kernel / "Runtime" / native / "modprobe").c_str(), X_OK) == 0 &&
           access((kernel / "Runtime" / native / "verify-host-drivers").c_str(), X_OK) == 0;
}

static void verify_installed_kernel_drivers(const fs::path &kernel = "/System/Kernel") {
    const std::string native = std::string("InfiltratorOS-") + NATIVE_KERNEL_RELEASE;
    const auto checker = kernel / "Runtime" / native / "verify-host-drivers";
    const auto modules = kernel / "Modules" / native;
    if (execute({"/bin/bash", checker.string(), modules.string()}) != 0)
        throw std::runtime_error("Native kernel is missing an active hardware driver; existing BOOT was kept");
}

static void verify_boot_setup_result(int result) {
    if (result == 77)
        throw std::runtime_error("BOOT firmware setup was skipped; existing BOOT was kept. See boot-install.log");
    if (result != 0)
        throw std::runtime_error("BOOT firmware setup failed; see boot-install.log");
}

static void install_native_kernel(Status &status) {
    // The current native boot contract requires a direct GPT EXT4 root and
    // a firmware-compatible EXT4 provider. Leave the distro path available
    // on other layouts without spending time on a kernel that cannot boot.
    if (!regular("/sys/firmware/efi/fw_platform_size") ||
        trim(read_file("/sys/firmware/efi/fw_platform_size")) != "64")
        throw std::runtime_error("Native kernel requires 64-bit EFI firmware; the existing boot configuration was kept");
    if (output({"/usr/bin/findmnt", "-nro", "FSTYPE,FSROOT", "-M", "/"}) != "ext4 /")
        throw std::runtime_error("Native kernel requires a direct EXT4 root mount; the existing boot configuration was kept");
    constexpr const char *secure_boot =
        "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c";
    std::ifstream secure(secure_boot, std::ios::binary);
    if (!secure_boot_disabled(secure))
        throw std::runtime_error("Native kernel requires Secure Boot disabled and a readable firmware state");
    const auto device = output({"/usr/bin/findmnt", "-nro", "SOURCE", "-M", "/"});
    if (device.rfind("/dev/", 0) != 0 ||
        output({"/usr/sbin/blkid", "-p", "-s", "PART_ENTRY_SCHEME", "-o", "value", "--", device}) != "gpt")
        throw std::runtime_error("Native kernel could not verify a direct GPT root partition");
    if (execute({"/usr/lib/infiltrator-boot/validate-module", "/System/Filesystems/EXT4.so"}) != 0)
        throw std::runtime_error("Native kernel requires a firmware-compatible EXT4 filesystem module");
    if (native_kernel_installed()) {
        verify_installed_kernel_drivers();
        return;
    }
    const Entry entry{"kernel", "InfiltratorOS Kernel", "Kernel", "", "", ""};
    TempDir directory(status.root, "native-kernel-");
    const auto run = directory.path / "kernel-installer.run";
    status.write("running", 75, "Preparing native kernel build", "Verifying the published Kernel installer");
    const auto release = parse(fetch(std::string("https://api.github.com/repos/Infiltrator-Projects/Kernel/releases/tags/v") +
                                     NATIVE_KERNEL_RELEASE));
    const auto asset = release_asset(entry, release.ptr, false, false);
    download(asset, run);
    required({"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "--no-remove",
              "--no-install-recommends", "-y", "install", "git", "build-essential", "bc",
              "bison", "flex", "fakeroot", "dwarves", "libdw-dev", "libelf-dev", "libssl-dev",
              "libkmod-dev", "rsync", "dpkg-dev", "debhelper", "kmod", "python3", "zstd"});
    status.write("running", 85, "Building the InfiltratorOS kernel", "Keeping the distribution kernel available through GRUB");
    // Manual builds can leave a user-owned Kernel workspace. The automatic
    // root build uses the worker's own private directory for source and tools.
    required({"/usr/bin/env", "INFILTRATOROS_TEMP_ROOT=" + (directory.path / "build").string(),
              run.string(), "--system-package-mode"});
    if (!native_kernel_installed())
        throw std::runtime_error("Native Kernel installation did not produce a trusted image and indexed drivers");
    verify_installed_kernel_drivers();
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
        std::vector<Entry> providers;
        if (entry.id == "boot") {
            providers = boot_filesystems(stage);
            if (!providers.empty()) {
                status.write("queued", 2, "Waiting for filesystem providers", "BOOT stages compatible modules after installation");
                wait_for_boot_filesystems(status_dir, providers);
            }
        }
        status.write("queued", 2, "Waiting for package lock", entry.repo);
        Lock global(ROOT / "native-install.lock");
        setenv("DEBIAN_FRONTEND", "noninteractive", 1);
        setenv("TMPDIR", status_dir.c_str(), 1);
        status.write("running", 5, "Checking native release", entry.repo);
        bool already = install(entry, status, app, temporal, filesystem);
        if (entry.id == "boot") {
            // Clean the known temporary BOOT independently of the kernel
            // build and guarded BOOT deployment. The helper verifies the ESP,
            // active entry and GRUB fallback before touching firmware state.
            status.write("running", 70, "Cleaning up BOOT", "Checking for an obsolete temporary entry");
            verify_boot_setup_result(execute({(stage / "install-boot-option").string(),
                                              "--cleanup-obsolete"}));
            auto command = boot_deployment(stage, status_dir, providers);
            const bool ext4_ready = std::find(command.begin(), command.end(),
                                              "/System/Filesystems/EXT4.so") != command.end();
            if (output({"/usr/bin/findmnt", "-nro", "FSTYPE", "-M", "/"}) == "ext4") {
                if (!ext4_ready)
                    throw std::runtime_error("Cannot install the native kernel: the EXT4 firmware module is not ready");
                install_native_kernel(status);
            }
            status.write("running", 95, "Configuring firmware option", "Checking the EFI system partition");
            verify_boot_setup_result(execute(command));
        }
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
static void build(Job &job, const fs::path &logs, const std::vector<int> &cpus) {
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
    arguments.insert(arguments.end(), {"/usr/bin/env", "MAKEFLAGS=-j" + slots,
                     "CMAKE_BUILD_PARALLEL_LEVEL=" + slots, job.run.string()});
    // Files always trains its aggressive build. The provider RUNs take a profile.
    if (job.entry.kind != "application")
        arguments.insert(arguments.end(), {"--profile", "aggressive"});
    if (job.entry.kind == "calendar" || job.entry.kind == "clock")
        arguments.insert(arguments.end(), {"--jobs", slots});
    arguments.insert(arguments.end(), {"--system-package-mode", "--build-only",
                                      "--output", job.folder.string()});
    if (execute(arguments, nullptr, logs / (job.entry.slug() + "-build.log")) != 0)
        throw std::runtime_error("Native build failed: " + job.entry.slug());
    std::vector<fs::path> packages;
    for (auto &item : fs::directory_iterator(job.folder))
        if (item.path().extension() == ".deb") packages.push_back(item.path());
    if (packages.size() != 1) throw std::runtime_error("Native build produced no unique DEB");
    job.package = packages.front();
    const std::map<std::string, std::string> expected{
        {"Package", job.entry.package},
        {"Version", job.asset.version + "+native1"},
        {"Architecture", "amd64"}};
    for (const auto &[name, value] : expected)
        if (output({"/usr/bin/dpkg-deb", "-f", job.package.string(), name}) != value)
            throw std::runtime_error("Invalid component-built DEB identity");
}
static void build_packages(std::vector<Job> &jobs, const fs::path &statuses) {
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
    while (next < jobs.size() || !active.empty()) {
        for (auto it = active.begin(); it != active.end();) {
            if (it->result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            it->result.get();
            timings << elapsed() << "ms finished " << jobs[it->job].entry.slug() << std::endl;
            it = active.erase(it);
        }
        if (next < jobs.size()) {
            const auto resources = build_resources(allowed_build_cpus());
            std::vector<BuildReservation> reservations;
            for (const auto &job : active) reservations.push_back(job.resources);
            const auto &entry = jobs[next].entry;
            const auto kind = entry.kind == "application" ? BuildKind::files :
                entry.id == "exfat" ? BuildKind::exfat : BuildKind::module;
            auto reservation = reserve_build(resources, reservations, kind, jobs.size() - next);
            if (reservation) {
                const auto job = next++;
                const auto count = std::to_string(active.size() + 1);
                const auto slots = std::max<std::size_t>(1, reservation->cpus.size());
                Status(statuses, entry.slug()).write("running", 35, "Compiling native component",
                    count + " builds active, " + std::to_string(slots) + " CPU slots for this build");
                timings << elapsed() << "ms started " << entry.slug() << "; active=" << count
                        << "; cpu_slots=" << slots << '/' << resources.cpu_budget
                        << "; reserved_mib=" << reservation->memory_mib
                        << "; memory_budget_mib=" << resources.memory_mib << std::endl;
                auto result = std::async(std::launch::async, [&, job, cpus = reservation->cpus] {
                    build(jobs[job], statuses, cpus);
                });
                active.push_back({job, std::move(*reservation), std::move(result)});
                continue;
            }
        }
        if (!active.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
static bool can_build(const Entry &e) {
    Strings packages{"build-essential", "binutils", "dpkg-dev"};
    // A build-only RUN cannot install its own prerequisites. Keep those jobs
    // on their original, serial path so the component can provision them.
    if (e.kind == "application") {
        packages.insert(packages.end(), {"cmake", "ninja-build", "pkg-config", "xvfb",
                                         "xauth", "dbus-daemon"});
        if (execute({"pkg-config", "--exists", "gtk4 >= 4.10", "libarchive"}) != 0)
            return false;
    } else if (e.kind == "calendar" || e.kind == "clock") {
        packages.insert(packages.end(), {"pkg-config", "jq", "ghostscript", "gettext"});
        if (e.kind == "calendar" && execute({"pkg-config", "--exists", "glib-2.0"}) != 0)
            return false;
        if ((e.id == "gregorian" || e.id == "indian" || e.id == "islamic-umalqura") &&
            execute({"pkg-config", "--exists", "icu-i18n"}) != 0)
            return false;
    }
    if (e.id == "exfat")
        packages.insert(packages.end(), {"cmake", "git", "pkg-config", "libssl-dev",
                                         "libsqlite3-dev", "exfatprogs", "dosfstools"});
    if (e.id == "ntfs") {
        packages.insert(packages.end(), {"cmake", "git", "pkg-config", "libssl-dev",
                                         "libsqlite3-dev"});
        const std::string status = std::string("$") + "{Status}";
        if (pkg_query("libntfs-3g89t64", status) != "install ok installed" &&
            pkg_query("libntfs-3g89", status) != "install ok installed")
            return false;
    }
    for (auto &pkg : packages)
        if (pkg_query(pkg, std::string("$") + "{Status}") != "install ok installed")
            return false;
    return true;
}
static int smart_queue(const std::vector<Entry> &entries) {
    if (geteuid() != 0) throw std::runtime_error("Smart queue requires root");
    fs::path stage = stage_dir();
    fs::path statuses = stage / "status";
    if (stage == ROOT || environment("INFILTRATOROS_RELEASE_DIR").empty())
        throw std::runtime_error("Smart queue requires a native run snapshot");
    std::set<std::string> selected;
    for (const auto &item : lines(read_file(stage / "selected.list"))) selected.insert(item);
    std::vector<Entry> chosen;
    for (auto slug : infiltrator_native::build_queue)
        if (selected.count(std::string(slug))) {
            auto it = std::find_if(entries.begin(), entries.end(),
                                   [&](const Entry &e) { return e.slug() == std::string(slug); });
            if (it == entries.end()) throw std::runtime_error("Unknown smart queue candidate");
            chosen.push_back(*it);
        }
    if (chosen.size() < 2) throw std::runtime_error("Insufficient smart build candidates");
    std::vector<std::unique_ptr<Lock>> locks;
    for (const auto &e : chosen)
        locks.emplace_back(std::make_unique<Lock>(ROOT / (e.slug() + ".lock")));
    Lock global(ROOT / "native-install.lock");
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    setenv("TMPDIR", statuses.c_str(), 1);
    TempDir scratch(statuses, "smart-build-");
    std::vector<Job> jobs;
    std::vector<Entry> serial;
    std::set<std::string> completed;
    try {
        for (const auto &e : chosen) {
            const bool app = e.kind == "application";
            const bool temporal = e.kind == "calendar" || e.kind == "clock";
            Status status(statuses, e.slug());
            status.write("queued", 2, "Smart build queued", "Awaiting native package lock");
            Document meta = release(e);
            Asset asset = release_asset(e, meta.ptr, temporal, !app && !temporal);
            if (app ? application_current(e, asset, true) : provider_current(e, asset)) {
                status.write("complete", 100, "Already installed", "Native package verified");
                completed.insert(e.slug());
                continue;
            }
            if (!can_build(e)) {
                serial.push_back(e);
                status.write("queued", 10, "Serial fallback queued",
                             "Native prerequisites unavailable for parallel compilation");
                continue;
            }
            Job job{e, asset, scratch.path / (e.slug() + ".run"), scratch.path / e.slug(), {}};
            status.write("running", 15, "Downloading native RUN", "SHA-256 release verification");
            download(asset, job.run);
            fs::create_directory(job.folder);
            jobs.push_back(job);
        }
        build_packages(jobs, statuses);
        for (auto &job : jobs) {
            Status status(statuses, job.entry.slug());
            status.write("running", 85, "Installing native package", "Serial package transaction");
            const bool app = job.entry.kind == "application";
            if (app) {
                // Preserve Files' dependency handling and same-version repairs.
                required({"/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "install",
                          "-y", "--reinstall", job.package.string()});
            } else {
                required({"/usr/bin/dpkg", "--install", job.package.string()});
            }
            verify_install(job.entry, job.asset, app);
            status.write("complete", 100, "Native installation complete", "Verified installed package");
            completed.insert(job.entry.slug());
        }
        for (const auto &e : serial) {
            Status status(statuses, e.slug());
            const bool app = e.kind == "application";
            const bool temporal = e.kind == "calendar" || e.kind == "clock";
            bool already = install(e, status, app, temporal, !app && !temporal);
            status.write("complete", 100, "Serial fallback complete",
                         already ? "Already installed" : "Component-owned RUN installed");
            completed.insert(e.slug());
        }
    } catch (const std::exception &error) {
        for (const auto &e : chosen)
            if (!completed.count(e.slug()))
                Status(statuses, e.slug()).write("failed", 100, "Smart build failed", error.what());
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
    std::map<std::string, std::string> published, pending;
    for (const auto &e : extra) {
        try {
            std::string raw = fetch("https://api.github.com/repos/Infiltrator-Projects/" +
                                    e.repo + "/releases/latest", 12);
            Document meta = parse(raw);
            (void)release_asset(e, meta.ptr, false, true);
            published[e.repo] = raw;
        } catch (const std::exception &error) {
            pending[e.repo] = error.what();
        }
    }
    Strings available, missing;
    for (const auto &[name, raw] : published) {
        auto file = directory / (name + ".json");
        if (fs::exists(file)) throw std::runtime_error("Filesystem snapshot collision");
        save(file, raw + "\n");
        available.push_back(lowercase(name));
    }
    for (const auto &[name, message] : pending) missing.push_back(name + ": " + message);
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
            const std::string leader = name == "install-files" && argc == 1 ? "files" :
                (name == "install-filesystem-resources" || name == "install-temporal-resources") &&
                    argc == 2 ? argv[1] : "";
            if (!infiltrator_native::queue_candidate(leader))
                throw std::runtime_error("Invalid smart build launcher");
            auto entries = catalogue(directory / "filesystem-resources.json", 5);
            auto additional = catalogue(directory / "filesystem-additional.json", 18);
            auto temporal = catalogue(directory / "temporal-resources.json", 54);
            entries.insert(entries.end(), additional.begin(), additional.end());
            entries.insert(entries.end(), temporal.begin(), temporal.end());
            entries.push_back(application("files"));
            return smart_queue(entries);
        }
        if (name == "install-temporal-resources") {
            auto entries = catalogue(directory / "temporal-resources.json", 54);
            if (argc == 2 && std::string(argv[1]) == "--list") {
                list(entries, 54, "INFILTRATOROS_SELECTED_TEMPORAL", false);
                return 0;
            }
            if (argc != 2) throw std::runtime_error("Usage: install-temporal-resources SLUG");
            auto found = std::find_if(entries.begin(), entries.end(),
                          [&](const Entry &e) { return e.slug() == argv[1]; });
            if (found == entries.end()) throw std::runtime_error("Unknown signed temporal provider");
            return individual(*found, false, true, false);
        }
        if (name == "install-filesystem-resources") {
            auto established = catalogue(directory / "filesystem-resources.json", 5);
            auto extra = catalogue(directory / "filesystem-additional.json", 18);
            auto all = established;
            all.insert(all.end(), extra.begin(), extra.end());
            if (argc == 2 && std::string(argv[1]) == "--list") {
                list(all, 5, "INFILTRATOROS_SELECTED_FILESYSTEMS", true);
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
