# Phase 3: Effects Expansion + Mask Feather Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add three new layer effects (hue/saturation shift, vignette, color correction) and soft-edge mask feathering — all CPU-only, no new libraries.

**Architecture:** New effect params are added to the `EffectParams` variant in `effect_stack.hpp`; new LayerBuilder methods push instances onto the effect stack; `SoftwareEffectRunner` dispatches to per-pixel implementations in `render_effects_processor`. Mask feather changes `pixel_passes_mask` from `bool` to `f32` alpha — all callers multiply pixel alpha by the returned weight.

**Tech Stack:** C++20, doctest, existing `Framebuffer`, HSL math (written inline, no extra lib).

---

## File Map

| File | Action |
|------|--------|
| `include/chronon3d/scene/effects/effect_stack.hpp` | Add `HueSaturationParams`, `VignetteParams`, `ColorCorrectionParams` to `EffectParams` variant |
| `include/chronon3d/scene/builders/layer_builder.hpp` | Add `hue_saturation()`, `vignette()`, `color_correction()` declarations |
| `src/scene/layer_builder.cpp` | Implement three new effect methods |
| `src/backends/software/utils/render_effects_processor.hpp` | Declare three new `apply_*` functions |
| `src/backends/software/utils/render_effects_processor.cpp` (or existing .cpp) | Implement per-pixel loops |
| `src/backends/software/software_effect_runner.cpp` | Dispatch new params in `apply_effect_stack` |
| `src/backends/software/processors/builtin_processors.cpp` | Register three new effect processors |
| `src/backends/software/processors/software_effect_processors.cpp` | Add three new processor classes |
| `include/chronon3d/scene/mask/mask.hpp` | Add `feather` field to `Mask` + mask param structs |
| `src/scene/layer_builder.cpp` | Pass feather in `mask_rect/rounded_rect/circle` |
| `src/backends/software/rasterizers/shape_rasterizer.cpp` | Change `pixel_passes_mask` → `mask_alpha()` returning f32; update all callers |
| `tests/renderer/effects/test_new_effects.cpp` | New: effect smoke tests |
| `tests/renderer/effects/test_mask_feather.cpp` | New: feather tests |
| `tests/CMakeLists.txt` | Register two new test files |

---

## Task 1: New Effect Params and `LayerBuilder` API

**Files:**
- Modify: `include/chronon3d/scene/effects/effect_stack.hpp`
- Modify: `include/chronon3d/scene/builders/layer_builder.hpp`
- Modify: `src/scene/layer_builder.cpp`

- [ ] **Step 1: Add new param structs to `effect_stack.hpp`**

In `include/chronon3d/scene/effects/effect_stack.hpp`, add after `BloomParams`:

```cpp
struct HueSaturationParams { f32 hue_shift{0.0f}; f32 saturation_scale{1.0f}; f32 lightness{0.0f}; };
struct VignetteParams      { f32 intensity{0.6f};  f32 radius{0.75f}; };
struct ColorCorrectionParams {
    Color shadows{0,0,0,0};
    Color midtones{0,0,0,0};
    Color highlights{0,0,0,0};
};
```

Extend the `EffectParams` variant to include the three new types:

```cpp
using EffectParams = std::variant<
    BlurParams,
    TintParams,
    BrightnessParams,
    ContrastParams,
    DropShadowParams,
    GlowParams,
    BloomParams,
    HueSaturationParams,
    VignetteParams,
    ColorCorrectionParams
>;
```

- [ ] **Step 2: Add declarations to `layer_builder.hpp`**

In `include/chronon3d/scene/builders/layer_builder.hpp`, after `LayerBuilder& bloom(...)`:

```cpp
    LayerBuilder& hue_saturation(f32 hue_shift_deg, f32 saturation_scale = 1.0f, f32 lightness = 0.0f);
    LayerBuilder& vignette(f32 intensity = 0.6f, f32 radius = 0.75f);
    LayerBuilder& color_correction(Color shadows = {}, Color midtones = {}, Color highlights = {});
```

- [ ] **Step 3: Implement in `layer_builder.cpp`**

In `src/scene/layer_builder.cpp`, after the `bloom()` implementation:

