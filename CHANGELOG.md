# Changelog

## 1.0.13

- Retire the one-shot native filesystem migration workflow and script so normal CI never edits, commits or pushes source back to `main`.
- Make `/Programs` the sole native namespace for user-launchable applications and reserve `/System` for operating-system infrastructure, with `/System/Commands` for genuine command helpers.
- Stop packaging `/System/Programs` and make package qualification fail if that directory is ever reintroduced; do not add upgrade-time cleanup logic for a path that should not exist.
- Add `/System/Commands:/Programs` to future login-shell PATHs without creating `/usr/bin` compatibility aliases.
- Require System Monitor provisioning to verify the canonical `/Programs/system-monitor` executable as well as the installed package.

## 1.0.12

- Make Calculator provisioning consume Calculator's own published native installer instead of maintaining a second source-download, PGO and Debian-build implementation inside InfiltratorOS.
- Verify the canonical `/Programs/calculator` executable rather than the retired `/usr/bin/calculator` path that caused a successful package installation to be reported as failed.
- Treat menu integration as part of successful installation: require the visible desktop launcher, Calculator icon, AppStream metadata, native PATH bridge, native BUILD-INFO and packaged MB Corpo fonts before declaring completion.
- Remove obsolete `infiltrator-calculator` and `infiltrator-calc` package identities, and distribution calculators, only after the canonical Calculator installation has passed verification.
- Read authoritative dpkg status instead of treating any `dpkg-query` database entry as an installed package.
- Add regression checks preventing the OS worker from reintroducing its own `dpkg-buildpackage`/source-bundle pipeline or the obsolete `/usr/bin/calculator` assumption.

## 1.0.11

- Let the published System Monitor native installer own its canonical optimisation profile in system-package mode instead of selecting that profile again in InfiltratorOS.

## 1.0.10

- Add the native `/System/Kernel` namespace with `Images`, `Modules` and `Firmware` directories.
- Keep the new kernel hierarchy structural only; do not alter `/boot`, Linux module trees, firmware locations or the running kernel.
- Qualify the kernel namespace in the built Debian package.

## 1.0.9

- Establish the first native InfiltratorOS filesystem skeleton at `/System`, `/Programs`, `/Users`, `/Volumes` and `/Temporary`.
- Add system role directories for Programs, Commands, Libraries, Devices, Filesystems, Preferences and shared Resources.
- Add resource directories for Calendars, Clocks, Fonts, Locales, Icons and Themes.
- Keep the native hierarchy additive over the Debian/Linux base and retain the earlier temporal discovery roots in parallel.
- Qualify every native directory in the built Debian package.

## 1.0.8

- Establish provisional system-wide temporal resource directories for discovered calendars and clocks.
- Qualify the built Debian package by verifying both temporal resource roots are present.
- Keep the installed path provisional while the resource representation and final hierarchy are still being proven.

## 1.0.7

- Resolve Calculator and System Monitor installers using their canonical package and command names.
- Derive installer filename prefix lengths from the canonical prefix.

## 1.0.6

- Give package identity one VERSION file and preserve immutable qualified release bytes.
- Share native launch and progress mechanics while retaining each product's installation policy.
- Report setup and exec failures before claiming background startup.
- Reject malformed or incomplete status records.
- Qualify process handoff and status failures with production-code fixtures; verify reproducible package bytes and approved artwork in CI.

## 1.0.5

- Use InfiltratorOS consistently in documentation, Plymouth metadata, package descriptions and provisioning windows.
- Document Boot, Filesystems, the planned Setup installer and the unchanged InfiltratorFS format name.
- Publish the infiltratoros Debian package and use infiltratoros consistently for installed paths and Plymouth files.
- Read the package version from VERSION and preserve immutable release tags and assets.
