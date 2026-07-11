# Chronon3D — Quickstart

> 10 copy-paste examples for the Chronon3D text rendering API.
>
> All examples use the canonical `presets::text::*` and `from_text_spec(TextSpec{...})` API (M1.8 §3C + §6).
> The deprecated `centered_text()` + `glow_text()` helpers were REMOVED in M1.8 Fase 5 (§14 Step 5).

---

## #1 — Hello World (single text)

```cpp
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::text;

Composition hello_world() {
    return composition(
        {.name = "HelloWorld", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.text("hello", title_centered("Hello, Chronon3D!"));
            return s.build();
        });
}
```

**Output**: 1920×1080 PNG with "Hello, Chronon3D!" centered on canvas (Poppins-Bold 96pt, white).

---

## #2 — Multiple text layers (title + subtitle)

```cpp
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::text;

Composition title_and_subtitle() {
    return composition(
        {.name = "TitleAndSubtitle", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Main title at canvas center
            s.text("title", title_centered("CHRONON3D V0.1"));
            // Subtitle at safe-area bottom
            s.text("subtitle", subtitle_bottom("Text Production V1"));
            return s.build();
        });
}
```

**Output**: 1920×1080 PNG with title at center + subtitle at safe-area bottom.

---

## #3 — Text with effects (glow + shadow)

```cpp
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::text;

Composition text_with_glow() {
    return composition(
        {.name = "TextWithGlow", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("glow_text", [](LayerBuilder& l) {
                // Build the preset, then attach a glow effect
                auto def = kinetic_word("HERO", 120.0f, Color{1.0f, 0.8f, 0.2f});
                def.effects.glow = GlowParams{
                    .color = Color{1.0f, 0.6f, 0.1f, 1.0f},
                    .radius = 24.0f,
                    .intensity = 0.8f
                };
                l.text("label", def);
            });
            return s.build();
        });
}
```

**Output**: 1920×1080 PNG with "HERO" in kinetic word style + orange glow effect.

---

## #4 — Text animation (typewriter reveal)

```cpp
#include <chronon3d/text/text_run_spec.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

Composition typewriter_reveal() {
    return composition(
        {.name = "TypewriterReveal", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 90},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("typewriter", [&ctx](LayerBuilder& l) {
                l.text_run("reveal", TextRunSpec{
                    .content    = {.value = "Typewriter reveal in progress..."},
                    .font       = {.font_size = 64.0f},
                    .layout     = {.box = Vec2{1200.0f, 200.0f},
                                   .anchor = TextAnchor::Center,
                                   .align  = TextAlign::Center},
                    .position   = Vec3{960.0f, 540.0f, 0.0f},
                    .animator   = TypewriterAnimator{
                        .start_delay = 0,
                        .chars_per_second = 12.0f,
                        .cursor_blink = true
                    }
                });
            });
            return s.build();
        });
}
```

**Output**: 30fps animation, "Typewriter reveal in progress..." types out over 3 seconds.

---

## #5 — Kinetic typography (preset usage)

```cpp
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::text;

Composition kinetic_typography() {
    return composition(
        {.name = "KineticTypography", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 60},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Hero word with kinetic typography + optional accent
            s.text("hero", kinetic_word("IMPACT", 144.0f, Color{1.0f, 0.2f, 0.2f}));
            return s.build();
        });
}
```

**Output**: "IMPACT" in red 144pt kinetic word style at canvas center.

---

## #6 — Text placement (SafeArea constraints)

```cpp
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::text;

Composition safe_area_demo() {
    return composition(
        {.name = "SafeAreaDemo", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Caption centered in safe area (5% inset)
            s.text("caption_top", caption_safe_area("Top safe-area caption"));
            s.text("caption_bottom", caption_safe_area("Bottom safe-area caption"));
            return s.build();
        });
}
```

**Output**: Two captions placed at top + bottom of the 5% safe-area inset.

---

## #7 — Custom fonts (from_text_spec with .ttf)

