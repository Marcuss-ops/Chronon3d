# LilDirk Neon Badge — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `DarkGridBgParams` + `NeonBadgeParams` as reusable modular templates and rewrite `LilDirkDemo` to match the reference video (dark grid bg, single red box, white pulsing glow, XYZ tilt).

**Architecture:** Two new public symbols in `builder_params.hpp` (`DarkGridBgParams`) and a new header `neon_badge_templates.hpp` (`NeonBadgeParams` + two inline builder functions). `LilDirkDemo` becomes a 10-line consumer.

**Tech Stack:** C++17, existing Chrono primitives (`FakeBox3D`, `FakeExtrudedText`, `GridPlane`, `LayerBuilder::with_glow`).

---

## File Map

| Action | File |
|--------|------|
| Modify | `include/chronon3d/scene/builders/builder_params.hpp` |
| Create | `include/chronon3d/templates/neon_badge_templates.hpp` |
| Modify | `examples/proofs/fake3d/text3d_serious_proof.cpp` |

---

## Task 1: Add `DarkGridBgParams` to `builder_params.hpp`

**Files:**
- Modify: `include/chronon3d/scene/builders/builder_params.hpp`

- [ ] **Step 1: Add struct at end of file, before closing `}`**

Open `include/chronon3d/scene/builders/builder_params.hpp`. After the `GridPlaneParams` struct (around line 96), before the closing `}` of the namespace, add:

```cpp
struct DarkGridBgParams {
    Color bg_color   {0.098f, 0.098f, 0.11f, 1.f};
    Color grid_color {1.f,    1.f,    1.f,   0.045f};
    f32   spacing    {80.f};
    f32   extent     {4000.f};
};
```

- [ ] **Step 2: Build to verify no compile errors**

```bash
cmake --build build --target chronon3d_lib -j4 2>&1 | tail -20
```
Expected: `[100%] Built target chronon3d_lib` with no errors.

- [ ] **Step 3: Commit**

```bash
git add include/chronon3d/scene/builders/builder_params.hpp
git commit -m "feat: add DarkGridBgParams to builder_params"
```

---

## Task 2: Create `neon_badge_templates.hpp`

**Files:**
- Create: `include/chronon3d/templates/neon_badge_templates.hpp`

- [ ] **Step 1: Create the file with full content**

