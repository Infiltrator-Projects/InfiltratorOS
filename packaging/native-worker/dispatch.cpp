// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "dispatch.h"
#include "filesystem.h"
#include "isolation.h"
#include "install.h"
#include "../native-build-queue.h"
#include <algorithm>
#include <curl/curl.h>
#include <iostream>
#include <string_view>
namespace infiltrator_native::worker {
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
            if (argc == 3 && std::string(argv[1]) == "--discover-additional") {
                discover(extra, argv[2]);
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
