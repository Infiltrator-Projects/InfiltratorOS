// SPDX-License-Identifier: GPL-3.0-or-later
// Exact installed-package path allowlist. Independent component binaries and
// their source trees are NEVER owned by this installer package.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace fs = std::filesystem;
using Names = std::set<std::string>;

static void demand(bool ok, const std::string &why) {
    if (!ok) throw std::runtime_error(why);
}
static std::string shell_quote(const std::string &s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}
static std::vector<std::string> command_lines(const std::string &command) {
    FILE *pipe = popen(command.c_str(), "r");
    demand(pipe != nullptr, "Cannot start package inspection command");
    std::string text;
    std::array<char, 32768> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe))
        text += buf.data();
    int status = pclose(pipe);
    demand(WIFEXITED(status) && WEXITSTATUS(status) == 0,
           "Package inspection command failed: " + command);
    std::istringstream in(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) lines.push_back(line);
    return lines;
}
static bool digest(const std::string &s) {
    if (s.size() != 64) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
static void add(Names &names, const std::string &item) {
    demand(!item.empty() && item[0] != '/' && item.find("..") == std::string::npos,
           "Unsafe installed pathname: " + item);
    demand(names.insert(item).second, "Duplicate installed pathname: " + item);
}
static Names expected() {
    Names names;
    const std::string command = "System/Commands/InfiltratorOS/";
    for (const char *name : {"activate-calendar", "activate-program-icons",
                              "ensure-native-fonts", "ensure-native-icons", "load-native-driver", "select-theme"})
        add(names, command + name);
    for (const char *name : {
             "etc/profile.d/90-infiltratoros-native-path.sh",
             "etc/udev/rules.d/81-infiltratoros-drivers.rules",
             "etc/xdg/autostart/infiltrator-calendar.desktop",
             "etc/xdg/autostart/infiltrator-program-icons.desktop",
             "System/Resources/Documentation/InfiltratorOS-fonts.sha256",
             "System/Resources/Documentation/InfiltratorOS-icons.sha256",
             "usr/share/plymouth/themes/infiltratoros/infiltratoros.plymouth",
             "usr/share/plymouth/themes/infiltratoros/infiltratoros.script",
             "usr/share/plymouth/themes/infiltratoros/infiltratoros.png"})
        add(names, name);
    size_t font_count = 0;
    for (const std::string &line : command_lines("cat assets/fonts/SHA256SUMS")) {
        demand(line.size() > 66 && digest(line.substr(0, 64)) &&
               line.substr(64, 2) == "  ", "Invalid font SHA-256 manifest");
        std::string name = line.substr(66);
        demand(name.size() >= 4 && name.substr(name.size()-4) == ".ttf" && name.find('/') == std::string::npos &&
               name[0] != '.', "Unsafe font filename");
        add(names, "System/Resources/Fonts/" + name);
        ++font_count;
    }
    demand(font_count == 23, "Expected exactly 23 font faces");
    const auto icons = command_lines(
        "cat assets/icons/mercedes-icons-v1.part?? | tar -xOJf - SHA256SUMS");
    demand(icons.size() == 1233, "Expected 1,233 canonical icon identities");
    for (const std::string &line : icons) {
        demand(line.size() > 66 && digest(line.substr(0, 64)) &&
               line.substr(64, 2) == "  ", "Invalid icon SHA-256 manifest");
        std::string name = line.substr(66);
        std::vector<std::string> bits;
        std::istringstream parts(name);
        std::string component;
        while (std::getline(parts, component, '/')) bits.push_back(component);
        const Names groups = {"adapter", "connect", "eco-coach", "finance",
                              "logbook", "remote-parking"};
        demand(bits.size() == 3 && groups.count(bits[0]) &&
               (bits[1] == "default" || bits[1] == "night") &&
               ((name.size() >= 4 && name.substr(name.size()-4) == ".png") ||
                (name.size() >= 5 && name.substr(name.size()-5) == ".webp")),
               "Unapproved icon identity: " + name);
        std::replace(name.begin(), name.end(), '/', '-');
        add(names, "System/Resources/Icons/" + name);
    }
    return names;
}
static Names actual(const fs::path &package) {
    Names names;
    for (const std::string &line : command_lines(
             "dpkg-deb -c " + shell_quote(package.string()))) {
        std::istringstream words(line);
        std::string permissions, owner, length, date, time, name;
        demand(static_cast<bool>(words >> permissions >> owner >> length >> date >> time >> name),
               "Invalid dpkg-deb file listing: " + line);
        if (permissions[0] == 'd') continue;
        demand(permissions[0] == '-', "Package includes a symlink or special file: " + name);
        demand(name.rfind("./", 0) == 0, "Package contains non-relative path: " + name);
        add(names, name.substr(2));
    }
    return names;
}
int main(int argc, char **argv) {
    try {
        demand(argc == 2, "Usage: native-package-layout-test PACKAGE.deb");
        const Names wanted = expected();
        const Names found = actual(fs::absolute(argv[1]));
        bool good = true;
        for (const auto &name : wanted) if (!found.count(name)) {
            std::cerr << "MISSING: " << name << '\n'; good = false;
        }
        for (const auto &name : found) if (!wanted.count(name)) {
            std::cerr << "UNAPPROVED LOCATION: " << name << '\n'; good = false;
        }
        demand(good, "Native package layout differs from approved files");
        std::cout << "InfiltratorOS layout: " << found.size()
                  << " approved persistent files, no installed worker executables\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