```cpp
LayerBuilder& LayerBuilder::hue_saturation(f32 hue_shift_deg, f32 saturation_scale, f32 lightness) {
    m_layer.effects.push_back(EffectInstance{
        effects::EffectDescriptor{.id = "color.hue_saturation"},
        HueSaturationParams{hue_shift_deg, saturation_scale, lightness}});
    return *this;
}

LayerBuilder& LayerBuilder::vignette(f32 intensity, f32 radius) {
    m_layer.effects.push_back(EffectInstance{
        effects::EffectDescriptor{.id = "color.vignette"},
        VignetteParams{intensity, radius}});
    return *this;
}

LayerBuilder& LayerBuilder::color_correction(Color shadows, Color midtones, Color highlights) {
    m_layer.effects.push_back(EffectInstance{
        effects::EffectDescriptor{.id = "color.correction"},
        ColorCorrectionParams{shadows, midtones, highlights}});
    return *this;
}
```

- [ ] **Step 4: Build to verify clean compile**

```
cmake --build build/chronon/win-debug --target chronon3d_scene_tests -j8
```

Expected: clean.

- [ ] **Step 5: Commit**

```
git add include/chronon3d/scene/effects/effect_stack.hpp
git add include/chronon3d/scene/builders/layer_builder.hpp
git add src/scene/layer_builder.cpp
git commit -m "feat(effects): add HueSaturation, Vignette, ColorCorrection params + LayerBuilder API"
```

---

## Task 2: Implement the three effects (per-pixel)

**Files:**
- Modify: `src/backends/software/utils/render_effects_processor.hpp`
- Modify: `src/backends/software/utils/render_effects_processor.cpp` (or the existing `.hpp` impl)
- Modify: `src/backends/software/software_effect_runner.cpp`
- Modify: `src/backends/software/processors/software_effect_processors.cpp`
- Modify: `src/backends/software/processors/builtin_processors.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/renderer/effects/test_new_effects.cpp`:

```cpp
#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "tests/helpers/pixel_assertions.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

static std::unique_ptr<Framebuffer> render_with_effect(Color fill_color, auto apply_effect_fn) {
    SoftwareRenderer renderer;
    Composition comp(CompositionSpec{.width = 100, .height = 100, .duration = 1},
        [=](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("box", [&](LayerBuilder& b) {
                b.rect("r", {.size = {100, 100}, .color = fill_color, .pos = {0, 0, 0}});
                apply_effect_fn(b);
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

TEST_CASE("HueSaturation: hue_shift=180 shifts red to cyan-ish") {
    // Solid red rect, shift hue by 180° → should become cyan-ish
    auto fb = render_with_effect(Color{1, 0, 0, 1}, [](LayerBuilder& b) {
        b.hue_saturation(180.0f, 1.0f, 0.0f);
    });
    REQUIRE(fb != nullptr);
    Color c = fb->get_pixel(50, 50);
    // After 180° hue shift, red→cyan: g+b > r
    CHECK(c.g + c.b > c.r + 0.3f);
}

TEST_CASE("HueSaturation: saturation_scale=0 desaturates to grey") {
    auto fb = render_with_effect(Color{1, 0, 0, 1}, [](LayerBuilder& b) {
        b.hue_saturation(0.0f, 0.0f, 0.0f);
    });
    REQUIRE(fb != nullptr);
    Color c = fb->get_pixel(50, 50);
    // Desaturated: r ≈ g ≈ b
    CHECK(std::abs(c.r - c.g) < 0.15f);
    CHECK(std::abs(c.g - c.b) < 0.15f);
}

TEST_CASE("Vignette: corners darker than center") {
    auto fb = render_with_effect(Color{1, 1, 1, 1}, [](LayerBuilder& b) {
        b.vignette(0.9f, 0.5f);
    });
    REQUIRE(fb != nullptr);
    Color center = fb->get_pixel(50, 50);
    Color corner = fb->get_pixel(2, 2);
    // Center should be brighter than corner
    float center_lum = center.r + center.g + center.b;
    float corner_lum = corner.r + corner.g + corner.b;
    CHECK(center_lum > corner_lum + 0.3f);
}

TEST_CASE("ColorCorrection: highlights tint adds to bright pixels") {
    auto fb = render_with_effect(Color{1, 1, 1, 1}, [](LayerBuilder& b) {
        b.color_correction({}, {}, Color{0.3f, 0, 0, 1});  // red highlights tint
    });
    REQUIRE(fb != nullptr);
    Color c = fb->get_pixel(50, 50);
    // Red channel should be boosted on a bright pixel
    CHECK(c.r > c.b + 0.1f);
}
```

