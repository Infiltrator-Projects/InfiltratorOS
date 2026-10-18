# InfiltratorOS documentation

The root [README](../README.md) is the product overview. This index separates maintained contracts from user-facing release history.

## Product and shared standards

- [Product family and UI contract](PRODUCT-FAMILY.md) — consistent identity, typography, palettes, UX and independent installation across InfiltratorOS, System Monitor, Calculator and Files.
- [Engineering philosophy](PHILOSOPHY.md) — project-specific obligations under [Libraries/Common philosophy](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md).
- [Architecture](ARCHITECTURE.md) — native integration and ownership.
- [Native filesystem](FILESYSTEM.md) — /System, /Programs and shared resources.
- [Validation](VALIDATION.md) — actual build and qualification evidence.
- [Changelog](../CHANGELOG.md) — release and implementation history.

## Documentation ownership

One document owns each maintained contract. Architecture explains *where* behaviour lives; philosophy explains *why*; validation explains *how it is proved*. Source, tests and published release artefacts remain the authority for present implementation, not aspirational descriptions. Changes affecting a contract update its owning document in the same commit.
