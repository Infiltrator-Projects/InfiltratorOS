# Architecture

## Ownership

`scripts/build-deb.sh` owns package membership and reads the root VERSION. It accepts only a numeric semantic version before constructing staging paths, requires the supported amd64 host, verifies the approved artwork, and uses SOURCE_DATE_EPOCH for deterministic Debian archives.

The 23 Mercedes-Benz/Daimler fonts are verified against
`assets/fonts/SHA256SUMS` and directly packaged under
`/System/Resources/Fonts`. The 1,233 flat icon files are checked during
build against the canonical archive checksum and installed directly under
`/System/Resources/Icons`. Their verification inventories are installed
in `/System/Resources/Documentation/InfiltratorOS`.
No seed directories, duplicate installed resources or persistent source archive
exist. The operating-system Debian package owns all these files; reinstalls
restore them, while independent application uninstallers do not remove them.

The InfiltratorOS package owns establishment of the native filesystem skeleton documented in `FILESYSTEM.md`. The hierarchy is additive over the Debian/Linux base during migration: packaging creates the native roots and role directories, while subsystem code decides how individual capabilities are discovered and used. Named user directories are deliberately outside package ownership.

`stage-theme.c` stages the approved theme; `select-theme.c` owns system theme selection. Debian maintainer scripts own invocation during package installation and removal.

System Monitor, Calculator, Files and Defragmenter retain separate provisioning workers because their acquisition and installed-state qualification are product-specific. Each worker downloads the product's independently published native installer. System Monitor, Calculator and Files use its explicit system-package mode; Defragmenter uses its standalone native installer and supplies the qualified CPU-native/LTO flags. The individual product repositories remain authoritative for package layouts. The application repository therefore remains the single authority for native compilation, optimisation, packaging and installation; InfiltratorOS owns orchestration, application/global locking, graphical progress and post-install contract verification.

Replacement policy remains product-specific. System Monitor and Calculator may remove competing distribution applications only after the corresponding Infiltrator replacement has passed verification. Files is additive at this stage: its worker must not purge the existing distribution file manager or change the desktop's default file-manager association.

The operating system provisions all 54 independently packaged calendar and clock resources from their published source installers. Calendar applications and System Settings consume those system resources rather than owning them, so application removal does not remove calendar implementations.

## Native executable ownership

`/Programs` is the sole native home for launchable applications. Whether an application ships with InfiltratorOS or is installed independently is a packaging concern, not a filesystem classification. `/System` is restricted to operating-system infrastructure, with `/System/Commands` owning genuine command helpers. InfiltratorOS establishes `/System/Commands:/Programs` for future login shells and does not create `/usr/bin` aliases for native applications.

## Shared native installation mechanics

`start-native-install.c` and `native-install-progress.c` compile once per product profile. `installer-profile.h` maps compile-time product identity to status and log paths. All fifteen installation workers are bundled inside the **single InfiltratorOS Debian post-installation script**, SHA-256 verified and unpacked under `/Temporary/InfiltratorOS/native-worker.*` for the duration of setup. They are NOT package-owned `/System` commands. The launcher accepts only the temporary installation runtime path established by `postinst`; runtime arguments cannot select another privileged product worker. A post-installation monitor removes that temporary directory when all workers report terminal status. All OS-owned temporary workers, downloads, status records, logs and locks live under `/Temporary/InfiltratorOS`. After all five installation groups succeed, the entire `/Temporary/InfiltratorOS` installer area is removed; failed installs retain diagnostic status and logs. Interrupted operations may leave temporary files, never permanent OS commands. Reinstalling InfiltratorOS runs the installation script again.

`installer-process.c` owns detached process lifetime. The parent reaps the first child and receives a close-on-exec pipe acknowledgement from the grandchild. Setup and exec failures return their errno; a successful exec acknowledges startup, independently of later worker success. Standard streams are redirected explicitly and inherited descriptors are closed. Progress windows drop to the discovered desktop user.

`installer-status.c` owns the read protocol: newline-terminated state, progress, stage and detail fields; bounded strings; progress from -1 through 100; no duplicated known fields. Invalid or incomplete records leave the caller's previous result unchanged. Worker writes use a temporary file and rename. Unknown fields allow additive protocol evolution.