- [ ] **Step 2: Register in CMakeLists**

In `tests/CMakeLists.txt`, inside `chronon3d_renderer_tests`:

```cmake
    renderer/effects/test_new_effects.cpp
```

- [ ] **Step 3: Run to confirm failure**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_renderer_tests --output-on-failure
```

Expected: tests fail — effects are no-ops (fallback path in `SoftwareEffectRunner`).

- [ ] **Step 4: Add `apply_*` declarations to `render_effects_processor.hpp`**

In `src/backends/software/utils/render_effects_processor.hpp`, add after the existing declarations:

```cpp
void apply_hue_saturation(Framebuffer& fb, f32 hue_shift_deg, f32 saturation_scale, f32 lightness);
void apply_vignette(Framebuffer& fb, f32 intensity, f32 radius);
void apply_color_correction(Framebuffer& fb, const Color& shadows, const Color& midtones, const Color& highlights);
```

- [ ] **Step 5: Implement per-pixel logic**

Find the existing `render_effects_processor.cpp` (search in `src/backends/software/utils/`) or add to the `.hpp` implementation. Add:

```cpp
// HSL helper — all inputs/outputs [0..1]
static void rgb_to_hsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxc = std::max({r, g, b});
    float minc = std::min({r, g, b});
    l = (maxc + minc) * 0.5f;
    if (maxc == minc) { h = s = 0.0f; return; }
    float d = maxc - minc;
    s = l > 0.5f ? d / (2.0f - maxc - minc) : d / (maxc + minc);
    if      (maxc == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (maxc == g) h = (b - r) / d + 2.0f;
    else                h = (r - g) / d + 4.0f;
    h /= 6.0f;
}

static float hue_to_rgb(float p, float q, float t) {
    if (t < 0) t += 1; if (t > 1) t -= 1;
    if (t < 1.0f/6) return p + (q - p) * 6 * t;
    if (t < 1.0f/2) return q;
    if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6;
    return p;
}

static void hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.0f) { r = g = b = l; return; }
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    float p = 2 * l - q;
    r = hue_to_rgb(p, q, h + 1.0f/3);
    g = hue_to_rgb(p, q, h);
    b = hue_to_rgb(p, q, h - 1.0f/3);
}

void apply_hue_saturation(Framebuffer& fb, f32 hue_shift_deg, f32 saturation_scale, f32 lightness) {
    const float hue_shift_norm = hue_shift_deg / 360.0f;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.a <= 0.0f) continue;
            float h, s, l;
            rgb_to_hsl(c.r, c.g, c.b, h, s, l);
            h = std::fmod(h + hue_shift_norm + 1.0f, 1.0f);
            s = std::clamp(s * saturation_scale, 0.0f, 1.0f);
            l = std::clamp(l + lightness, 0.0f, 1.0f);
            hsl_to_rgb(h, s, l, c.r, c.g, c.b);
            fb.set_pixel(x, y, c);
        }
    }
}

void apply_vignette(Framebuffer& fb, f32 intensity, f32 radius) {
    const float cx = fb.width()  * 0.5f;
    const float cy = fb.height() * 0.5f;
    const float max_dist = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.a <= 0.0f) continue;
            float dx = (x - cx) / max_dist;
            float dy = (y - cy) / max_dist;
            float dist = std::sqrt(dx * dx + dy * dy);
            // smoothstep(radius, 1.0, dist) → 0 at center, 1 at edge
            float t = std::clamp((dist - radius) / (1.0f - radius), 0.0f, 1.0f);
            float vig = 1.0f - intensity * (t * t * (3.0f - 2.0f * t));
            c.r *= vig; c.g *= vig; c.b *= vig;
            fb.set_pixel(x, y, c);
        }
    }
}

