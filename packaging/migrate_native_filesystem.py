#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one migration anchor in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


Path("packaging/infiltrator-native-path.sh").write_text(r'''# InfiltratorOS native PATH.
case ":$PATH:" in
  *:/Programs:*) ;;
  *) PATH="/Programs:$PATH" ;;
esac
case ":$PATH:" in
  *:/System/Commands:*) ;;
  *) PATH="/System/Commands:$PATH" ;;
esac
export PATH
''')

build = Path("scripts/build-deb.sh")
text = build.read_text()
text = text.replace('    "$SYSTEM_ROOT/Programs" \\\n', '', 1)
replace_anchor = '''    "$PKGROOT/DEBIAN" \\
    "$THEME_DIR" \\
    "$DIST" \\
'''
replace_new = '''    "$PKGROOT/DEBIAN" \\
    "$THEME_DIR" \\
    "$DIST" \\
    "$PKGROOT/etc/profile.d" \\
'''
if text.count(replace_anchor) != 1:
    raise SystemExit("InfiltratorOS package directory anchor missing")
text = text.replace(replace_anchor, replace_new, 1)
install_anchor = '''install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"\ninstall -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"\n'''
install_new = install_anchor + '''install -m 0644 "$ROOT/packaging/infiltrator-native-path.sh" \\
    "$PKGROOT/etc/profile.d/90-infiltratoros-native-path.sh"\n'''
if text.count(install_anchor) != 1:
    raise SystemExit("InfiltratorOS maintainer-script install anchor missing")
text = text.replace(install_anchor, install_new, 1)
if '$SYSTEM_ROOT/Programs' in text:
    raise SystemExit("obsolete /System/Programs still created by build-deb.sh")
build.write_text(text)

postinst = Path("debian/postinst")
text = postinst.read_text()
anchor = '''/usr/lib/infiltratoros/select-theme configure\n/usr/lib/infiltratoros/start-system-monitor-install\n'''
new = '''/usr/lib/infiltratoros/select-theme configure\n# Older native-skeleton releases created /System/Programs. Remove only the\n# obsolete empty directory; never delete user or package content recursively.\nrmdir /System/Programs 2>/dev/null || true\n/usr/lib/infiltratoros/start-system-monitor-install\n'''
if text.count(anchor) != 1:
    raise SystemExit("InfiltratorOS postinst anchor missing")
postinst.write_text(text.replace(anchor, new, 1))

replace_once(
    "packaging/install-calculator.c",
    'if (!installed("calculator") || access("/usr/bin/calculator", X_OK) != 0) {',
    'if (!installed("calculator") || access("/Programs/calculator", X_OK) != 0) {',
)

monitor = Path("packaging/install-system-monitor.c")
text = monitor.read_text()
anchor = '''    if (!installed("system-monitor"))\n        fail("native System Monitor package is not installed after the build");\n'''
new = '''    if (!installed("system-monitor") || access("/Programs/system-monitor", X_OK) != 0)\n        fail("native System Monitor package or /Programs executable is not installed after the build");\n'''
if text.count(anchor) != 1:
    raise SystemExit("System Monitor verification anchor missing")
monitor.write_text(text.replace(anchor, new, 1))

workflow = Path(".github/workflows/build-deb.yml")
text = workflow.read_text()
text = text.replace('            System/Programs \\\n', '', 1)
profile_anchor = '''          test -d "$RUNNER_TEMP/payload/usr/lib/infiltrator/temporal/clocks"\n'''
profile_new = profile_anchor + '''          test -f "$RUNNER_TEMP/payload/etc/profile.d/90-infiltratoros-native-path.sh"\n          test ! -e "$RUNNER_TEMP/payload/System/Programs"\n'''
if text.count(profile_anchor) != 1:
    raise SystemExit("InfiltratorOS package verification anchor missing")
workflow.write_text(text.replace(profile_anchor, profile_new, 1))

Path("VERSION").write_text("1.0.12\n")

changelog = Path("CHANGELOG.md")
text = changelog.read_text()
heading = "# Changelog\n"
entry = '''# Changelog\n\n## 1.0.12\n\n- Make `/Programs` the single native namespace for all user-launchable Infiltrator applications; remove the `/System/Programs` split.\n- Reserve `/System` for operating-system infrastructure and `/System/Commands` for genuine command helpers.\n- Add `/System/Commands:/Programs` to future login-shell PATHs without creating `/usr/bin` compatibility links.\n- Make the native Calculator and System Monitor provisioning workers verify `/Programs/calculator` and `/Programs/system-monitor`.\n- Remove an obsolete empty `/System/Programs` directory safely during upgrade and qualify its absence in the built package.\n'''
if heading not in text:
    raise SystemExit("InfiltratorOS changelog heading missing")
changelog.write_text(text.replace(heading, entry, 1))

