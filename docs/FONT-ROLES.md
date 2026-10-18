# InfiltratorOS typography — 23 verified faces

The canonical physical inventory is [assets/fonts/SHA256SUMS](../assets/fonts/SHA256SUMS). The internal font family names below were read from the actual TTF **name** tables; spelling matters to Fontconfig/Pango. The resource directory is `/System/Resources/Fonts`; persistent shared fonts must not be deleted when one app is uninstalled.

This is a *role selection*, not an instruction to display all 23 faces in each window. Common's existing three-face contract remains a supported fallback during migration. An installed InfiltratorOS 1.0.20+ seeds all 23. Standalone consumers using an older Common font bootstrap can continue to start with the older faces; they cannot claim full 23-face parity until their installer also provisions the new assets.

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
