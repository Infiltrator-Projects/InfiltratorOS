// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOROS_NATIVE_WORKER_RELEASE_H
#define INFILTRATOROS_NATIVE_WORKER_RELEASE_H
#include "runtime.h"
#include <json-c/json.h>
#include <set>
namespace infiltrator_native::worker {
struct Document {
    json_object *ptr = nullptr;
    explicit Document(json_object *obj) : ptr(obj) {
        if (!ptr) throw std::runtime_error("Invalid JSON");
    }
    Document(Document &&other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
    Document(const Document &) = delete;
    ~Document() { if (ptr) json_object_put(ptr); }
};

Document parse(const std::string &data);
json_object *field(json_object *object, const char *name);
std::string str(json_object *object, const char *name);
struct Entry {
    std::string id, name, repo, package, resource, kind;
    std::string slug() const { return lowercase(repo); }
};

struct Asset {
    std::string version, url, sha;
    size_t size = 0;
    std::string source_revision;
};

std::vector<Entry> catalogue(const fs::path &path, std::size_t expected = 0);
void list(const std::vector<Entry> &entries, std::size_t count, const char *selector, bool by_id);
Asset release_asset(const Entry &entry, json_object *meta, bool calendar_or_clock, bool filesystem);
Document release(const Entry &entry);
void download(const Asset &asset, const fs::path &target);
Entry application(const std::string &slug);
void discover(const std::vector<Entry> &extra, const fs::path &snapshot);

} // namespace infiltrator_native::worker
#endif
