# Chronon3D — Feature Reference

> Reference documentation for features that have detailed APIs, limitations, and usage patterns.
> These were previously in `README.md` and are now collected here for easy reference.

---

## SVG Path Import V1

Chronon3d supports importing paths directly from SVG path strings or files. This is designed for direct integration with `PathShape`.

### Usage

```cpp
#include <chronon3d/assets/svg_path_loader.hpp>

// 1. Parsing directly from path data:
auto result = chronon3d::assets::parse_svg_path_data("M 10 10 L 50 50 Z");
if (result.ok) {
    PathShape my_path = result.path;
}

// 2. Loading from a file:
auto result_file = chronon3d::assets::load_svg_path_file("assets/my_icon.svg");
```

### V1 Supported Commands

| Command | Description | Relative |
|---|---|---|
| `M` / `m` | Move to | ✅ |
| `L` / `l` | Line to | ✅ |
| `H` / `h` | Horizontal line to | ✅ |
| `V` / `v` | Vertical line to | ✅ |
| `C` / `c` | Cubic bezier | ✅ |
| `Q` / `q` | Quadratic bezier | ✅ |
| `Z` / `z` | Close path | N/A |

### Limitations (V1)

- **File Extraction**: Extracts only the first `<path d="...">` attribute inside the SVG file
- **Not supported**: Gradients, masks, multiple paths, groups, transforms, viewBox dimensions, CSS styling (`fill`, `stroke` width), text, filters
- Any unsupported commands return `ok = false` with a detailed error message

---

## Text V1 — Layout Engine & Presets

Chronon3d has a fully-functional, cache-invalidation-safe Text V1 layout engine with layout presets, automatic fitting (`auto_fit`), wrapping, overflow control, and subtitle/karaoke model support.

### Supported Features

| Feature | Description |
|---|---|
| **Wrapping & Overflow** | Word wrap, character wrap, max lines limit, text ellipsis |
| **Auto-Fit** | Binary-search algorithm to scale text down to fit a target box |
| **Layout Presets** | Predefined templates: `headline`, `subtitle`, `lower_third`, `quote`, `breaking_news`, `luxury_gold`, `neon` |
| **Subtitles** | Standard cues, word timings, highlight models |
| **HarfBuzz Shaping** | Glyph positioning for Latin and simple UTF-8 scripts |
| **Per-glyph Animation** | `TextAnimMode::ByGlyph` with cluster substring extraction |

### Limitations

> [!IMPORTANT]
> **Unicode & Language Support**: Text V1 wrapping and layout engine measures strings byte-by-byte (`char`). It is friendly with Latin / simple UTF-8 languages (English, Italian, Spanish, Portuguese, etc.). Global Unicode layout, RTL languages (Arabic, Hebrew), complex ligatures (Hindi), CJK line-breaking, emoji, and HarfBuzz/ICU integration are planned for future versions.

### Implementation Details

- Font engine: FreeType face loading + HarfBuzz `hb_shape`
- Glyph cache: LRU with configurable max memory (`CHRONON_TEXT_CACHE_MAX_MB`)
- Text centering uses **pixel-ink centering** — measures actual rendered pixel bounds rather than font metrics (see [ORIENTATION.md](ORIENTATION.md#text-rendering-pixel-ink-centering))