```cpp
#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <cmath>

namespace chronon3d {
namespace templates {

// ─── dark_grid_background ────────────────────────────────────────────────────
// Flat dark background with subtle crosshatch grid.
// Sets up a front-facing Camera2_5D internally; call before any 3D layers.

inline void dark_grid_background(SceneBuilder& s, const FrameContext& ctx,
                                  const DarkGridBgParams& p = {}) {
    const f32 H = static_cast<f32>(ctx.height > 0 ? ctx.height : 720);

    Camera2_5D cam;
    cam.enabled                    = true;
    cam.position                   = {0.f, 0.f, -H};
    cam.point_of_interest          = {0.f, 0.f, 0.f};
    cam.point_of_interest_enabled  = true;
    cam.zoom                       = H;
    s.camera_2_5d(cam);

    s.layer("nbg_bg", [&p](LayerBuilder& l) {
        l.rect("r", {.size = {99999.f, 99999.f}, .color = p.bg_color});
    });

    s.layer("nbg_grid", [&p](LayerBuilder& l) {
        l.enable_3d().position({0.f, 0.f, 0.f}).grid_plane("g", {
            .pos           = {0.f, 0.f, 0.f},
            .axis          = PlaneAxis::XY,
            .extent        = p.extent,
            .spacing       = p.spacing,
            .color         = p.grid_color,
            .fade_distance = 0.0f,
        });
    });
}

// ─── NeonBadgeParams ─────────────────────────────────────────────────────────

struct NeonBadgeParams {
    std::string text     {"TEXT"};
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    f32         font_size{200.f};
    Color       text_color{1.f, 1.f, 1.f, 1.f};

    // Red box
    Color box_color  {0.86f, 0.08f, 0.12f, 1.f};
    f32   box_width  {680.f};           // total width of the red rect
    Vec2  box_padding{0.f,  24.f};      // extra padding (x unused, y adds height)
    f32   box_depth  {2.f};

    // Glow (white halo around the box, pulsing)
    Color glow_color    {1.f, 1.f, 1.f, 1.f};
    f32   glow_radius   {40.f};
    f32   glow_base     {0.55f};        // minimum glow intensity
    f32   glow_amplitude{0.25f};        // pulse swing above/below base
    f32   glow_speed    {1.8f};         // Hz

    // 3D tilt
    bool  enable_tilt{true};
    f32   tilt_x_amp {5.f};
    f32   tilt_y_amp {4.f};
    f32   tilt_z_amp {2.f};

    // Decorative bars (top + bottom, wipe-in easeOutBack)
    bool  enable_bars      {true};
    f32   bar_height       {22.f};
    f32   bar_wipe_duration{0.6f};      // seconds
};

// ─── neon_badge ──────────────────────────────────────────────────────────────
// Builds: red box with pulsing glow, optional top/bottom bars, white text.
// All elements share the same XYZ tilt — they move as a rigid group.
// Layer name prefix: "nb_"

inline void neon_badge(SceneBuilder& s, const FrameContext& ctx,
                        const NeonBadgeParams& p = {}) {
    const f32 fps  = static_cast<f32>(ctx.frame_rate.numerator) /
                     static_cast<f32>(ctx.frame_rate.denominator > 0
                                      ? ctx.frame_rate.denominator : 1);
    const f32 time = static_cast<f32>(ctx.frame) / fps;

    // Glow pulse: sinusoidal intensity
    const f32 glow_intensity = p.glow_base +
        p.glow_amplitude * std::sin(time * p.glow_speed * 6.28318f);

    // XYZ tilt angles (shared by all layers)
    const f32 rot_x = p.enable_tilt ? std::sin(time * 1.2f) * p.tilt_x_amp : 0.f;
    const f32 rot_y = p.enable_tilt ? std::cos(time * 0.9f) * p.tilt_y_amp : 0.f;
    const f32 rot_z = p.enable_tilt ? std::sin(time * 0.5f) * p.tilt_z_amp : 0.f;

    // Bar wipe-in with easeOutBack overshoot
    const f32 t_bars = std::min(time / std::max(p.bar_wipe_duration, 0.001f), 1.0f);
    auto ease_out_back = [](f32 t) -> f32 {
        constexpr f32 c1 = 1.70158f, c3 = c1 + 1.f;
        return 1.f + c3 * std::pow(t - 1.f, 3.f) + c1 * std::pow(t - 1.f, 2.f);
    };
    const f32 bar_scale = std::max(0.f, ease_out_back(t_bars));

    const f32 box_h = p.font_size * 0.72f + p.box_padding.y * 2.f;

    // ── Red box with pulsing glow ──
    s.layer("nb_box", [&](LayerBuilder& l) {
        l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
        l.fake_box3d("box", {
            .pos   = {0.f, 0.f, 0.f},
            .size  = {p.box_width, box_h},
            .depth = p.box_depth,
            .color = p.box_color,
        });
        l.with_glow({
            .enabled   = true,
            .radius    = p.glow_radius,
            .intensity = glow_intensity,
            .color     = p.glow_color,
        });
    });

    // ── Decorative bars ──
    if (p.enable_bars) {
        const f32 bar_w = p.box_width * bar_scale;
        const f32 bar_y = box_h * 0.5f + p.bar_height * 0.5f + 4.f;

        s.layer("nb_bar_t", [&](LayerBuilder& l) {
            l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
            l.fake_box3d("b", {
                .pos   = {0.f,  bar_y, 0.f},
                .size  = {bar_w, p.bar_height},
                .depth = 2.f,
                .color = p.box_color,
            });
        });

        s.layer("nb_bar_b", [&](LayerBuilder& l) {
            l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
            l.fake_box3d("b", {
                .pos   = {0.f, -bar_y, 0.f},
                .size  = {bar_w, p.bar_height},
                .depth = 2.f,
                .color = p.box_color,
            });
        });
    }

    // ── White text ──
    s.layer("nb_text", [&](LayerBuilder& l) {
        l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
        l.fake_extruded_text("t", {
            .text              = p.text,
            .font_path         = p.font_path,
            .font_size         = p.font_size,
            .depth             = 0,
            .extrude_z_step    = 0.f,
            .front_color       = p.text_color,
            .side_color        = {0.8f, 0.8f, 0.8f, 1.f},
            .bevel_size        = 0.f,
            .highlight_opacity = 0.f,
        });
    });
}

} // namespace templates
} // namespace chronon3d
```

