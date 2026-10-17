# Architecture

## Ownership

`scripts/build-deb.sh` owns package membership and reads the root VERSION. It accepts only a numeric semantic version before constructing staging paths, requires the supported amd64 host, verifies the approved artwork, and uses SOURCE_DATE_EPOCH for deterministic Debian archives.

The InfiltratorOS package owns establishment of the native filesystem skeleton documented in `FILESYSTEM.md`. The hierarchy is additive over the Debian/Linux base during migration: packaging creates the native roots and role directories, while subsystem code decides how individual capabilities are discovered and used. Named user directories are deliberately outside package ownership.

`stage-theme.c` stages the approved theme; `select-theme.c` owns system theme selection. Debian maintainer scripts own invocation during package installation and removal.

System Monitor and Calculator retain separate installation workers because their acquisition, compilation, qualification and replacement policies differ. Each worker owns its application lock, shared native-build lock, progress updates and terminal result.

## Shared native installation mechanics

`start-native-install.c` and `native-install-progress.c` compile once per product profile. `installer-profile.h` maps compile-time identity to fixed installed paths. Runtime arguments cannot select another privileged worker.

`installer-process.c` owns detached process lifetime. The parent reaps the first child and receives a close-on-exec pipe acknowledgement from the grandchild. Setup and exec failures return their errno; a successful exec acknowledges startup, independently of later worker success. Standard streams are redirected explicitly and inherited descriptors are closed. Progress windows drop to the discovered desktop user.

`installer-status.c` owns the read protocol: newline-terminated state, progress, stage and detail fields; bounded strings; progress from -1 through 100; no duplicated known fields. Invalid or incomplete records leave the caller's previous result unchanged. Worker writes use a temporary file and rename. Unknown fields allow additive protocol evolution.

## Failure and recovery

A launcher writes a complete failure record if worker startup fails. Progress UI failure is logged separately and does not stop an already running worker. A worker's terminal status and log remain available after its progress window closes. Installation recipes verify the replacement before removing distribution applications.

Desktop discovery currently selects the first eligible graphical user found in /proc. Multi-seat selection and physical boot qualification remain explicit integration concerns; neither is inferred from a package build.
