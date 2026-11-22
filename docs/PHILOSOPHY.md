# Engineering Philosophy

This repository follows the canonical [Infiltrator Engineering Philosophy](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md).

The standard is Mercedes-Benz engineering excellence as an execution benchmark, not a branding claim or affiliation: technical depth, quality, reliability, trust, effortless operation, disciplined reduction, elegance and craftsmanship. The strongest justified solution wins regardless of age or fashion; complexity must earn its place; and simplicity must come from complete engineering rather than removed capability.

Substantive code commentary and maintained technical documentation are held to a postgraduate / university master's-level engineering standard: precise, complete, current and focused on invariants, ownership, reasoning, failure/recovery, compatibility and other non-obvious contracts rather than narration of obvious syntax.

Repository-specific design, architecture, safety and validation documents may strengthen or specialise this standard, but must not silently weaken or contradict it.


## ELEGANCE — the artistic design authority

**ELEGANCE is first and foremost an artistic and visual design requirement.** It means deliberate beauty, accomplished composition and unmistakable craftsmanship. The reference is the aesthetic sophistication associated with Mercedes-Benz, Fendi and Prada: finely judged proportions, confident typography, exceptional attention to detail, material and tonal harmony, composed hierarchy, and a coherent identity. These names describe a level of design ambition, not an instruction to copy their trademarks, artwork or layouts.

Elegance is judged by the finished work **as something seen**, not by the number of words, elements or lines removed. An elaborate design can be elegant; an empty one can be crude. A detailed engineering explanation can be beautifully presented; a shorter explanation can be visually ugly or academically inadequate. Minimalism, terseness, whitespace removal, generic uniformity and simplification are **not** definitions of elegance. Neither are ornamental ASCII boxes, exaggerated banners or decoration for its own sake.

This artistic direction applies to the operating system, BOOT, applications, themes, illustrations, diagrams, manuals, Markdown and source-code commentary. Every medium should express it through its own craft:

- **Composition:** deliberate proportion, rhythm, visual balance, alignment, hierarchy and considered negative space. Nothing should look accidental, crowded, mechanically templated or arbitrarily empty.
- **Typography and detail:** beautiful typographic relationships, precise spacing and line breaks, consistent but not monotonous treatment of headings and accents, and exceptional finishing quality. Use the approved InfiltratorOS typographic identity where the medium supports it.
- **Visual identity:** cohesive, recognisable and timeless rather than generic, fashionable or gaudy. Colour, contrast, surfaces and illustrations should feel art-directed and intentional.
- **Documentation and source:** make long, rigorous material visually inviting and easy to follow. Use carefully composed titles, sections, paragraphs, code blocks and comment structures. Preserve every useful explanation of algorithms, invariants, rationale, ownership, limitations, error handling and recovery. Improve **presentation** without diminishing **substance**.
- **Judgement:** assess the actual rendered page, editor view or interface, including the relationship between its parts. Do not claim artistic elegance from a word count, comment count, automated formatting pass or superficial tidying.

**Non-negotiable interpretation:** when asked for *ELEGANCE*, do **not** respond by shortening comments, deleting technical depth, reducing the documentation, removing visual richness, or replacing carefully considered design with plain minimalism. Aesthetic refinement and postgraduate-level technical completeness are simultaneous requirements. Neither replaces the other.

## Source-first, hardware-native execution

InfiltratorOS is designed as a **source-first, locally compiled and hardware-optimised operating system**. This is a core product requirement, not an incidental installer option. The intended native installation path obtains an exact, verified source release for each Infiltrator-owned component, compiles it on the target machine using a qualified toolchain, validates the result and installs the machine-specific binary. A generic precompiled package may serve bootstrap, compatibility or recovery needs, but it does not silently count as a completed native source build.

Native compilation deliberately trades installation time and computing effort for binaries tailored to the installed hardware. Where the component, compiler and target support them, the native build policy favours appropriate CPU-specific instruction selection, aggressive optimisation, link-time optimisation (LTO) and properly trained, measured profile-guided optimisation (PGO). Flags are means, not evidence: successful compilation or a `+native` package suffix alone cannot establish that PGO was trained, that a binary was built for the actual machine, or that runtime performance improved. Correctness, safety, functional parity, recovery and measurable results remain mandatory.

The complexity of acquiring, compiling, qualifying and installing source belongs to the system's graphical setup and software-management workflows, not to ordinary users. Verified source identity, bounded privileges, failure reporting and recoverable installation are part of that contract. Build optimisations and fallbacks must be recorded accurately rather than silently claimed.

Independent component ownership is equally intentional. Each calendar, clock, filesystem and application has its own source authority and release lifecycle, while Common owns genuinely shared interfaces. A completed, unchanged provider does not require further feature development merely because it is separately compiled for each machine. Rebuilds or adaptations are justified by relevant source, toolchain, dependency, interface or hardware changes, not by a preference for monolithic binaries or generic distribution packages.

## One graphical operating system

All components belong to InfiltratorOS and share a coherent visual and
interaction language. Beauty, clarity and consistency are product requirements.
Routine installation, settings, maintenance and recovery must be graphical;
the terminal is hidden and reserved for absolute emergencies.

Open-source starting points are progressively rewritten under product-owned
architecture and behaviour. Required source attribution and licensing remain
accurate. Current component names govern application IDs, executables, package
identities, installed paths, publication and maintained documentation. During
alpha development, obsolete names are retired rather than published as aliases.
