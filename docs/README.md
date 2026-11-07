# InfiltratorOS documentation

The root [README](../README.md) is the product overview. This index separates current-state technical contracts from historical release evidence.

[**Current bottom-up boot-to-desktop development status**](BOOT-TO-DESKTOP-STATUS.md) — durable checkpoints, verified BOOT release, preserved Kernel build baseline, next qualification gate and exact resume instructions.

## Product and shared standards

- [Product family and UI contract](PRODUCT-FAMILY.md) — consistent identity, typography, palettes, UX and independent installation across InfiltratorOS, System Monitor, Calculator and Files.
- [Engineering philosophy](PHILOSOPHY.md) — project-specific obligations under [Libraries/Common philosophy](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md).
- [Architecture](ARCHITECTURE.md) — native integration and ownership.
- [Native filesystem](FILESYSTEM.md) — `/System`, `/Programs` and shared resources.
- [Unified filesystem runtime target](FILESYSTEM_SO_RUNTIME_CONTRACT.md) — planned single `.so` consumed by BOOT, kernel and applications; not yet implemented.
- [Desktop-first philosophy addendum](DESKTOP-FIRST-PHILOSOPHY-ADDENDUM.md) — graphical-operation requirements and current transition.
- [Validation](VALIDATION.md) — actual build and qualification evidence.
- [Typography and 23-font roles](FONT-ROLES.md) — audited font families and recommended per-app hierarchy.
- [Changelog](../CHANGELOG.md) — concise release milestones, with full details preserved in immutable tags.
- [Four-repository alignment review (18 October 2026)](ALIGNMENT-REVIEW-2026-10-18.md) — historical snapshot; verify outstanding observations again against current source.

## Documentation ownership

One document owns each maintained contract. Architecture explains *where* behaviour lives; philosophy explains *why*; validation explains *how it is proved*. Source, tests and published release artefacts remain the authority for present implementation, not aspirational descriptions. Changes affecting a contract update its owning document in the same commit.
