// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "filesystem.h"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fstream>
#include <grp.h>
#include <infiltratr/filesystem_module_v2.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
namespace infiltrator_native::worker {
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
bool provider_current(const Entry &entry, const Asset &asset) {
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
            validate_common_descriptor(entry.resource, entry.id));
}

} // namespace infiltrator_native::worker
