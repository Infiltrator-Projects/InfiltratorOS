// SPDX-License-Identifier: GPL-3.0-or-later
// Independent native release snapshot validator for InfiltratorOS.
// Runtime implementation: C++17, libcurl TLS and json-c. No Python interpreter.
#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <memory>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json-c/json.h>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <unistd.h>
namespace fs = std::filesystem;

static constexpr size_t MAX_SNAPSHOT = 3 * 1024 * 1024;
static constexpr size_t MAX_METADATA = 512 * 1024 * 1024;
static constexpr const char *SNAPSHOT_URL =
    "https://raw.githubusercontent.com/Infiltrator-Projects/Infiltrator-Repository/main/catalogue/native-installers-v4.json";

struct Json {
    json_object *object = nullptr;
    explicit Json(json_object *ptr) : object(ptr) {
        if (!ptr) throw std::runtime_error("Invalid JSON release snapshot");
    }
    Json(const Json &) = delete;
    Json &operator=(const Json &) = delete;
    ~Json() { json_object_put(object); }
};
static json_object *field(json_object *object, const char *key) {
    json_object *value = nullptr;
    if (!object || json_object_get_type(object) != json_type_object ||
        !json_object_object_get_ex(object, key, &value))
        throw std::runtime_error(std::string("Missing JSON field: ") + key);
    return value;
}
static std::string string_field(json_object *object, const char *key) {
    json_object *value = field(object, key);
    if (json_object_get_type(value) != json_type_string)
        throw std::runtime_error(std::string("Invalid JSON string: ") + key);
    return json_object_get_string(value);
}
static bool boolean_field(json_object *object, const char *key) {
    json_object *value = field(object, key);
    if (json_object_get_type(value) != json_type_boolean)
        throw std::runtime_error(std::string("Invalid JSON boolean: ") + key);
    return json_object_get_boolean(value);
}
static size_t integer_field(json_object *object, const char *key) {
    json_object *value = field(object, key);
    if (json_object_get_type(value) != json_type_int || json_object_get_int64(value) <= 0 ||
        json_object_get_int64(value) > static_cast<int64_t>(MAX_METADATA))
        throw std::runtime_error(std::string("Invalid JSON integer: ") + key);
    return static_cast<size_t>(json_object_get_int64(value));
}
static std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}
static std::string read_text(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Unable to read catalogue: " + path.string());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
static std::vector<std::string> lines(const std::string &text) {
    std::vector<std::string> items;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            if (!current.empty()) items.push_back(current);
            current.clear();
        } else current += c;
    }
    if (!current.empty()) items.push_back(current);
    return items;
}
static void write_text(const fs::path &path, const std::string &text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || !(stream << text)) throw std::runtime_error("Unable to write " + path.string());
    if (chmod(path.c_str(), 0644) != 0) throw std::runtime_error("Cannot set resource mode");
}
struct Transfer {
    std::string body;
    size_t limit = 0;
    bool exceeded = false;
};
static size_t curl_write(char *ptr, size_t width, size_t count, void *userdata) {
    auto &transfer = *static_cast<Transfer *>(userdata);
    size_t n = width * count;
    if (n > transfer.limit || transfer.body.size() > transfer.limit - n) {
        transfer.exceeded = true;
        return 0;
    }
    transfer.body.append(ptr, n);
    return n;
}
static std::string https_get(const std::string &url) {
    for (int attempt = 0; attempt != 4; ++attempt) {
        CURL *curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Cannot initialise TLS client");
        Transfer transfer{"", MAX_SNAPSHOT, false};
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "InfiltratorOS-native/1");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        CURLcode error = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
        if (transfer.exceeded) throw std::runtime_error("Release snapshot exceeds 3 MiB");
        if (error == CURLE_OK && status >= 200 && status < 300) return transfer.body;
        if (status >= 400 && status < 500 && status != 408 && status != 429)
            throw std::runtime_error("Release snapshot HTTP failure " + std::to_string(status));
        if (attempt == 3)
            throw std::runtime_error("Release snapshot TLS/HTTP failure " +
                                     std::to_string(status) + " (" +
                                     curl_easy_strerror(error) + ")");
        std::this_thread::sleep_for(std::chrono::seconds(1 << attempt));
    }
    throw std::runtime_error("Release snapshot unavailable");
}
/* Public release redirects establish the current stable tag without using
 * the GitHub REST quota. Digests and commit identities are supplied only by
 * the centrally authenticated publisher. Never invent either from a tag. */
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
static std::set<std::string> expected_repos(const fs::path &providers,
                                            const fs::path &filesystems) {
    auto temporal = lines(read_text(providers));
    auto fsentries = lines(read_text(filesystems));
    if (temporal.empty() || fsentries.empty())
        throw std::runtime_error("Incomplete provider lists");
    std::set<std::string> items;
    for (const auto &slug : temporal) {
        if (!std::regex_match(slug, std::regex("(?:calendar|clock)-[a-z0-9-]+")) ||
            !items.insert(slug).second)
            throw std::runtime_error("Duplicate or invalid calendar/clock identity: " + slug);
    }
    for (const auto &slug : fsentries) {
        if (!std::regex_match(slug, std::regex("filesystem-[a-z0-9-]+")) ||
            !items.insert(slug).second)
            throw std::runtime_error("Duplicate or invalid filesystem identity: " + slug);
    }
    items.insert("system-monitor");
    items.insert("calculator");
    items.insert("files");
    items.insert("defragmenter");
    items.insert("boot");
    items.insert("calendar");
    if (items.size() != temporal.size() + fsentries.size() + 6)
        throw std::runtime_error("Unexpected catalogue cardinality");
    return items;
}
static void verify_release(const std::string &repo, json_object *release) {
    if (!std::regex_match(repo, std::regex("[A-Za-z][A-Za-z0-9-]+")) ||
        boolean_field(release, "draft") || boolean_field(release, "prerelease"))
        throw std::runtime_error(repo + ": unpublished or unsafe release");
    std::string tag = string_field(release, "tag_name");
    if (!std::regex_match(tag, std::regex("v[0-9]+\\.[0-9]+\\.[0-9]+(?:-[A-Za-z0-9.-]+)?")))
        throw std::runtime_error(repo + ": invalid version tag");
    auto lower = lowercase(repo);
    if (lower.rfind("calendar-", 0) == 0 || lower.rfind("clock-", 0) == 0 ||
        lower.rfind("filesystem-", 0) == 0 || lower == "calendar") {
        std::string revision = string_field(release, "target_commitish");
        if (!std::regex_match(revision, std::regex("[a-f0-9]{40}")))
            throw std::runtime_error(repo + ": release lacks immutable source identity");
    }
    if (lower == "filesystem-exfat") {
        int major = 0, minor = 0, patch = 0;
        if (sscanf(tag.c_str(), "v%d.%d.%d", &major, &minor, &patch) != 3 ||
            (major == 0 && minor == 1 && patch < 3))
            throw std::runtime_error("Unsafe exFAT release");
    }
    json_object *assets = field(release, "assets");
    if (json_object_get_type(assets) != json_type_array ||
        json_object_array_length(assets) == 0)
        throw std::runtime_error(repo + ": no release assets");
    const std::string prefix = "https://github.com/Infiltrator-Projects/" + repo +
                               "/releases/download/" + tag + "/";
    std::set<std::string> names;
    int installers = 0;
    const size_t count = json_object_array_length(assets);
    for (size_t n = 0; n < count; ++n) {
        auto *asset = json_object_array_get_idx(assets, n);
        std::string name = string_field(asset, "name");
        std::string url = string_field(asset, "browser_download_url");
        std::string digest = string_field(asset, "digest");
        size_t size = integer_field(asset, "size");
        if (size == 0 || url != prefix + name ||
            !std::regex_match(name, std::regex("[A-Za-z0-9_.+-]+")) ||
            !std::regex_match(digest, std::regex("sha256:[a-f0-9]{64}")) ||
            !names.insert(name).second)
            throw std::runtime_error(repo + ": invalid asset identity: " + name);
        if (name.size() >= 21 &&
            (name.find("-native-installer.run") != std::string::npos ||
             name.find("-local-folder.run") != std::string::npos))
            ++installers;
    }
    if (installers != 1)
        throw std::runtime_error(repo + ": expected exactly one native RUN installer");
}
// The publisher is shared by multiple product versions. An OS release
// authorizes only its own packaged catalogue, not every newly published
// filesystem. Reject duplicate/unsafe identities even outside that subset.
static std::unique_ptr<Json> selected_snapshot(json_object *source,
        const std::set<std::string> &required,
        const std::set<std::string> &optional,
        const std::set<std::string> &ignored = {}) {
    auto *schema = field(source, "schema");
    if (json_object_get_type(schema) != json_type_int || json_object_get_int(schema) != 1)
        throw std::runtime_error("Unsupported release snapshot schema");
    auto *releases = field(source, "releases");
    auto *pending = field(source, "pending");
    if (json_object_get_type(releases) != json_type_object ||
        json_object_get_type(pending) != json_type_object)
        throw std::runtime_error("Invalid release catalogue structure");
    auto result = std::make_unique<Json>(json_object_new_object());
    auto *selected_releases = json_object_new_object();
    auto *selected_pending = json_object_new_object();
    json_object_object_add(result->object, "schema", json_object_new_int(1));
    json_object_object_add(result->object, "releases", selected_releases);
    json_object_object_add(result->object, "pending", selected_pending);
    std::set<std::string> all_seen, selected_seen;
    auto selected = [&](const std::string &name) {
        const auto key = lowercase(name);
        if (!std::regex_match(name, std::regex("[A-Za-z][A-Za-z0-9-]+")) ||
            !all_seen.insert(key).second)
            throw std::runtime_error("Unsafe or duplicate repository in release snapshot: " + name);
        if (required.count(key) || optional.count(key)) {
            selected_seen.insert(key);
            return true;
        }
        if (!ignored.count(key) && !std::regex_match(key, std::regex("filesystem-[a-z0-9-]+")))
            throw std::runtime_error("Unexpected non-filesystem repository: " + name);
        return false;
    };
    json_object_object_foreach(releases, repository, release) {
        if (selected(repository)) {
            verify_release(repository, release);
            json_object_object_add(selected_releases, repository, json_object_get(release));
        }
    }
    {
        json_object_object_foreach(pending, repository, reason) {
            if (selected(repository)) {
                if (json_object_get_type(reason) != json_type_string)
                    throw std::runtime_error("Invalid pending provider reason");
                if (!optional.count(lowercase(repository)))
                    throw std::runtime_error("Required native release is pending: " + std::string(repository));
                json_object_object_add(selected_pending, repository, json_object_get(reason));
            }
        }
    }
    if (selected_seen.size() != required.size() + optional.size())
        throw std::runtime_error("Incomplete native OS catalogue");
    return result;
}
static std::unique_ptr<Json> checked_catalogue(
        const std::set<std::string> &required,
        const std::set<std::string> &optional,
        const std::set<std::string> &ignored = {}) {
    std::map<std::string,std::string> live_tags;
    std::string last_error;
    for (unsigned attempt=0; attempt<4; ++attempt) {
        try {
            // A unique query avoids serving a stale raw-content CDN cache
            // after the independently scheduled publisher has committed.
            const auto nonce=std::chrono::system_clock::now().time_since_epoch().count();
            const std::string content=https_get(std::string(SNAPSHOT_URL)+"?refresh="+std::to_string(nonce));
            Json raw(json_tokener_parse(content.c_str()));
            auto root = selected_snapshot(raw.object, required, optional, ignored);
            auto *releases = field(root->object, "releases");
            std::vector<std::string> stale;
            json_object_object_foreach(releases, repository, release) {
                const std::string name(repository);
                // Resolve each live tag only once per attempt series. The
                // next catalogue can catch up without another REST lookup.
                auto found=live_tags.find(name);
                if (found==live_tags.end())
                    found=live_tags.emplace(name,live_latest_tag(name)).first;
                const auto cached=string_field(release,"tag_name");
                if (cached!=found->second) stale.push_back(name+" ("+cached+" vs "+found->second+")");
            }
            if (stale.empty()) return root;
            last_error="Authenticated publisher has not caught up: "+stale.front()+
                " ("+std::to_string(stale.size())+" changed releases)";
        } catch(const std::exception &error) {
            last_error=error.what();
        }
        std::cerr<<"Native catalogue check "<<attempt+1<<"/4: "<<last_error<<'\n';
        if(attempt<3)std::this_thread::sleep_for(std::chrono::seconds(5u*(attempt+1u)));
    }
    throw std::runtime_error(last_error+"; no unverified or stale installer was selected");
}
static void prepare(const fs::path &destination, const fs::path &providers,
                    const fs::path &filesystems,const fs::path &all_filesystems,
                    const fs::path &selected = {}) {
    if (geteuid() != 0) throw std::runtime_error("Snapshot preparation requires root");
    auto required=expected_repos(providers,filesystems);
    std::set<std::string> optional, all;
    for(const auto &name:lines(read_text(all_filesystems))) {
        if(!std::regex_match(name,std::regex("filesystem-[a-z0-9-]+")) ||
           !all.insert(name).second)throw std::runtime_error("Invalid complete filesystem list");
        if(!required.count(name))optional.insert(name);
    }
    for (const auto &name : required)
        if (name.rfind("filesystem-", 0) == 0 && !all.count(name))
            throw std::runtime_error("Incomplete filesystem list");
    std::set<std::string> ignored;
    if (!selected.empty()) {
        ignored = required;
        ignored.insert(optional.begin(), optional.end());
        std::set<std::string> requested;
        for (const auto &slug : lines(read_text(selected))) {
            if (!ignored.count(slug) || !requested.insert(slug).second)
                throw std::runtime_error("Unknown or duplicate selected release: " + slug);
        }
        if (!requested.count("boot"))
            throw std::runtime_error("Selected catalogue has no BOOT job");
        for (const auto &slug : requested) ignored.erase(slug);
        required = std::move(requested);
        optional.clear();
    }
    if(fs::exists(destination))throw std::runtime_error("Refusing snapshot overwrite");
    auto root=checked_catalogue(required,optional,ignored);
    auto *releases=field(root->object,"releases");
    auto *pending=field(root->object,"pending");
    if (!fs::create_directory(destination))
        throw std::runtime_error("Cannot create snapshot directory");
    chmod(destination.c_str(), 0755);
    try {
        write_text(destination / "optional-pending.json",
            std::string(json_object_to_json_string_ext(pending,JSON_C_TO_STRING_PLAIN))+"\n");
        json_object_object_foreach(releases, repository, release) {
            std::string name(repository);
            std::string json = json_object_to_json_string_ext(release, JSON_C_TO_STRING_PLAIN);
            write_text(destination / (name + ".json"), json + "\n");
            json_object *assets = field(release, "assets");
            for (size_t i = 0; i < json_object_array_length(assets); ++i) {
                auto *asset = json_object_array_get_idx(assets, i);
                std::string filename = string_field(asset, "name");
                if (filename.find("-native-installer.run") == std::string::npos &&
                    filename.find("-local-folder.run") == std::string::npos) continue;
                std::string digest = string_field(asset, "digest");
                write_text(destination / (name + ".sha256"),
                           digest.substr(7) + " " + filename + "\n");
            }
        }
    } catch (...) {
        fs::remove_all(destination);
        throw;
    }
    std::cout << "Validated one HTTPS release snapshot for " << json_object_object_length(releases)
              << " independent installations without GitHub REST requests\n";
}
int main(int argc, char **argv) {
    try {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        if (argc == 3 && std::string(argv[1]) == "--check-latest") {
            const auto tag = live_latest_tag(argv[2]);
            std::cout << tag << '\n';
            curl_global_cleanup();
            return 0;
        }
        if (argc != 5 && argc != 6)
            throw std::runtime_error("Usage: prepare-native-releases OUTPUT PROVIDERS FILESYSTEMS ALL_FILESYSTEMS [SELECTED]");
        prepare(argv[1], argv[2], argv[3], argv[4], argc == 6 ? fs::path(argv[5]) : fs::path{});
        curl_global_cleanup();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "InfiltratorOS native snapshot: " << error.what() << "\n";
        curl_global_cleanup();
        return 1;
    }
}
