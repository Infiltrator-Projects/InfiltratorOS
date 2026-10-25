# InfiltratorOS product-family contract

This is the family-level integration and presentation contract for [InfiltratorOS](../README.md), [System Monitor](https://github.com/Infiltrator-Projects/System-Monitor), [Calculator](https://github.com/Infiltrator-Projects/Calculator) [Files](https://github.com/Infiltrator-Projects/Files) and [Defragmenter](https://github.com/Infiltrator-Projects/Defragmenter). It documents the *current design authority*, not an assertion that all planned OS capabilities are implemented.

## One family, independently installable products

All five products identify as members of **InfiltratorOS** in their GitHub overview, help/about information and user-facing identity where appropriate. Each application remains useful when installed alone on a supported host. Installing InfiltratorOS may provision applications, but an application may not require the InfiltratorOS package merely to start or perform its own ordinary work.

Application names remain **System Monitor**, **Calculator**, **Files** and **Defragmenter**. Package and executable identities remain product-owned; Linux user-facing executables belong in `/Programs`. OS infrastructure and durable shared capabilities belong in `/System`, not private application directories. Linux compatibility metadata (desktop entries, packaging files) can remain in conventional host paths.

## Authoritative standards, not copied implementations

- [Libraries/Common engineering philosophy](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md) owns the engineering benchmark: trustworthy behaviour, proportional complexity, restrained visual design, recovery, precise documentation and qualification.
- [Libraries/Common `infiltratr/design.h`](https://github.com/Infiltrator-Projects/Libraries/blob/main/include/infiltratr/design.h) owns **System / Day / Night** mode semantics, semantic colour roles, design metrics and typography identity. Applications consume their pinned Common contract rather than maintaining rival palettes.
- **MB Corpo S** (regular/bold) is for ordinary interface text, controls and emphasis; **MB Corpo A condensed** is for deliberate product display/brand text. Required font files must actually be available on the target; product UI must not *intentionally* substitute unrelated families silently. A standalone install must bootstrap or package the required verified font resources without making the OS package a dependency. Other Daimler/MB faces distributed by InfiltratorOS do not silently replace these three UI roles.
- Platform-specific adapters may render GTK3, GTK4, native Windows or other qualified widgets. They must preserve the same roles, contrast, keyboard and focus behaviour, density discipline and native interaction semantics. Product-specific data visualisation colours are permissible when meaningful; generic surfaces, controls, states and text must use Common roles.
- **System** follows the host appearance but still resolves through the Common Day/Night palette. Explicit Day/Night choice and persistence must not be redefined by an unrelated application.
- The consistent visual signature is calm, engineered and concise: clear navigation and content hierarchy, restrained accent/focus use, legible values, quiet chrome, and progressive disclosure. A calculator keypad, process monitor and file browser are *not* required to have identical arrangements.

## UI acceptance

For every app, compare a real Day and Night window with its siblings. Verify title, body, bold/selected states, modal and context surfaces, toolbar/sidebar/navigation, buttons, hover/focus, disabled/failed/warning/success states, density, resizing, keyboard access and visibility of long labels. A screenshot is visual evidence, not a substitute for accessibility and behaviour tests. Verify a standalone installation as well as installation under InfiltratorOS. Avoid claiming parity from mere consumption of a shared header.

The InfiltratorOS graphical native-install progress window is also part of this family surface; its eventual Common-aligned typography, colours and control treatment are an explicit integration requirement, not a licence to copy independent palette literals.

## Repository and documentation interface

Every family repository has a root `README.md` for purpose, identity, build/installation and a family navigation link; `docs/README.md` indexes maintained technical documents. These stable names have distinct roles: `docs/PHILOSOPHY.md` points to Common philosophy, `docs/ARCHITECTURE.md` records ownership and boundaries, `docs/VALIDATION.md` records qualification. Domain-specific DESIGN, decision, release and other documents remain separate where genuinely useful. Maintained claims must not conflict with executable behaviour; historical snapshots must be labelled historical.

Do **not** forcibly homogenise languages, native toolkits, build systems, VERSION file locations or specialist directories. System Monitor owns `support/VERSION`; InfiltratorOS, Calculator and Files currently own root `VERSION`. These are valid single-authority variations. Preserve first-party Common gitlinks and their exact revisions; do not vendor a second Common copy merely for visual consistency.

## Existing product-specific responsibilities

- **InfiltratorOS**: OS integration, Plymouth and shared native filesystem/resources, application provisioning. It does not yet provide the planned complete Setup installer.
- **System Monitor**: hardware, performance, process and system inspection/management.
- **Calculator**: exact/shared mathematical and interaction semantics across native Linux, Windows and iPhone shells.
- **Files**: namespace browsing, file operations, journalling and recovery; filesystem-specific capabilities are discovered, not assumed, and a separate filesystem remains authoritative for its own persistent format.
- **Defragmenter**: native filesystem allocation analysis, offline relocation and recovery; its independent Debian package owns `/Programs/defragmenter` and `/Programs/defragmenter-test-media`, with private filesystem workers in `/System/Filesystems/Defragmenter` and its product resources and documentation under `/System/Resources`.

An attractive shared appearance is insufficient if it sacrifices safety, performance, capabilities or independent installation. Product logic stays within its owning repository; new product-neutral mechanisms belong in Libraries/Common after their abstraction is justified.
