# Architecture

## Ownership

`scripts/build-deb.sh` owns package membership and reads the root VERSION. It accepts only a numeric semantic version before constructing staging paths, requires the supported amd64 host, verifies the approved artwork, and uses SOURCE_DATE_EPOCH for deterministic Debian archives.

The 23 Mercedes-Benz/Daimler fonts are independently named, checked against a
single `assets/fonts/SHA256SUMS` manifest and seeded in the InfiltratorOS
package; the persistent native font cache is established by
`packaging/ensure-native-fonts.sh`. The three obsolete version-1.000 MB Corpo
files are not distributed. The font cache remains outside Debian package
ownership to avoid removal by unrelated application uninstalls.

The InfiltratorOS package owns establishment of the native filesystem skeleton documented in `FILESYSTEM.md`. The hierarchy is additive over the Debian/Linux base during migration: packaging creates the native roots and role directories, while subsystem code decides how individual capabilities are discovered and used. Named user directories are deliberately outside package ownership.

`stage-theme.c` stages the approved theme; `select-theme.c` owns system theme selection. Debian maintainer scripts own invocation during package installation and removal.

System Monitor, Calculator, Files and Defragmenter retain separate provisioning workers because their acquisition and installed-state qualification are product-specific. Each worker downloads the product's published native installer and invokes its explicit system-package mode instead of reproducing compiler flags, package layout or application-owned build metadata. The application repository therefore remains the single authority for native compilation, optimisation, packaging and installation; InfiltratorOS owns orchestration, application/global locking, graphical progress and post-install contract verification.

Replacement policy remains product-specific. System Monitor and Calculator may remove competing distribution applications only after the corresponding Infiltrator replacement has passed verification. Files is additive at this stage: its worker must not purge the existing distribution file manager or change the desktop's default file-manager association.

The operating system depends directly on the standard Calendar Resources capability pack. Calendar applications and System Settings consume those system resources rather than owning them, so application removal does not remove calendar implementations.

## Native executable ownership

`/Programs` is the sole native home for launchable applications. Whether an application ships with InfiltratorOS or is installed independently is a packaging concern, not a filesystem classification. `/System` is restricted to operating-system infrastructure, with `/System/Commands` owning genuine command helpers. InfiltratorOS establishes `/System/Commands:/Programs` for future login shells and does not create `/usr/bin` aliases for native applications.

## Shared native installation mechanics

`start-native-install.c` and `native-install-progress.c` compile once per product profile. `installer-profile.h` maps compile-time identity to fixed installed paths. Runtime arguments cannot select another privileged worker.

`installer-process.c` owns detached process lifetime. The parent reaps the first child and receives a close-on-exec pipe acknowledgement from the grandchild. Setup and exec failures return their errno; a successful exec acknowledges startup, independently of later worker success. Standard streams are redirected explicitly and inherited descriptors are closed. Progress windows drop to the discovered desktop user.

`installer-status.c` owns the read protocol: newline-terminated state, progress, stage and detail fields; bounded strings; progress from -1 through 100; no duplicated known fields. Invalid or incomplete records leave the caller's previous result unchanged. Worker writes use a temporary file and rename. Unknown fields allow additive protocol evolution.

Calculator's worker additionally treats desktop integration as part of the installed application contract. Success requires the canonical `calculator` package, executable `/Programs/calculator`, BUILD-INFO, the visible `net.ssmith.calculator.desktop` launcher with its native Exec path, the Calculator icon and AppStream metadata, the native PATH bridge and the shared MB Corpo resource contract. Only after that contract passes may obsolete Calculator package identities or competing distribution calculators be purged.

Defragmenter's additive worker downloads its immutable published local compiler and verifies SHA-256 against the release manifest before privileged execution. Its package builder owns compilation and native +native1 Debian identity; the OS worker supplies -O3, CPU-native and LTO flags, acquires the shared global build lock, and verifies the installed package, Test Media, filesystem services and desktop launchers. The Defragmenter package owns `/Programs/defragmenter` and `/Programs/defragmenter-test-media`, and locates private filesystem services in `/System/Filesystems/Defragmenter`, the native font bootstrap in `/System/Commands`, and product resources/documentation under `/System/Resources`; the old `/usr/lib/defragmenter` and `/usr/share/doc/defragmenter` trees must not remain. Two-pass PGO is not claimed for a build without a trained profile.

Files' worker treats its native layout and launcher as installed-state contracts without claiming replacement ownership. Success requires the canonical `files` package, executable `/Programs/files`, aggressive two-pass PGO BUILD-INFO, absence of the obsolete `/usr/bin/files` executable and a desktop launcher targeting `/Programs/files`. Existing file-manager packages and default-handler policy remain untouched.

## Failure and recovery

A launcher writes a complete failure record if worker startup fails. Progress UI failure is logged separately and does not stop an already running worker. A worker's terminal status and log remain available after its progress window closes. Installation recipes verify a replacement before removing distribution applications.

Desktop discovery currently selects the first eligible graphical user found in /proc. Multi-seat selection and physical boot qualification remain explicit integration concerns; neither is inferred from a package build.

## Shared icon resources

InfiltratorOS owns a verified icon payload seed in `/usr/lib/infiltratoros/icon-seed`.
The post-install helper installs every icon directly in `/System/Resources/Icons/`.
Filenames encode the source application, normal/night variant and original resource
name. The old `mercedes` directory is migrated only after every new copy is verified;
cleanup removes only unchanged seed files and empty directories. It preserves
custom files, locally modified legacy icons and the host icon theme, and does not
require any individual app to own or uninstall the shared artwork.