Calculator's worker additionally treats desktop integration as part of the installed application contract. Success requires the canonical `calculator` package, executable `/Programs/calculator`, BUILD-INFO, the visible `net.ssmith.calculator.desktop` launcher with its native Exec path, the Calculator icon and AppStream metadata, the native PATH bridge and the shared MB Corpo resource contract. Only after that contract passes may obsolete Calculator package identities or competing distribution calculators be purged.

Defragmenter's additive worker downloads its immutable published local compiler and verifies SHA-256 against the release manifest before privileged execution. Its package builder owns compilation and native +native1 Debian identity; the OS worker supplies -O3, CPU-native and LTO flags, acquires the shared global build lock, and verifies the installed package, Test Media, filesystem services and desktop launchers. The Defragmenter package owns `/Programs/defragmenter` and `/Programs/defragmenter-test-media`, and locates private application workers under `/System/Commands/Defragmenter`, the native font bootstrap in `/System/Commands`, and product resources/documentation under `/System/Resources`; the old `/usr/lib/defragmenter` and `/usr/share/doc/defragmenter` trees must not remain. Two-pass PGO is not claimed for a build without a trained profile.

Files' worker treats its native layout and launcher as installed-state contracts without claiming replacement ownership. Success requires the canonical `files` package, executable `/Programs/files`, aggressive two-pass PGO BUILD-INFO, absence of the obsolete `/usr/bin/files` executable and a desktop launcher targeting `/Programs/files`. Existing file-manager packages and default-handler policy remain untouched.

## Failure and recovery

A launcher writes a complete failure record if worker startup fails. Progress UI failure is logged separately and does not stop an already running worker. A worker's terminal status and log remain available after its progress window closes. Installation recipes verify a replacement before removing distribution applications.

Desktop discovery currently selects the first eligible graphical user found in /proc. Multi-seat selection and physical boot qualification remain explicit integration concerns; neither is inferred from a package build.

## Package destination inventory

The Debian data payload contains **1,269 regular files**: five OS helpers
in `/System/Commands/InfiltratorOS`, 23 direct font files, 1,233 direct flat
icon files, two SHA-256 inventories in
`/System/Resources/Documentation/InfiltratorOS`, three Plymouth integration
files and three Linux compatibility files under `/etc`. The fifteen temporary
application setup executables are only in the Debian maintainer-script bundle,
not installed permanently. The applications and 54 temporal modules remain owned by their
independent packages. CI checks that no duplicate resource or `Seed` path
enters the Debian payload.

## Shared icon resources

Only the build process reassembles the verified source icon archive.
The generated Debian package contains the flat icon files directly under
`/System/Resources/Icons/`. The installation helper validates their digest
inventory, then removes only unchanged old nested `mercedes` icons and
empty legacy directories. Custom and modified legacy icons and the host
icon theme remain untouched.

## Native application icons

Nemo's raw executable file icons are per-user GVfs metadata, not an ELF or desktop-entry property. A Cinnamon-only autostart command (`activate-program-icons`) runs unprivileged in each logged-in user's session, reads the actual native app artwork under `/System/Resources/Icons`, and registers icon metadata for the `/Programs` launchables. The user never needs to run `gio` manually. It preserves preexisting custom-icon choices and uses installed theme icons where an application supplies no own native image. Newly installed programs are processed at the following Cinnamon login.

## Independent temporal provisioning

`install-temporal-resources.py` is embedded with the exact `temporal-resources.json` catalogue in the verified temporary worker bundle. It holds the same global native installation lock as System Monitor, installs build prerequisites after APT releases its transaction lock, then processes all 54 repositories. Each published release must have exactly its generic DEB, native RUN and source ZIP; the worker verifies the RUN size and GitHub-provided SHA-256 before executing its explicit aggressive system-package mode. Each source installer owns compilation, PGO training, tests and native package creation. The OS then verifies package identity/version, native build record, module hash and provider ABI identity. A failed provider is logged while the remaining providers are attempted, and any failure leaves the group failed with diagnostic logs retained. No provider installer becomes a persistent OS command.