```cpp
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_spec.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

Composition custom_font() {
    return composition(
        {.name = "CustomFont", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("custom", [](LayerBuilder& l) {
                // Build a TextDefinition with a custom font + properties
                auto def = from_text_spec(TextSpec{
                    .content    = {.value = "Custom Roboto Light"},
                    .font       = {.font_path   = "assets/fonts/Roboto-Light.ttf",
                                   .font_family = "Roboto",
                                   .font_weight = 300,
                                   .font_size   = 72.0f},
                    .layout     = {.box = Vec2{1400.0f, 200.0f},
                                   .anchor = TextAnchor::Center,
                                   .align  = TextAlign::Center},
                    .position   = Vec3{960.0f, 540.0f, 0.0f},
                    .appearance = {.color = Color{0.9f, 0.9f, 0.95f, 1.0f}}
                });
                l.text("label", def);
            });
            return s.build();
        });
}
```

**Output**: "Custom Roboto Light" rendered with the Roboto-Light.ttf font (72pt, light grey).

---

## #8 — Text on path (tracking + line height)

```cpp
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_spec.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

Composition text_with_tracking() {
    return composition(
        {.name = "TextWithTracking", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("tracked", [](LayerBuilder& l) {
                // Wide-tracked title with custom line height
                auto def = from_text_spec(TextSpec{
                    .content    = {.value = "W I D E   T R A C K"},
                    .font       = {.font_size = 56.0f},
                    .layout     = {.box       = Vec2{1600.0f, 200.0f},
                                   .anchor    = TextAnchor::Center,
                                   .align     = TextAlign::Center,
                                   .tracking  = 12.0f,
                                   .line_height = 1.5f},
                    .position   = Vec3{960.0f, 540.0f, 0.0f},
                    .appearance = {.color = Color{1.0f, 0.9f, 0.4f, 1.0f}}
                });
                l.text("label", def);
            });
            return s.build();
        });
}
```

**Output**: "W I D E   T R A C K" with 12px tracking + 1.5 line height in yellow.

---

## #9 — Exporting (writing the canvas to PNG)

```cpp
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <fstream>

using namespace chronon3d;
using namespace chronon3d::presets::text;

void export_to_png(const std::string& output_path) {
    auto comp = composition(
        {.name = "ExportHello", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.text("hello", title_centered("Export Demo"));
            return s.build();
        });

    SoftwareRenderer renderer;
    auto fb = renderer.render(comp, Frame{0});

    // The Framebuffer can be written to PNG via the SoftwareRenderer's
    // built-in write_png() method (or stb_image_write via the SDK).
    renderer.write_png(*fb, output_path);
}
```

**Output**: `export_to_png("hello.png")` writes the rendered frame to `hello.png`.

---

## #10 — Inspection (chronon3d_cli inspect-text)

```bash
# Inspect a composition's text bboxes + visibility audit
chronon3d_cli inspect-text HelloWorld --frame 0 --json > hello_audit.json

# Sample output:
# [
#   {
#     "node": "hello_text",
#     "font": "assets/fonts/Poppins-Bold.ttf",
#     "glyph_count": 19,
#     "frame": 0,
#     "local_bbox":  {"x": 0,    "y": 0,    "w": 0,    "h": 0},
#     "world_bbox":  {"x": 280,  "y": 480,  "w": 1360, "h": 120},
#     "predicted_bbox": {"x": 270,  "y": 470,  "w": 1380, "h": 140},
#     "alpha_bbox":  {"x": 290,  "y": 490,  "w": 1340, "h": 100},
#     "status": "PASS"
#   }
# ]
#
# Exit codes:
#   0 = PASS  (all invariants hold)
#   1 = FAIL  (composition not found, or non-diagnostic build)
#   2 = VIOLATION (bbox contract violation: predicted < world)
```

**Use case**: CI integration — fail the build if any text composition has a bbox contract violation.

---

## See also

- [`docs/FEATURES.md`](FEATURES.md) §Text Ergonomics — 17 animation helpers + 16 placements + 5 presets
- [`docs/ROADMAP.md`](ROADMAP.md) M1.8 §3A/§3B/§3C + §14/§15 rows
- [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) Fase 5 closure row
- [`include/chronon3d/presets/text/text_presets_v1.hpp`](../include/chronon3d/presets/text/text_presets_v1.hpp) — 5 preset functions (canonical API)
- [`include/chronon3d/text/text_definition.hpp`](../include/chronon3d/text/text_definition.hpp) — `TextDefinition` + `from_text_spec` adapter
- [`docs/CHANGELOG.md`](CHANGELOG.md) — §13, §14, §3C, Fase 5 entries