filesystem = Path("docs/FILESYSTEM.md")
filesystem.write_text('''# Native filesystem skeleton\n\nInfiltratorOS defines a small, human-readable native filesystem model alongside the conventional Debian/Linux hierarchy during the transition period. The Linux base may continue to use `/bin`, `/usr`, `/etc`, `/var` and other compatibility paths, but new Infiltrator executables do not extend the old `/usr/bin` application namespace.\n\nThe native skeleton is:\n\n```text\n/\n├── System/\n│   ├── Commands/\n│   ├── Libraries/\n│   ├── Devices/\n│   ├── Filesystems/\n│   ├── Kernel/\n│   │   ├── Images/\n│   │   ├── Modules/\n│   │   └── Firmware/\n│   ├── Resources/\n│   │   ├── Calendars/\n│   │   ├── Clocks/\n│   │   ├── Fonts/\n│   │   ├── Locales/\n│   │   ├── Icons/\n│   │   └── Themes/\n│   └── Preferences/\n├── Programs/\n├── Users/\n├── Volumes/\n└── Temporary/\n```\n\n## Meaning\n\n`/Programs` is the one authoritative native namespace for user-launchable applications. The physical location does not change according to whether an application arrived with InfiltratorOS, from the Infiltrator repository, from a standalone Debian package, or from a local native build. Calculator, System Monitor and System Settings therefore all belong in `/Programs`.\n\n`/System` is reserved for operating-system capabilities and implementation components. `/System/Commands` is for genuine command helpers rather than GUI applications; `/System/Libraries`, `/System/Devices` and `/System/Filesystems` identify their corresponding infrastructure roles. There is deliberately no `/System/Programs`.\n\n`/System/Kernel` is the native namespace for kernel-owned material. `Images` is intended for kernel images and closely associated boot artefacts, `Modules` for loadable kernel modules, and `Firmware` for firmware consumed by kernel-managed hardware support. These directories remain structural placeholders: establishing them does not move, copy, link, mount over or otherwise alter the Linux kernel, `/boot`, distribution module trees or existing firmware locations.\n\n`/System/Resources` contains discoverable capabilities and shared presentation resources. Calendars and clocks belong under `Resources`, not under `Preferences`: a calendar or clock implementation is a capability, while selecting one is a preference. `/System/Preferences` is reserved for system-wide policy and configuration.\n\n`/Users` is the root of user-owned spaces, but the InfiltratorOS package creates no named user directory; account/setup machinery owns user lifecycle. `/Volumes` is the native mount/volume presentation root. `/Temporary` is the native temporary-storage concept.\n\n## Executable discovery\n\nThe native command search path is `/System/Commands:/Programs` ahead of the inherited host PATH. InfiltratorOS installs that policy through `/etc/profile.d/90-infiltratoros-native-path.sh`. Standalone Infiltrator application packages may establish the same two native entries independently so installation order does not matter. GUI desktop launchers use absolute `/Programs/...` paths and therefore do not depend on a new login session.\n\nNo native Infiltrator application is mirrored into `/usr/bin` merely for compatibility. The Linux base can keep its historical paths while the Infiltrator namespace grows independently.\n\n## Transitional status\n\nThe active Linux kernel remains in the distribution's existing boot/module/firmware locations until a future migration is explicitly designed and qualified. The current `/usr/lib/infiltrator/temporal` experiment is retained in parallel until temporal resources are migrated deliberately; it is not silently removed by establishing this hierarchy.\n\nThe hierarchy should stay small. New top-level directories require a distinct user-visible role rather than mirroring implementation detail.\n''')

architecture = Path("docs/ARCHITECTURE.md")
text = architecture.read_text()
section = '''\n\n## Native executable ownership\n\n`/Programs` is the sole native home for launchable applications. Whether an application ships with InfiltratorOS or is installed independently is a packaging concern, not a filesystem classification. `/System` is restricted to operating-system infrastructure, with `/System/Commands` owning genuine command helpers. InfiltratorOS establishes `/System/Commands:/Programs` for future login shells and does not create `/usr/bin` aliases for native applications.\n'''
if "## Native executable ownership" not in text:
    architecture.write_text((text.rstrip() + section).rstrip() + "\n")

readme = Path("README.md")
text = readme.read_text()
section = '''\n\n## Native application namespace\n\nInfiltratorOS uses one application namespace: `/Programs`. User-launchable Infiltrator applications install there regardless of whether they ship with the OS or are installed separately. `/System` is reserved for OS infrastructure and `/System/Commands` for genuine command helpers; `/System/Programs` is intentionally absent. The OS adds `/System/Commands:/Programs` to future login-shell PATHs and does not create native application aliases in `/usr/bin`.\n'''
if "## Native application namespace" not in text:
    readme.write_text((text.rstrip() + section).rstrip() + "\n")

for path in [build, workflow, filesystem]:
    if "System/Programs" in path.read_text() and path != filesystem:
        raise SystemExit(f"obsolete System/Programs remains in {path}")
