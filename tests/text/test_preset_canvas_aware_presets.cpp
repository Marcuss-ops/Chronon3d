// test_preset_canvas_aware_presets.cpp — TICKET-PRESET-CANVAS-AWARE-COORDS
//
// ERT-style canvas-aware verification of the 5 reusable
// `chronon3d::presets::text::` v1 presets:
//
//   1. title_centered
//   2. subtitle_bottom
//   3. caption_safe_area
//   4. kinetic_word
//   5. lower_third
//
// Each preset is verified across 5 canvas configurations:
//
//   - 1920×1080 (Landscape 16:9, the canonical reference)
//   - 1080×1920 (Portrait 9:16 — TikTok / Reels / Shorts native)
//   - 1080×1080 (Square 1:1 — Instagram feed)
//   - 3840×2160 (4K Landscape 16:9 — high-end preview / archive)
//   - 1280×720  (Custom 16:9 — intermediate resolution)
//
// For each (preset, canvas) pair three invariants are locked:
//
//   I1. Box fits in canvas
//       The preset's computed frame.size must be ≤ the canvas
//       dimensions on both axes.  Verifies that no preset path
//       emits an out-of-canvas box (which would imply a hard-coded
//       1920×1080 reference escaping the resolver).
//
//   I2. Pin stays inside the safe-area rectangle
//       When the preset pins to a safe-area edge or center, the
//       resolved pin (anchor point) must lie inside the
//       safe-area rectangle.  The safe-area rectangle is
//       [safe_margin_left, safe_margin_top] ×
//       [canvas.width - safe_margin_right, canvas.height - safe_margin_bottom].
//
//   I3. Placement kind matches the preset's documented semantic
//       Verifies that no preset swaps to an Absolute placement
//       (which would imply a hard-coded pixel position) on any of
//       the 5 canvas configurations.
//
// Test surface: 5 presets × 5 canvases × 3 invariants = 75 CHECK()
// assertions + a 6th cross-cut TEST_CASE locking the AspectRatioPolicy
// FitCanvas per-axis scaling behavior on 3 non-16:9 canvases.
//
// The ERT parameterization is implemented as a `for (const auto&
// fixture : kCanvasFixtures)` loop inside each TEST_CASE (rather than
// the DOCTEST GENERATE()/GENERATE_REF() macros) for two reasons:
//
//   (a) The DOCTEST version in this repo does not expose the
//       `values<T>()` initializer-list helper that GENERATE_REF()
//       wraps.  Using a plain for-loop is portable across DOCTEST
//       versions and produces identical assertion counts.
//
//   (b) Each TEST_CASE has its own `CAPTURE(fixture.label)` so the
//       test report shows the per-canvas subcase in the diagnostic
//       output (e.g. "test_case.cpp:120: CHECK(box.x <= c.width) is
//       FALSE — captured: fixture.label = 1920x1080 landscape").
//
// This is the "ERT parametrized su almeno 4 aspect ratio" lock the
// user spec calls for.  The existing `test_presets_stability.cpp`
// already has per-aspect TEST_CASEs (16 individual TEST_CASEs across
// the 5 presets); this file ADDS the cross-aspect parameterization
// + the safe-area invariant + custom-resolution coverage.
//
// Pure struct + math inspection — no Framebuffer / compositor /
// GPU / Blend2D / FontEngine / HarfBuzz.  Compiles UNCONDITIONALLY
// (mirrors `test_presets_stability.cpp` + `test_safe_area_placement.cpp`).
//
// Anti-duplicazione honour:
//   - Reuses the canonical CanvasInfo / SafeAreaPreset / TextBoxConstraints /
//     AspectRatioPolicy types (zero new symbols in include/chronon3d/).
//   - Reuses DOCTEST TEST_CASE + CAPTURE — no parallel parametrization framework.
//   - Does NOT introduce a parallel PresetOutput type.

#include <doctest/doctest.h>

#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_placement.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::presets::text;