- [ ] **Step 2: Build to verify no compile errors**

```bash
cmake --build build --target chronon3d_lib -j4 2>&1 | tail -20
```
Expected: `[100%] Built target chronon3d_lib`.

- [ ] **Step 3: Commit**

```bash
git add include/chronon3d/templates/neon_badge_templates.hpp
git commit -m "feat: add neon_badge_templates (DarkGridBg + NeonBadge)"
```

---

## Task 3: Rewrite `LilDirkDemo` in `text3d_serious_proof.cpp`

**Files:**
- Modify: `examples/proofs/fake3d/text3d_serious_proof.cpp`

- [ ] **Step 1: Add include at top of file**

Find the existing `#include` block at the top of `text3d_serious_proof.cpp` and add:

```cpp
#include <chronon3d/templates/neon_badge_templates.hpp>
```

- [ ] **Step 2: Replace the entire `LilDirkDemo` function**

Find the block from `// ─── LilDirk-style demo` (line ~434) through the closing `}` of `LilDirkDemo()` (line ~516) and replace it entirely with:

```cpp
// ─── LilDirk-style demo ──────────────────────────────────────────────────────
// Matches reference video: dark grid bg, red box, white pulsing glow, XYZ tilt.
static Composition LilDirkDemo() {
    constexpr int kW = 1280, kH = 720;
    constexpr int kDur = 210;  // 7 s @ 30 fps

    return composition({.name="LilDirkDemo", .width=kW, .height=kH, .duration=kDur},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        templates::dark_grid_background(s, ctx, {});

        templates::neon_badge(s, ctx, {
            .text       = "LIL DIRK",
            .box_color  = {0.86f, 0.08f, 0.12f, 1.f},
            .glow_color = Color::white(),
        });

        return s.build();
    });
}
```

- [ ] **Step 3: Build the full CLI target**

```bash
cmake --build build --target chronon3d_cli -j4 2>&1 | tail -20
```
Expected: `[100%] Built target chronon3d_cli` with no errors.

- [ ] **Step 4: Render frame 30 (1 second in) and spot-check visually**

```bash
./build/chronon3d_cli render LilDirkDemo --frame 30 --out /tmp/lildirk_f30.png
```
Expected output file contains: dark grid background, red rectangle center, white text "LIL DIRK", white glow halo around box.

- [ ] **Step 5: Render frame 0 to verify bar wipe-in (bars should be narrow)**

```bash
./build/chronon3d_cli render LilDirkDemo --frame 0 --out /tmp/lildirk_f00.png
```
Expected: bars are at ~0px width (wipe-in has just started).

- [ ] **Step 6: Render frame 90 to verify glow pulse (different intensity than frame 30)**

```bash
./build/chronon3d_cli render LilDirkDemo --frame 90 --out /tmp/lildirk_f90.png
```
Expected: glow visibly brighter or dimmer than frame 30.

- [ ] **Step 7: Commit**

```bash
git add examples/proofs/fake3d/text3d_serious_proof.cpp
git commit -m "feat: rewrite LilDirkDemo using neon_badge + dark_grid_background templates"
```
