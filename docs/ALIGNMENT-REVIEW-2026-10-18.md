# Four-repository family alignment review — 2026-10-18

**Scope:** [InfiltratorOS](https://github.com/Infiltrator-Projects/InfiltratorOS), [System Monitor](https://github.com/Infiltrator-Projects/System-Monitor), [Calculator](https://github.com/Infiltrator-Projects/Calculator) and [Files](https://github.com/Infiltrator-Projects/Files), observed on each repository's `main`. This is dated audit evidence, not a replacement for the maintained [product-family contract](PRODUCT-FAMILY.md) or a claim of fully qualified UI parity.

## Repository and documentation comparison

| Property | InfiltratorOS | System Monitor | Calculator | Files |
| --- | --- | --- | --- | --- |
| Main README | Root | Moved from `.github/` to root | Root | Root |
| Technical index | `docs/README.md` | `docs/README.md` | `docs/README.md` | `docs/README.md` |
| Engineering philosophy | `docs/PHILOSOPHY.md` | Same | Same | Same |
| Architecture/validation | `docs/ARCHITECTURE.md`, `docs/VALIDATION.md` | Same | Same | Same |
| Version source | `VERSION` | `support/VERSION` | `VERSION` | `VERSION` |
| Application scope | OS package and graphical provisioning | C17 GTK3, Windows preview | C++17 GTK4, Win32, SwiftUI | C++20 GTK4 |
| Shared Common gitlink | Not an application gitlink | `src/infiltratr-common` | Same | Same |

Those differences in version location, build language and UI toolkit are legitimate ownership boundaries, not reasons to create aliases or duplicate build mechanisms. Documentation layout is aligned without flattening meaningful product-specific architecture.

## UI/source findings

**Already aligned in implementation:**

- System Monitor `src/app_theme.c`, Calculator `src/ui/calculator_theme.hpp`, and Files `src/ui/theme_controller.cpp` use `infiltratr_theme_resolve` and Common typography/design metrics.
- System Monitor and Calculator pin Common revision `2dd9a36a3dba1d069c59ad6d10e697253a91ebca` (Common 1.19.51); Files pins `70452491dae6d0f9f75c7099292f624db08648cc` (Common 1.19.56). `src/design.c` has identical Git blob SHA `9414ac434645680ed15a04e78adeffa4f0e4e91a` at both pins. There is **no evidence of a palette-definition difference solely from those pins**.
- Files has a scoped GTK4 theme provider, permitting simultaneously displayed Day and Night windows. Its brand-title CSS previously forced weight 600; it now requests Common's actual brand weight.

**Remaining confirmed inconsistencies:**

1. **Font identity is not yet unified.** InfiltratorOS installs 23 newer committed MB/Daimler TTFs and identifies `mb_corpo_a_title_cond_regular.ttf`, `mb_corpo_s_title_bold.ttf`, and `mb_corpo_s_title_regular.ttf` as UI-role files. Libraries/Common's `src/design.c`, `cmake/InfiltratrEnsureNativeFonts.sh` and `InfiltratrTypographyAssets.cmake` still declare the older three-file `mb_corpo_a_cond_regular.ttf`, `mb_corpo_s_bold.ttf`, `mb_corpo_s_regular.ttf` archive contract. Exact filenames, provenance, installer verification and independently installed applications must move together. Do not create silent duplicate aliases or downgrade verified font bytes merely to satisfy an old filename check.
2. **Standalone Files typography has a provisioning gap.** Files' `CMakeLists.txt` currently installs the executable, launcher and BUILD-INFO but no canonical font bootstrap; the GTK theme applies Common's `gtk_fallback` for normal UI text. Standalone installation must verify/provide the desired fonts before a strict no-substitution rendering rule can be enforced safely.
3. **InfiltratorOS graphical installer chrome is not yet the Common visual system.** `packaging/native-install-progress.c` currently constructs a GTK3 progress window with native defaults, not Common's semantic Day/Night palette and canonical typography. Do not patch that with hard-coded copied RGB values: bring the installer under the one owned design contract through a tested integration.
4. **Full visual consistency is not proven by source checks.** System Monitor's presentation includes product-specific telemetry colours and custom Day overrides; Calculator has three native platform shells; Files has its own GTK4 browser composition. Verify actual side-by-side Day/Night screenshots, contrast, font resolution, focus/hover/disabled states, dialogs, clipping, resizing and keyboard workflows before declaring those surfaces visually aligned.

## Done during this pass

- Root product-family navigation and `docs/README.md` indexes added/normalised across the four repositories.
- One authoritative family/UI contract published in InfiltratorOS, referenced by the application repos rather than copied.
- System Monitor's root README, source-layout checker, packaged README and Doxygen main-page paths aligned, with a versioned verification rebuild.
- Files' MB Corpo brand-title weight aligned with Common instead of a local synthetic weight.
- InfiltratorOS native font-layout documentation corrected to describe the actual 23-file manifest and current role names.
- Calculator's stale hard-coded Common version description replaced by the authoritative checked gitlink.

## Qualification boundary

These commits do **not** certify the four actual screens as already visually identical, and do not solve the old/new MB font archive contract. Building after a documentation change validates repository health; it does not test real cross-product GUI parity. Maintain exact independent installation, native `/Programs` paths, current user choices and actual product capabilities while completing the remaining design integration.
