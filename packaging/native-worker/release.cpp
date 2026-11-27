// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "release.h"
#include "../native-build-queue.h"
#include <cstdio>
#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
namespace infiltrator_native::worker {
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
std::vector<Entry> catalogue(const fs::path &path, size_t expected) {
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
static constexpr const char *MINIMUM_BOOT_RELEASE = "0.1.50";
// A published stable release is required; release classification alone does not
// establish qualification for physical UEFI boot.

Asset release_asset(const Entry &entry, json_object *meta, bool calendar_or_clock,
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
Document release(const Entry &entry) {
    const std::string directory=environment("INFILTRATOROS_RELEASE_DIR");
    if(!directory.empty())
        return parse(read_file(fs::path(directory)/(entry.repo+".json")));
    // Standalone worker invocations still use authenticated publisher
    // metadata. No runtime REST request or GitHub account is required.
    Document catalogue=parse(fetch("https://raw.githubusercontent.com/Infiltrator-Projects/Infiltrator-Repository/main/catalogue/native-installers-v4.json"));
    auto *meta=field(field(catalogue.ptr,"releases"),entry.repo.c_str());
    return parse(json_object_to_json_string_ext(meta,JSON_C_TO_STRING_PLAIN));
}
void download(const Asset &asset,const fs::path &target) {
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
Entry application(const std::string &slug) {
    for (const auto &contract : infiltrator_native::application_contracts) {
        if (contract.slug != slug) continue;
        return {slug, std::string(contract.repository), std::string(contract.repository),
                std::string(contract.package), std::string(contract.resource), "application"};
    }
    throw std::runtime_error("Unknown native application");
}
void discover(const std::vector<Entry> &extra, const fs::path &snapshot) {
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

} // namespace infiltrator_native::worker
