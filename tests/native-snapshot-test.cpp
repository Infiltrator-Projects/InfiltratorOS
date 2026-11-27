// SPDX-License-Identifier: GPL-3.0-or-later
#define main native_release_snapshot_entrypoint
#include "../packaging/native-release-snapshot.cpp"
#undef main

static void check(bool condition) {
    if (!condition) throw std::runtime_error("Native snapshot subset regression");
}
static std::unique_ptr<Json> fixture() {
    auto result = std::make_unique<Json>(json_object_new_object());
    json_object_object_add(result->object, "schema", json_object_new_int(1));
    auto *releases = json_object_new_object();
    json_object_object_add(result->object, "releases", releases);
    json_object_object_add(result->object, "pending", json_object_new_object());
    auto *boot = json_object_new_object();
    json_object_object_add(boot, "draft", json_object_new_boolean(false));
    json_object_object_add(boot, "prerelease", json_object_new_boolean(false));
    json_object_object_add(boot, "tag_name", json_object_new_string("v1.0.1"));
    auto *assets = json_object_new_array();
    json_object_object_add(boot, "assets", assets);
    auto *asset = json_object_new_object();
    json_object_object_add(asset, "name", json_object_new_string("boot-1.0.1-native-installer.run"));
    json_object_object_add(asset, "browser_download_url", json_object_new_string(
        "https://github.com/Infiltrator-Projects/Boot/releases/download/v1.0.1/boot-1.0.1-native-installer.run"));
    json_object_object_add(asset, "digest", json_object_new_string(("sha256:" + std::string(64, 'a')).c_str()));
    json_object_object_add(asset, "size", json_object_new_int(123));
    json_object_array_add(assets, asset);
    json_object_object_add(releases, "Boot", boot);
    json_object_object_add(releases, "Filesystem-EFS", json_object_new_object());
    json_object_object_add(field(result->object, "pending"), "Filesystem-FAT12", json_object_new_string("pending"));
    json_object_object_add(field(result->object, "pending"), "Filesystem-BFS", json_object_new_string("new"));
    return result;
}
int main() {
    const std::string prefix = "https://github.com/Infiltrator-Projects/Boot/releases/tag/";
    check(verified_redirect_tag(prefix, 302, prefix + "v1.2.3") == "v1.2.3");
    check(verified_redirect_tag(prefix, 200, prefix + "v1.2.3").empty());
    check(verified_redirect_tag(prefix, 302, prefix + "v1.2.3?other").empty());
    check(verified_redirect_tag(prefix, 302, "https://example.invalid/v1.2.3").empty());
    const std::set<std::string> required{"boot"}, optional{"filesystem-fat12"};
    auto raw = fixture();
    auto selected = selected_snapshot(raw->object, required, optional);
    auto reduced = fixture();
    json_object_object_add(field(reduced->object, "releases"), "Calculator", json_object_new_object());
    json_object_object_add(field(reduced->object, "pending"), "Clock-Binary", json_object_new_string("not published"));
    auto subset = selected_snapshot(reduced->object, {"boot"}, {}, {"calculator", "clock-binary"});
    check(json_object_object_length(field(subset->object, "releases")) == 1);
    check(json_object_object_length(field(subset->object, "pending")) == 0);
    check(json_object_object_length(field(selected->object, "releases")) == 1);
    check(json_object_object_length(field(selected->object, "pending")) == 1);
    check(field(field(selected->object, "releases"), "Boot") != nullptr);
    check(field(field(selected->object, "pending"), "Filesystem-FAT12") != nullptr);
    auto rejects = [&](auto mutate) {
        auto invalid = fixture();
        mutate(invalid->object);
        bool rejected = false;
        try { (void)selected_snapshot(invalid->object, required, optional); }
        catch (const std::exception &) { rejected = true; }
        check(rejected);
    };
    rejects([](auto *root) { json_object_object_del(field(root, "releases"), "Boot"); });
    rejects([](auto *root) { json_object_object_del(field(root, "pending"), "Filesystem-FAT12"); });
    rejects([](auto *root) { json_object_object_add(field(root, "pending"), "filesystem-efs", json_object_new_string("duplicate")); });
    rejects([](auto *root) { json_object_object_add(field(root, "releases"), "../Filesystem-X", json_object_new_object()); });
    rejects([](auto *root) { json_object_object_add(field(root, "releases"), "Unknown-App", json_object_new_object()); });
    rejects([](auto *root) {
        json_object_object_del(field(root, "releases"), "Boot");
        json_object_object_add(field(root, "pending"), "Boot", json_object_new_string("not available"));
    });
    std::cout << "Native release snapshot subset contracts passed\n";
}