void apply_color_correction(Framebuffer& fb, const Color& shadows, const Color& midtones, const Color& highlights) {
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.a <= 0.0f) continue;
            float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            // Shadow influence: peaks at lum=0
            float shadow_w = std::max(0.0f, 1.0f - lum * 3.0f);
            // Highlight influence: peaks at lum=1
            float highlight_w = std::max(0.0f, lum * 3.0f - 2.0f);
            // Midtone influence: peaks at lum=0.5
            float midtone_w = std::max(0.0f, 1.0f - std::abs(lum - 0.5f) * 4.0f);
            c.r = std::clamp(c.r + shadows.r*shadow_w + midtones.r*midtone_w + highlights.r*highlight_w, 0.0f, 1.0f);
            c.g = std::clamp(c.g + shadows.g*shadow_w + midtones.g*midtone_w + highlights.g*highlight_w, 0.0f, 1.0f);
            c.b = std::clamp(c.b + shadows.b*shadow_w + midtones.b*midtone_w + highlights.b*highlight_w, 0.0f, 1.0f);
            fb.set_pixel(x, y, c);
        }
    }
}
```

- [ ] **Step 6: Add three effect processor classes to `software_effect_processors.cpp`**

In `src/backends/software/processors/software_effect_processors.cpp`, add after the existing processors:

```cpp
class SoftwareHueSaturationProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params) override {
        if (auto* p = std::get_if<HueSaturationParams>(&params))
            apply_hue_saturation(fb, p->hue_shift, p->saturation_scale, p->lightness);
    }
};

class SoftwareVignetteProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params) override {
        if (auto* p = std::get_if<VignetteParams>(&params))
            apply_vignette(fb, p->intensity, p->radius);
    }
};

class SoftwareColorCorrectionProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params) override {
        if (auto* p = std::get_if<ColorCorrectionParams>(&params))
            apply_color_correction(fb, p->shadows, p->midtones, p->highlights);
    }
};

std::unique_ptr<EffectProcessor> create_hue_saturation_processor() {
    return std::make_unique<SoftwareHueSaturationProcessor>();
}
std::unique_ptr<EffectProcessor> create_vignette_processor() {
    return std::make_unique<SoftwareVignetteProcessor>();
}
std::unique_ptr<EffectProcessor> create_color_correction_processor() {
    return std::make_unique<SoftwareColorCorrectionProcessor>();
}
```

Also add the missing include at the top:
```cpp
#include "../utils/render_effects_processor.hpp"
```

- [ ] **Step 7: Register in `builtin_processors.cpp`**

Add forward declarations:
```cpp
std::unique_ptr<EffectProcessor> create_hue_saturation_processor();
std::unique_ptr<EffectProcessor> create_vignette_processor();
std::unique_ptr<EffectProcessor> create_color_correction_processor();
```

In `register_builtin_processors()`, after the tint line:
```cpp
    registry.register_effect_processor<HueSaturationParams>(create_hue_saturation_processor());
    registry.register_effect_processor<VignetteParams>(create_vignette_processor());
    registry.register_effect_processor<ColorCorrectionParams>(create_color_correction_processor());
