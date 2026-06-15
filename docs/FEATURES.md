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
| **HarfBuzz Shaping** | Full glyph positioning via `hb_shape()` with auto-detection of script, direction, and language |
| **Bidi Segmentation** | Right-to-left text support via FriBidi (`bidi_segmenter.cpp`). Auto-detects Arabic, Hebrew, and other RTL scripts |
| **Text Direction** | `TextDirection::Auto/RTL/LTR` with HarfBuzz auto-detection from Unicode character ranges |
| **Per-glyph Animation** | `TextAnimMode::ByGlyph` with cluster substring extraction via `PlacedGlyphRun` |
| **PlacedGlyphRun** | Canonical glyph positioning with tracking-aware advances and cluster info. Single source of truth for fill, stroke, typewriter, and TextAnimator |
| **Text Gradient Fill** | Linear, radial, and conic gradient fills via `FillStyle` / `apply_text_fill_style()` |
| **Text Stroke** | Stroke with HarfBuzz-shaped glyphs via `FtGlyphPathBuilder` (consistent GSUB for Arabic, Devanagari, etc.) |
| **Text Material** | Premium effects: gradient fill, bevel, top highlight, bottom shade, inner shadow, emissive boost. Presets: `premium()`, `neon()`, `glass()`, `flat()` |
| **Glyph Atlas** | Per-glyph LRU cache (32MB, 8 shards) keyed by `(font_path, glyph_id, font_size)` |
| **Pre-shaped Bypass** | Typewriter/TextAnimator can pass pre-shaped `PlacedGlyphRun` to skip re-shaping |

### Limitations

> [!IMPORTANT]
> **CJK Line-Breaking**: CJK text renders correctly via HarfBuzz auto-detection, but word-wrapping uses byte-level logic. Proper CJK line-breaking (ICU-based) is planned.
> **Emoji**: Emoji rendering depends on the font containing emoji glyphs. Color emoji (COLR/CBDT) is not yet supported.
> **GlyphAtlas Integration**: The per-glyph atlas infrastructure is implemented but not yet wired into the main rendering hot-path.

### Implementation Details

- Font engine: FreeType face loading + HarfBuzz `hb_shape()` with auto-detect (`hb_buffer_guess_segment_properties()`)
- Bidi: FriBidi for RTL segmentation (`segment_bidi_runs()`)
- Glyph cache: Full-image LRU (`CHRONON_TEXT_CACHE_MAX_MB`) + per-glyph atlas (`CHRONON_GLYPH_ATLAS_MAX_BYTES`)
- Text centering: Layout-box centering (default) or **pixel-ink centering** (opt-in via `TextCenteringMode::PixelInk`)
