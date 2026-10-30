# Desktop-first operating system — philosophy addendum

This addendum clarifies the intended user experience of InfiltratorOS. It supplements, rather than replaces, the engineering philosophy and product-family contracts.

## Identity and inspirations

InfiltratorOS is intended to be its own operating system, not merely a renamed GNU/Linux distribution. Its design draws on AmigaOS's modular shared-library model, BeOS's integrated graphical desktop, and the workstation experiences of Solaris and SGI IRIX. These are design inspirations, not claims that those systems share a kernel or codebase. In particular, BeOS was not Linux.

## Graphical operation is the normal operation

The desktop is the primary interface to the operating system. Installation, software management, settings, disk management, file operations, maintenance, and normal recovery must have complete graphical workflows. A command-line interface can remain available for engineering or exceptional recovery; ordinary users must not need it.

A graphical application should implement its functions through its own code and the project's reusable system interfaces, rather than merely launching unrelated external command-line utilities where a suitable native capability exists.

## Shared capabilities and ownership

Common/Libraries owns genuinely reusable interfaces and mechanisms. Independently released calendar, clock, and filesystem modules own their format-specific functionality. Applications consume those authoritative modules rather than maintaining duplicated implementations. The goal is to reduce unnecessary external runtime dependencies without confusing them with build-time tooling or temporary host APIs.

## Current development boundary

The current Linux Mint/Debian environment is a transitional development and compatibility platform, not the intended permanent definition of InfiltratorOS. Its present dependencies must be reported accurately, and future independence must not be presented as already delivered.

Near-term integration work focuses on the desktop, graphical applications, consistent interaction, native shared modules, and elimination of redundant implementations. Kernel replacement, boot internals, and universal module loading are longer-term engineering milestones, not prerequisites for improving the current graphical experience.