```

- [ ] **Step 8: Update dispatch in `software_effect_runner.cpp`**

In `src/backends/software/software_effect_runner.cpp`, in the `apply_effect_stack` method, add three more `else if` branches after the `BloomParams` one:

```cpp
        } else if (auto* p = std::any_cast<HueSaturationParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
        } else if (auto* p = std::any_cast<VignetteParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
        } else if (auto* p = std::any_cast<ColorCorrectionParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
```

- [ ] **Step 9: Run tests**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_renderer_tests --output-on-failure
```

Expected: 4 new effect tests pass; all prior tests still pass.

- [ ] **Step 10: Commit**

```
git add src/backends/software/utils/render_effects_processor.hpp
git add src/backends/software/processors/software_effect_processors.cpp
git add src/backends/software/processors/builtin_processors.cpp
git add src/backends/software/software_effect_runner.cpp
git add tests/renderer/effects/test_new_effects.cpp
git add tests/CMakeLists.txt
git commit -m "feat(effects): add HueSaturation, Vignette, ColorCorrection effects"
```

---

## Task 3: Mask Feather

**Files:**
- Modify: `include/chronon3d/scene/mask/mask.hpp`
- Modify: `src/scene/layer_builder.cpp`
- Modify: `src/backends/software/rasterizers/shape_rasterizer.hpp`
- Modify: `src/backends/software/rasterizers/shape_rasterizer.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/renderer/effects/test_mask_feather.cpp`:

```cpp
#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "tests/helpers/pixel_assertions.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

static std::unique_ptr<Framebuffer> render_masked(RectMaskParams mask) {
    SoftwareRenderer renderer;
    Composition comp(CompositionSpec{.width = 200, .height = 200, .duration = 1},
        [mask](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("box", [&](LayerBuilder& b) {
                b.rect("r", {.size = {200, 200}, .color = {1, 1, 1, 1}, .pos = {0, 0, 0}});
                b.mask_rect(mask);
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

TEST_CASE("Mask feather=0: hard edge, outside pixel is black") {
    RectMaskParams m;
    m.size = {100, 100};
    m.pos = {0, 0, 0};
    m.feather = 0.0f;
    auto fb = render_masked(m);
    REQUIRE(fb != nullptr);
    // Outside mask region (x=150, y=150) must be transparent/black
    Color outside = fb->get_pixel(150, 150);
    CHECK(outside.a < 0.05f);
    // Inside mask (x=50, y=50) must be white
    Color inside = fb->get_pixel(50, 50);
    CHECK(inside.r > 0.9f);
}

TEST_CASE("Mask feather=20: boundary pixel has partial alpha") {
    RectMaskParams m;
    m.size = {120, 120};
    m.pos = {40, 40, 0};
    m.feather = 20.0f;
    auto fb = render_masked(m);
    REQUIRE(fb != nullptr);
    // Pixel on the mask edge (x=40, y=100) should be partially lit — not fully black
    Color edge = fb->get_pixel(40, 100);
    // It should NOT be fully white (inside) or fully black (outside) — partial
    float lum = edge.r;
    CHECK(lum > 0.05f);   // not fully clipped
    CHECK(lum < 0.95f);   // not fully opaque
}
```

- [ ] **Step 2: Register in CMakeLists**

In `tests/CMakeLists.txt`, inside `chronon3d_renderer_tests`:

```cmake
    renderer/effects/test_mask_feather.cpp
```

- [ ] **Step 3: Run to confirm failure**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_renderer_tests --output-on-failure
```

Expected: `test_mask_feather=20` fails — edge pixel is hard-clipped to 0.

- [ ] **Step 4: Add `feather` to `Mask` and param structs**

In `include/chronon3d/scene/mask/mask.hpp`:

Add `feather` to `struct Mask`:
```cpp
struct Mask {
    MaskType type{MaskType::None};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    f32  radius{0.0f};
    bool inverted{false};
    f32  feather{0.0f};   // ADD THIS — soft edge blur radius in pixels (0 = hard)

    [[nodiscard]] bool enabled() const { return type != MaskType::None; }
};
```

Add `feather` to each param struct:
```cpp
struct RectMaskParams        { Vec2 size{100,100}; Vec3 pos{0,0,0}; bool inverted{false}; f32 feather{0.0f}; };
struct RoundedRectMaskParams { Vec2 size{100,100}; f32 radius{8}; Vec3 pos{0,0,0}; bool inverted{false}; f32 feather{0.0f}; };
struct CircleMaskParams      { f32 radius{50}; Vec3 pos{0,0,0}; bool inverted{false}; f32 feather{0.0f}; };
```

- [ ] **Step 5: Pass feather in `LayerBuilder` mask methods**

In `src/scene/layer_builder.cpp`, update the three `mask_*` methods to set `feather`:

```cpp
LayerBuilder& LayerBuilder::mask_rect(RectMaskParams p) {
    m_layer.mask.type = MaskType::Rect;
    m_layer.mask.size = p.size;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    m_layer.mask.feather = p.feather;    // ADD
    return *this;
}

LayerBuilder& LayerBuilder::mask_rounded_rect(RoundedRectMaskParams p) {
    m_layer.mask.type = MaskType::RoundedRect;
    m_layer.mask.size = p.size;
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    m_layer.mask.feather = p.feather;    // ADD
    return *this;
}

LayerBuilder& LayerBuilder::mask_circle(CircleMaskParams p) {
    m_layer.mask.type = MaskType::Circle;
    m_layer.mask.size = {p.radius * 2.0f, p.radius * 2.0f};
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    m_layer.mask.feather = p.feather;    // ADD
    return *this;
}
```

- [ ] **Step 6: Change `pixel_passes_mask` → `mask_alpha` in `shape_rasterizer.hpp`**

In `src/backends/software/rasterizers/shape_rasterizer.hpp`, change the declaration:

```cpp
// Returns alpha weight [0..1]: 1=inside, 0=outside, fractional for feathered edges.
f32 mask_alpha(const RenderState& state, i32 x, i32 y);
```

Remove or keep `pixel_passes_mask` as a thin wrapper if needed for backward compat (see Step 8).

- [ ] **Step 7: Implement `mask_alpha` in `shape_rasterizer.cpp`**

In `src/backends/software/rasterizers/shape_rasterizer.cpp`, replace `pixel_passes_mask` with:

```cpp
f32 mask_alpha(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled()) return 1.0f;

    Vec4 local = state.layer_inv_matrix *
                 Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
    Vec2 lp{local.x, local.y};

    const Mask& mask = *state.mask;
    const float feather = mask.feather;

    // Compute signed distance to mask interior (positive = inside).
    float sdf = 0.0f;
    switch (mask.type) {
        case MaskType::Rect: {
            Vec2 center{mask.pos.x + mask.size.x * 0.5f, mask.pos.y + mask.size.y * 0.5f};
            Vec2 d{std::abs(lp.x - center.x) - mask.size.x * 0.5f,
                   std::abs(lp.y - center.y) - mask.size.y * 0.5f};
            // SDF: negative inside
            sdf = -(std::max(d.x, 0.0f) + std::min(std::max(d.x, d.y), 0.0f));
            // Negate: positive = inside
            break;
        }
        case MaskType::RoundedRect: {
            Vec2 center{mask.pos.x + mask.size.x * 0.5f, mask.pos.y + mask.size.y * 0.5f};
            float r = std::min(mask.radius, std::min(mask.size.x, mask.size.y) * 0.5f);
            Vec2 q{std::abs(lp.x - center.x) - mask.size.x * 0.5f + r,
                   std::abs(lp.y - center.y) - mask.size.y * 0.5f + r};
            float outer = std::min(std::max(q.x, q.y), 0.0f) +
                          std::sqrt(std::max(0.0f, q.x) * std::max(0.0f, q.x) +
                                    std::max(0.0f, q.y) * std::max(0.0f, q.y)) - r;
            sdf = -outer;  // positive = inside
            break;
        }
        case MaskType::Circle: {
            Vec2 center{mask.pos.x + mask.radius, mask.pos.y + mask.radius};
            float dist = std::sqrt((lp.x - center.x)*(lp.x - center.x) +
                                   (lp.y - center.y)*(lp.y - center.y));
            sdf = mask.radius - dist;  // positive = inside
            break;
        }
        default:
            return 1.0f;
    }

    float alpha;
    if (feather <= 0.0f) {
        alpha = (sdf >= 0.0f) ? 1.0f : 0.0f;
    } else {
        // smoothstep over [-feather, feather]
        float t = std::clamp((sdf + feather) / (2.0f * feather), 0.0f, 1.0f);
        alpha = t * t * (3.0f - 2.0f * t);  // smoothstep
    }

    return mask.inverted ? (1.0f - alpha) : alpha;
}
```

- [ ] **Step 8: Update callers of `pixel_passes_mask` / `mask_alpha`**

The only caller is in `draw_transformed_shape()` in the same file. Find:

```cpp
            if (state && !pixel_passes_mask(*state, x, y)) continue;
