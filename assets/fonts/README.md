# Mercedes-Benz and Daimler font resources

This directory contains 23 SHA-256-distinct MB Corpo and Daimler TTF files. The
authoritative SHA-256 manifest is [SHA256SUMS](SHA256SUMS). InfiltratorOS verifies
every listed binary during packaging, seeds all 23 into
`/usr/lib/infiltratoros/font-seed`, and installs them as persistent native
resources under `/System/Resources/Fonts` using one shared installer.

Three earlier 1.000 MB Corpo builds were removed in favour of the newer 1.010
counterparts:

- `mb_corpo_a_cond_regular.ttf` -> `mb_corpo_a_title_cond_regular.ttf`
- `mb_corpo_s_bold.ttf` -> `mb_corpo_s_title_bold.ttf`
- `mb_corpo_s_regular.ttf` -> `mb_corpo_s_title_regular.ttf`

Each pair has identical glyph, shaping and metrics tables. Only the font
header and naming metadata differ: the newer builds identify as Version 1.010
and refer to Mercedes-Benz Group AG instead of Daimler AG. Filenames reflect
Android application resource naming; all six internally identify as Title
faces. The newer files were also present in the supplied Mercedes apps.

Daimler CAC and CS `_app` faces have distinct binaries and internal family
names and are retained separately. No Roboto or Liberation Sans fonts are
bundled in this font collection.

This repository does not delete persistent native font files on uninstall.
Other independent Infiltrator applications may still reference the older
three filenames; updating their font contracts is a separate change.

The audited internal family names and application-specific selections are documented in [docs/FONT-ROLES.md](../../docs/FONT-ROLES.md).