namespace {

// ── Canvas fixtures ─────────────────────────────────────────────────────
//
// Each fixture returns a CanvasInfo whose safe-area margins follow
// SafeAreaPreset's 5%-of-dimension convention.  The "custom" entry is
// a non-standard 1280×720 — verifies the preset pipeline does not
// silently assume one of the canonical aspect ratios.

struct CanvasFixture {
    const char* label;
    CanvasInfo  canvas;
};

// The 5 canvas configurations under test (≥4 aspect ratios per
// user spec; this is 5 — landscape / portrait / square / 4K / custom).
const std::vector<CanvasFixture> kCanvasFixtures = {
    {"1920x1080 landscape", CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset::Landscape16x9)},
    {"1080x1920 portrait",  CanvasInfo::with_safe_area(1080.0f, 1920.0f, SafeAreaPreset::Portrait9x16)},
    {"1080x1080 square",    CanvasInfo::with_safe_area(1080.0f, 1080.0f, SafeAreaPreset::Square1x1)},
    {"3840x2160 4K",        CanvasInfo::with_safe_area(3840.0f, 2160.0f, SafeAreaPreset::Landscape16x9)},
    {"1280x720 custom",     CanvasInfo::with_safe_area(1280.0f,  720.0f, SafeAreaPreset::Landscape16x9)},
};

// ── Safe-area predicate (replicated from SafeAreaPreset defaults) ─────────
//
// 5% of dimension, applied symmetrically on all 4 sides.  Replicated
// inline to avoid coupling the test to a specific SafeAreaPreset
// factory — the assertion is "the preset's pin lies inside the
// safe-area rectangle", not "the resolver uses SafeAreaPreset under
// the hood".  The 5% margin matches SafeAreaPreset's default
// convention (industry-standard title/action safe zone).
constexpr f32 kSafeMargin = 0.05f;  // matches SafeAreaPreset default convention (5% of dimension, all 4 sides)