```

Replace with:

```cpp
            if (state) {
                const f32 ma = mask_alpha(*state, x, y);
                if (ma <= 0.0f) continue;
                if (ma < 1.0f) {
                    // Blend pixel with partial alpha
                    // (handled below: multiply pixel color alpha by mask alpha)
                    const Color pixel_color = use_gradient
                        ? resolve_gradient_color(*fill, lp, sz, color.a * ma)
                        : Color{color.r, color.g, color.b, color.a * ma};
                    fb.set_pixel(x, y, compositor::blend(pixel_color, fb.get_pixel(x, y), BlendMode::Normal));
                    continue;
                }
            }
```

(The `continue` after partial-alpha branch prevents the normal full-alpha path from re-running for that pixel.)

- [ ] **Step 9: Run tests**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_renderer_tests --output-on-failure
```

Expected: both mask feather tests pass; all existing mask tests still pass.

- [ ] **Step 10: Full suite**

```
cmake --build build/chronon/win-debug --target chronon3d_tests -j8
ctest --test-dir build/chronon/win-debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 11: Commit**

```
git add include/chronon3d/scene/mask/mask.hpp
git add src/scene/layer_builder.cpp
git add src/backends/software/rasterizers/shape_rasterizer.hpp
git add src/backends/software/rasterizers/shape_rasterizer.cpp
git add tests/renderer/effects/test_mask_feather.cpp
git add tests/CMakeLists.txt
git commit -m "feat(mask): add feather soft-edge support via SDF-based mask_alpha()"
```
