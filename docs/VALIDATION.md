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