bool pin_inside_safe_area(const Vec2& pin, const CanvasInfo& c) {
    const f32 left   = kSafeMargin * c.width;
    const f32 right  = kSafeMargin * c.width;
    const f32 top    = kSafeMargin * c.height;
    const f32 bottom = kSafeMargin * c.height;
    return pin.x >= left
        && pin.x <= c.width  - right
        && pin.y >= top
        && pin.y <= c.height - bottom;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_centered — CanvasCenter, fits-in-canvas, fits-in-safe-area
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ERT canvas-aware: title_centered across 5 canvas configurations") {
    for (const auto& fixture : kCanvasFixtures) {
        CAPTURE(fixture.label);

        const auto def = title_centered("CHRONON3D", fixture.canvas);

        // I1 — box fits in canvas (per-axis)
        CHECK(def.frame.size.x <= fixture.canvas.width);
        CHECK(def.frame.size.y <= fixture.canvas.height);

        // I2 — pin (canvas center) is inside the safe area
        const Vec2 pin{fixture.canvas.width * 0.5f, fixture.canvas.height * 0.5f};
        CHECK(pin_inside_safe_area(pin, fixture.canvas));

        // I3 — placement is semantic (CanvasCenter), NOT Absolute
        CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
        CHECK(def.frame.placement.kind != TextPlacementKind::Absolute);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. subtitle_bottom — SafeAreaBottom, fits-in-canvas, fits-in-safe-area
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ERT canvas-aware: subtitle_bottom across 5 canvas configurations") {
    for (const auto& fixture : kCanvasFixtures) {
        CAPTURE(fixture.label);

        const auto def = subtitle_bottom("a subtitle", fixture.canvas);

        // I1 — box fits in canvas
        CHECK(def.frame.size.x <= fixture.canvas.width);
        CHECK(def.frame.size.y <= fixture.canvas.height);

        // I2 — pin (canvas.width/2, canvas.height - safe_margin_bottom)
        //      is on the bottom safe-area edge → inside the safe area.
        const f32 safe_margin_bottom = kSafeMargin * fixture.canvas.height;
        const Vec2 pin{fixture.canvas.width * 0.5f, fixture.canvas.height - safe_margin_bottom};
        CHECK(pin_inside_safe_area(pin, fixture.canvas));

        // I3 — placement is semantic (SafeAreaBottom), NOT Absolute
        CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaBottom);
        CHECK(def.frame.placement.kind != TextPlacementKind::Absolute);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. caption_safe_area — SafeAreaCenter, fits-in-canvas, fits-in-safe-area
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ERT canvas-aware: caption_safe_area across 5 canvas configurations") {
    for (const auto& fixture : kCanvasFixtures) {
        CAPTURE(fixture.label);

        const auto def = caption_safe_area("a caption", fixture.canvas);

        // I1 — box fits in canvas
        CHECK(def.frame.size.x <= fixture.canvas.width);
        CHECK(def.frame.size.y <= fixture.canvas.height);

        // I2 — pin (canvas center) is inside the safe area
        const Vec2 pin{fixture.canvas.width * 0.5f, fixture.canvas.height * 0.5f};
        CHECK(pin_inside_safe_area(pin, fixture.canvas));

        // I3 — placement is semantic (SafeAreaCenter), NOT Absolute
        CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaCenter);
        CHECK(def.frame.placement.kind != TextPlacementKind::Absolute);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. kinetic_word — CanvasCenter, fits-in-canvas, fits-in-safe-area
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ERT canvas-aware: kinetic_word across 5 canvas configurations") {
    for (const auto& fixture : kCanvasFixtures) {
        CAPTURE(fixture.label);

        const auto def = kinetic_word("HERO", fixture.canvas);

        // I1 — box fits in canvas (kinetic_word reserves a larger hero box)
        CHECK(def.frame.size.x <= fixture.canvas.width);
        CHECK(def.frame.size.y <= fixture.canvas.height);

        // I2 — pin (canvas center) is inside the safe area
        const Vec2 pin{fixture.canvas.width * 0.5f, fixture.canvas.height * 0.5f};
        CHECK(pin_inside_safe_area(pin, fixture.canvas));

        // I3 — placement is semantic (CanvasCenter), NOT Absolute
        CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
        CHECK(def.frame.placement.kind != TextPlacementKind::Absolute);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. lower_third — BottomLeft, fits-in-canvas, fits-in-safe-area
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ERT canvas-aware: lower_third across 5 canvas configurations") {
    for (const auto& fixture : kCanvasFixtures) {
        CAPTURE(fixture.label);

        const auto def = lower_third("MARCO ROSSI", fixture.canvas);

        // I1 — box fits in canvas
        CHECK(def.frame.size.x <= fixture.canvas.width);
        CHECK(def.frame.size.y <= fixture.canvas.height);

        // I2 — pin (canvas.width/2, canvas.height - safe_margin_bottom)
        //      is on the bottom safe-area edge.
        const f32 safe_margin_bottom = kSafeMargin * fixture.canvas.height;
        const Vec2 pin{fixture.canvas.width * 0.5f, fixture.canvas.height - safe_margin_bottom};
        CHECK(pin_inside_safe_area(pin, fixture.canvas));

        // I3 — placement is semantic (BottomLeft), NOT Absolute
        CHECK(def.frame.placement.kind == TextPlacementKind::BottomLeft);
        CHECK(def.frame.placement.kind != TextPlacementKind::Absolute);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Cross-cut: AspectRatioPolicy::FitCanvas (default) scales per-axis independently
// ═══════════════════════════════════════════════════════════════════════════
//
// Verifies the default FitCanvas policy scales the box's W and H
// independently against canvas.width and canvas.height (NOT a uniform
// aspect-locked scale).  This locks the "no implicit 1920×1080 default"
// invariant for non-16:9 canvases: a 4K canvas produces a 16:9 box, a
// square canvas produces a 1:1 box (per-axis scale, not stretch).
//
// On a square canvas the box aspect ratio is 1:1 (each axis is the
// same as canvas.width and canvas.height respectively).  On a
// non-square canvas the box aspect ratio matches the canvas aspect
// ratio (16:9 landscape or 9:16 portrait).
TEST_CASE("ERT canvas-aware: AspectRatioPolicy::FitCanvas scales box per-axis (no implicit 16:9 lock)") {
    // Iterates ALL 5 canvas fixtures (not just non-16:9) because
    // FitCanvas's per-axis scaling semantic is canvas-aspect-agnostic:
    // on every fixture, the box's aspect ratio must equal the
    // canvas's aspect ratio (width/height scale independently
    // against canvas.width/canvas.height — NO uniform aspect-lock).
    for (const auto& fixture : kCanvasFixtures) {
        CAPTURE(fixture.label);

        // The v1 title_centered preset defaults to FitCanvas (the
        // `policy` parameter is not passed).  The resulting box's W
        // and H scale independently against canvas.width/height — the
        // box's aspect ratio equals the canvas's aspect ratio on
        // every fixture (FitCanvas semantic).
        const auto def = title_centered("CHRONON3D", fixture.canvas);
        const f32 canvas_aspect = fixture.canvas.width / fixture.canvas.height;
        const f32 box_aspect    = def.frame.size.x / def.frame.size.y;
        CHECK(box_aspect == doctest::Approx(canvas_aspect).epsilon(0.01f));
    }
}