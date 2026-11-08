// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the production native C++ installation worker without downloading
// or invoking independent component builds. All tests are non-destructive.
#define main native_component_worker_entrypoint
#include "../packaging/native-component-worker.cpp"
#undef main
#include <functional>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(std::string("Native worker contract: ") + message);
}
static void rejects(const std::function<void()> &action, const char *description) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(std::string("Native worker accepted invalid ") + description);
}
static void add_asset(json_object *assets, const std::string &name, const std::string &url,
                      const std::string &hash = std::string(64, 'a')) {
    json_object *asset = json_object_new_object();
    json_object_object_add(asset, "name", json_object_new_string(name.c_str()));
    json_object_object_add(asset, "browser_download_url", json_object_new_string(url.c_str()));
    json_object_object_add(asset, "digest", json_object_new_string(("sha256:" + hash).c_str()));
    json_object_object_add(asset, "size", json_object_new_int64(123));
    json_object_array_add(assets, asset);
}
static Document sample_release(const Entry &e, const std::string &version, bool temporal) {
    auto *data = json_object_new_object();
    json_object_object_add(data, "draft", json_object_new_boolean(0));
    json_object_object_add(data, "prerelease", json_object_new_boolean(0));
    json_object_object_add(data, "tag_name", json_object_new_string(("v" + version).c_str()));
    json_object_object_add(data, "target_commitish",
                           json_object_new_string(std::string(40, 'b').c_str()));
    auto *assets = json_object_new_array();
    const auto prefix = "https://github.com/Infiltrator-Projects/" + e.repo +
                        "/releases/download/v" + version + "/";
    const std::string run = e.slug() + "-" + version + "-native-installer.run";
    const std::string deb = e.package + "_" + version + "_amd64.deb";
    add_asset(assets, run, prefix + run);
    add_asset(assets, deb, prefix + deb);
    if (temporal) {
        const auto zip = e.slug() + "-" + version + "-source.zip";
        add_asset(assets, zip, prefix + zip);
    }
    json_object_object_add(data, "assets", assets);
    return Document(data);
}
int main() {
    try {
        auto temporal = catalogue("packaging/temporal-resources.json", 54);
        auto established = catalogue("packaging/filesystem-resources.json", 5);
        auto additional = catalogue("packaging/filesystem-additional.json", 18);
        check(temporal.size() == 54, "temporal cardinality");
        check(established.size() == 5, "established cardinality");
        check(additional.size() == 18, "additional cardinality");
        check(established[0].slug() == "filesystem-fat12", "FAT12 identity");
        check(application("system-monitor").repo == "System-Monitor", "system monitor identity");
        check(application("defragmenter").resource == "/Programs/defragmenter",
              "defragmenter destination");
        // A missing optional Defragmenter field must never invalidate other apps.
        Asset app_version;
        app_version.version = "1.0.243";
        check(application_package_matches(application("system-monitor"), app_version,
            "install ok installed\t1.0.243"), "System Monitor Debian identity");
        check(application_package_matches(application("system-monitor"), app_version,
            "install ok installed\t1.0.243\t"), "trailing empty optional field");
        check(!application_package_matches(application("system-monitor"), app_version,
            "install ok installed\t1.0.242"), "reject stale application version");
        check(!application_package_matches(application("system-monitor"), app_version,
            "deinstall ok config-files\t1.0.243"), "reject uninstalled package");
        app_version.version = "0.2.66";
        check(application_package_matches(application("calculator"), app_version,
            "install ok installed\t0.2.66"), "Calculator native package identity");
        check(!application_package_matches(application("files"), app_version,
            "install ok installed\t0.2.66"), "reject generic Files package");
        check(application_package_matches(application("files"), app_version,
            "install ok installed\t0.2.66+native1"), "Files trained native suffix");
        check(application_package_matches(application("defragmenter"), app_version,
            "install ok installed\t0.2.66+native1"), "Defragmenter trained native suffix");

        rejects([&]() { (void)application("unrecognised"); }, "application");

        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto asset = release_asset(temporal[0], release.ptr, true, false);
            check(asset.size == 123 && asset.sha == std::string(64, 'a'),
                  "temporal digest and size");
            json_object_object_add(release.ptr, "draft", json_object_new_boolean(1));
            rejects([&]() { (void)release_asset(temporal[0], release.ptr, true, false); },
                    "draft release");
        }
        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto *assets = field(release.ptr, "assets");
            json_object *run = json_object_array_get_idx(assets, 0);
            json_object_object_add(run, "digest", json_object_new_string("sha256:INVALID"));
            rejects([&]() { (void)release_asset(temporal[0], release.ptr, true, false); },
                    "asset SHA-256");
        }
        {
            auto release = sample_release(temporal[0], "1.0.5", true);
            auto *assets = field(release.ptr, "assets");
            json_object *run = json_object_array_get_idx(assets, 0);
            json_object_object_add(run, "browser_download_url",
                                   json_object_new_string("https://attacker.example/script.run"));
            rejects([&]() { (void)release_asset(temporal[0], release.ptr, true, false); },
                    "asset owner");
        }
        {
            auto release = sample_release(established[0], "0.1.14", false);
            auto asset = release_asset(established[0], release.ptr, false, true);
            check(asset.version == "0.1.14", "filesystem version");
        }
        {
            auto it = std::find_if(established.begin(), established.end(),
                         [](const Entry &e) { return e.id == "exfat"; });
            check(it != established.end(), "exFAT manifest identity");
            auto release = sample_release(*it, "0.1.2", false);
            rejects([&]() { (void)release_asset(*it, release.ptr, false, true); },
                    "known-broken exFAT release");
        }
        {
            setenv("INFILTRATOROS_SELECTED_FILESYSTEMS", "not-a-filesystem", 1);
            rejects([&]() { list(established, 5, "INFILTRATOROS_SELECTED_FILESYSTEMS", true); },
                    "filesystem selection");
            unsetenv("INFILTRATOROS_SELECTED_FILESYSTEMS");
        }
        {
            std::string base = "/tmp/infiltratoros-native-unit-XXXXXX";
            std::vector<char> writable(base.begin(), base.end());
            writable.push_back(0);
            char *name = mkdtemp(writable.data());
            if (!name) throw std::runtime_error("Cannot create disposable test directory");
            fs::path path(name);
            Status status(path, "test-driver");
            status.write("queued", 2, "Waiting", "Native status");
            check(read_file(path / "test-driver-install.status").find("state=queued\n") == 0,
                  "atomic status contract");
            save(path / "sha.txt", "abc");
            check(digest(path / "sha.txt") ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                  "OpenSSL SHA-256 contract");
            fs::remove_all(path);
        }
        std::cout << "Native C++ provider contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
