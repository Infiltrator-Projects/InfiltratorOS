# Validation

## Local contracts

Run `sh tests/check.sh` on Linux. Optimised builds with NDEBUG exercise the production process and status modules using disposable fixtures. Checks remain active in release builds.

Process fixtures cover missing executables, failed setup, successful exec, an executable that subsequently fails, asynchronous worker handoff, closure of a parent descriptor above 4096, and startup with closed stdin. Status fixtures cover complete records, indeterminate progress, CRLF, unknown fields, invalid numbers, overflow, bounds, duplicate fields, embedded NUL, oversized lines and truncated records. Rejected records must leave output unchanged.

Defragmenter's integration worker is compiled with warnings-as-errors and checked for its published-release checksum validation, native -O3/LTO compiler policy, native package identity, Test Media and launcher contract. Built-package qualification requires the fourth worker, launcher and progress binary. Actual native build quality and integration on physical Linux/Mint hardware remain distinct from a hosted package-construction test.

## Package and release qualification

GitHub Actions builds all installed C programs with warnings as errors, builds the DEB twice and compares bytes, inspects control metadata and maintainer-script syntax, extracts the payload, verifies installed executable membership, and compares the installed PNG with approved source artwork. Source archive and commit identity accompany SHA-256 sealed artifacts.

Publication consumes only the successful build's sealed artifact. It verifies remote bytes while the release is a draft and again after publication. A draft can be repaired; public tags and assets cannot be replaced.

## Integration scope

These automated checks do not run the package's root maintainer scripts on the CI host. Real Debian/Mint qualification must inspect Plymouth during boot, reboot and shutdown; GRUB and initramfs updates; graphical/headless worker startup; APT lock contention; interrupted downloads/builds; local PGO; and replacement verification on supported hardware. Record distribution, kernel, desktop, package version and logs with that evidence.

The calendar/clock contract checks all 54 catalogue identities, validates stable three-asset release selection and digest rejection, verifies independent per-provider and per-run statuses, and tests the extracted native package record and module identity. Package construction embeds a fifteenth temporary worker and launcher/progress group while preserving the exhaustive permanent-payload allowlist. Provider repositories qualify the exact generic package and aggressive native installer before publishing.

Release qualification also runs `tests/temporal-bundle-e2e.py` on an ephemeral GitHub Actions host. The test extracts the exact integrity-checked worker from the built Debian maintainer script, installs a legacy ownership fixture at all 54 canonical module paths, invokes the real worker against the published releases, and verifies aggressive native package installation, provider metadata and all 54 dpkg ownership transfers. The OS release is published only after this full worker test passes. A distinct process installs each provider and verifies its own status and package ownership. The maintainer-script monitor checks all 58 statuses from the current run and only removes that run’s stage, never the shared /Temporary/InfiltratorOS directory.

## Independently released FAT modules

`tests/filesystem-install-test.py` validates the two-item catalogue, exact two-asset stable release contract, digest/URL/size fail-closed rules and independent per-run status tracking. InfiltratorOS must not claim success unless the matching module is installed under `/System/Filesystems`, owned by the format-specific Debian package, built aggressively with local CPU/LTO flags and authenticated by BUILD-INFO hash plus format-specific V1/V3/V4/defrag exports. The individual FAT repositories separately qualify actual native package installation and filesystem fixtures before publication. Full Defragmenter integration and physical-media and firmware qualification remain separate evidence gates.

## 1.0.38 full filesystem installation gate

Both independent FAT filesystem source releases must be at least 0.1.2 and carry their exact QEMU kernel module and OVMF UEFI driver inside the source-embedded native RUN and DEB. The worker checks the selected module's Debian ownership, ELF identity, EFI PE identity, tested KO kernel version, SHA-256 and the native host KO's `uname -r` vermagic. It installs matching kernel headers through APT before building the active native driver. CI uses an explicit restricted reference-only switch for its ephemeral runner, with kernel mount and firmware write tests performed separately by the source repositories. Unavailable kernel headers, unresolved symbols, missing EFI or kernel binaries, or wrong ABI mean installation failure, not partial success.
