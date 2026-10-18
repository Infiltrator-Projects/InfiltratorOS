# Mercedes-Benz icon resource seed

This catalogue was extracted from six supplied Mercedes Android APK/APKM archives
(Adapter, Connect, Logbook, Finance, Remote Parking and Eco Coach).
The source audit found 13,779 images and 6,105 compiled Android XML drawables.
This seed deliberately includes **1,233 named raster icons**, reduced to **816
unique bytewise image binaries**. Repeated assets remain addressable under each
source application's namespace through archive hard links.

Only icon-oriented, portable PNG and WebP images were selected. For each
application and normal/night variant, the best available resource density was
chosen; filenames identify the original app resource. The archive contains
`SHA256SUMS` and `SOURCES.csv` to preserve exact image identities and
original APK paths. These checksums validate original bytes, not similarity.

Android binary XML vectors, animation data, nine-patch backgrounds, photos,
full-screen onboarding art and the 168 SVG parking movement diagrams are not
simple standalone desktop icons and were not converted into this set.

The canonical `mercedes-icons-v1.tar.xz` file is stored in ten size-bounded
numbered Git blobs; `PART_SHA256SUMS` verifies every piece. The Debian build
reassembles and verifies the exact archive SHA-256
`8d55ccbe5f1eefe3800ab12578a641524742b9482410d95757317434523b342c`. The archive is then installed as a single package-owned seed,
and an idempotent helper populates `/System/Resources/Icons/mercedes`.
No application installer or package purge removes this system-owned catalogue.

**Rights:** these are extracted manufacturer artworks. The APK files are not
evidence of permission to redistribute the artwork publicly or relicense it.
Usage must respect the relevant copyright, trademarks and app licences.
