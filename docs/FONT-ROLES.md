# InfiltratorOS typography — 23 verified faces

The canonical physical inventory is [assets/fonts/SHA256SUMS](../assets/fonts/SHA256SUMS). The internal font family names below were read from the actual TTF **name** tables; spelling matters to Fontconfig/Pango. The resource directory is `/System/Resources/Fonts`; persistent shared fonts must not be deleted when one app is uninstalled.

This is a *role selection*, not an instruction to display all 23 faces in each window. An installed InfiltratorOS 1.0.20+ seeds all 23. The current standalone Linux installer revisions for System Monitor, Calculator and Files also provision the verified collection; earlier released packages may still use the old Common three-face bootstrap. A currently installed font name does not guarantee an older already-running application has selected it.

## Available families and actual faces

| Families | Included files | Intended scope |
| --- | --- | --- |
| **MB Corpo S Text WEB** | `mb_corpo_s_text_regular.ttf`, `mb_corpo_s_text_bold.ttf` | Paragraphs, navigation, ordinary labels; regular and bold |
| **MB Corpo S Text WEB** Light | `mb_corpo_s_text_light.ttf` | Secondary supporting prose at sufficient size/contrast |
| **MB Corpo S Text WEB Cond** | `mb_corpo_s_text_cond_regular.ttf`, `mb_corpo_s_text_cond_bold.ttf` | Dense tab labels, column headings and compact settings |
| **MB Corpo S Text WEB Cond Light** | `mb_corpo_s_text_cond_light.ttf` | Large, non-critical compact annotations |
| **MB Corpo S Title WEB** | `mb_corpo_s_title_regular.ttf`, `mb_corpo_s_title_bold.ttf` | Prominent section headings and action emphasis |
| **MB Corpo S Title WEB Light** | `mb_corpo_s_title_light.ttf` | Spacious secondary heading treatment |
| **MB Corpo S Title WEB Cond** | `mb_corpo_s_title_cond_regular.ttf`, `mb_corpo_s_title_cond_bold.ttf` | Tightly packed headings/navigation |
| **MB Corpo S Title WEB Cond Light** | `mb_corpo_s_title_cond_light.ttf` | Larger restrained labels |
| **MB Corpo A Title Cond WEB** | `mb_corpo_a_title_cond_regular.ttf` | Family product names and signature headings |
| **MB Corpo A Title WEB** | `mb_corpo_a_title_regular.ttf` | Broad display headings where width permits |
| **MB Corpo A Text Cond WEB** | `mb_corpo_a_text_cond_regular.ttf` | Short technical callouts and compact units |
| **MB Corpo A Screen1 Cond WEB** | `mb_corpo_a_screen_1_cond_regular.ttf` | Large secondary numeric/technical readouts |
| **MB Corpo A Screen2 Cond WEB** | `mb_corpo_a_screen_2_cond_regular.ttf` | Primary calculator and progress readouts |
| **Daimler CAC / CAC App** | `daimler_cac_regular.ttf`, `daimler_cac_app_regular.ttf` | Specialist identity/artwork use, not default body text |
| **Daimler CS / CS App** | `daimler_cs_regular.ttf`, `daimler_cs_demi.ttf`, `daimler_cs_app_regular.ttf`, `daimler_cs_app_demi.ttf` | Specialist identity treatments; App and standard variants remain distinct |

## Optimised per-product roles

| Surface | InfiltratorOS | System Monitor | Calculator | Files |
| --- | --- | --- | --- | --- |
| Ordinary UI | S Text Regular | S Text Regular | S Text Regular | S Text Regular |
| Emphasis | S Title Bold | S Title Bold | S Text Bold | S Text Bold |
| Product heading | A Title Cond | A Title Cond | A Title Cond | A Title Cond |
| Dense controls | S Text Cond | S Text Cond | S Text Cond | S Text Cond |
| Large numbers | A Screen2 Cond | A Screen1 Cond | A Screen2 Cond | Not applicable |
| Detailed/technical text | S Text Regular | S Text Cond | S Text Regular | S Text Regular with Cond for metadata |
| Secondary prose | S Text Regular | S Text Regular | S Text Regular | S Text Regular |

The dense and Screen roles may fall back to their original Common Title faces on individually installed older packages. Do not use the lightweight faces for small, contrast-critical text; do not synthesise heavy weights for families that do not carry a matching bold file. GTK3/GTK4, Win32 and iOS native adapters may use different font registration but should implement these same semantic choices once their verified assets are updated.

**Provenance boundary:** These are supplied MB/Daimler font resources, not a claim of Mercedes-Benz affiliation. Font licensing and redistribution permissions remain a separate obligation from SHA-256 integrity and the UI design policy.


## Typographic hierarchy and application-specific choices

**Common composition rules:** Prefer a small, repeatable set of faces per surface rather than using every available font. Use `S Text` for actual reading, `S Title` for hierarchy, `A Title` for distinct product identity, `S Text Cond` for *short* dense labels, and `A Screen` for large display numerals. Font weight and size must express information hierarchy, not decoration. Light or very condensed fonts are not suitable for 10–12 px error text, long filenames, detailed process names or calculator expressions. Avoid synthesising 600/650/800 weights from Regular-only A Screen and A Title faces.

**InfiltratorOS graphical progress:** Brand identity is A Title Cond, prominent install state is S Title Bold, explanatory text is S Text Regular, and short stage labels are S Text Cond. Reserve A Screen2 for the large numerical percent/completion readout; do not use it for long log lines. If the 23-face font cache cannot be resolved at setup time, present an explicit error instead of silently substituting a generic OS UI typeface.

**System Monitor:** CPU/memory/disk/network metrics and high-information numerical cards benefit from A Screen1 Cond. Hierarchical card headings use S Title Bold, dense process tables use S Text Cond where legible, and full application/process names should retain S Text Regular when condensation would damage scanability. Units, unavailable-value labels and provenance must remain distinguishable from numerical measurements. Telemetry colours are independent of font choices.

**Calculator:** The current large result uses A Screen2 Cond; expression entry, exact results and history remain S Text Regular to avoid misleading glyphs or loss of mathematical distinctions. Dense mode tabs and short commands use S Text Cond. Ordinary digits and operators should not be squeezed into Screen-face text intended for readouts. Precise results must remain copyable and mathematically authoritative; fonts are presentation only.

**Files:** Filenames and path components should prioritise S Text Regular with clarity over maximum compression. Metadata columns and short sidebar/place labels can use S Text Cond; the brand title uses A Title Cond. A Text Cond may be used for short breadcrumbs but must not truncate meaninglessly, and selected/error states must keep adequate contrast. The file manager is a scanning tool, not a typography specimen.

### Cross-platform qualification boundary

The 23-face Linux font cache does not itself update Calculator's Windows or iPhone embedded font sets, or System Monitor's Windows font registration. Distinct native platforms need independently verified asset registration and exact family-resolution tests before extended font parity may be claimed. A shared face name must resolve to the correct installed bytes; fallback success is not equivalent to typography success. Tests should cover long paths, monospace-sensitive values (including hexadecimal and precision-heavy numbers), table alignment, mixed-case names, locale glyph coverage, scaling and high-contrast Day/Night modes.
