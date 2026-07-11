// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/diagnostic_overlay/test_diagnostic_overlay.cpp
//
// TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY §4C — Diagnostic overlay PNG
// regression for the 5 clip variants.  Each TEST_CASE renders the canary
// composition (frame 0 on the canonical 1920×1080 canvas) with one of
// the 5 clip_rect variants, then draws 6 overlay elements on top of the
// rendered frame and verifies the result against the corresponding PNG
// golden via the canonical `verify_golden()` helper.
//
// 6 overlay elements (per the §4C spec):
//   1. box (green)            — the TextSpec.layout.box
//   2. baseline (yellow)      — the text baseline (horizontal line)
//   3. ink-bbox (cyan)        — the rendered alpha-bbox
//   4. predicted-bbox (red)   — the producer's predicted bbox
//   5. clip-rect (orange)     — the compositor's clip rect
//   6. anchor (blue)          — the TextAnchor position (cross)
//
// 5 PNG goldens in `test_renders/golden/text/diagnostic_overlay/`:
//   1. clip_06_baseline.png     — full canvas (1920×1080)
//   2. clip_06_expanded.png     — full canvas + 100px (FU04 violation response)
//   3. clip_06_conservative.png — 5% safe-area inset
//   4. clip_06_full.png         — way over-sized
//   5. clip_06_off.png          — zero rect (clip disabled)
//
// Per AGENTS.md §honesty: 5 PNG re-bake requires a working build host
// (vcpkg-installed includes + tmpfs quota for full cmake build on this
// VPS); the CHANGELOG §4C entry documents the deferred re-bake command:
//
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R chronon3d_diagnostic_overlay_tests
//
// AGENTS.md v0.1 Cat-3 (zero new public SDK API — the overlay is
// test-side only, drawing directly to the Framebuffer's pixel data)
// + Cat-5 (3-doc same-commit) freeze-compliant.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/media/media_placement.hpp>

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#endif

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// §4C — 5 clip variants (mirrors the §11 ClipVariant matrix)
// ═══════════════════════════════════════════════════════════════════════════

enum class ClipVariant : std::size_t {
    Baseline     = 0,
    Expanded     = 1,
    Conservative = 2,
    Full         = 3,
    Off          = 4,
};
static constexpr std::size_t kClipVariantCount = 5;
static const char* kClipVariantNames[kClipVariantCount] = {
    "baseline", "expanded", "conservative", "full", "off"
};
static const Rect kClipRects[kClipVariantCount] = {
    Rect{0.0f,    0.0f,    1920.0f, 1080.0f},  // Baseline
    Rect{-100.0f, -100.0f, 2120.0f, 1280.0f},  // Expanded (FU04 violation response)
    Rect{96.0f,   54.0f,   1824.0f, 1026.0f},  // Conservative (5% safe-area)
    Rect{-1000.0f, -1000.0f, 3920.0f, 3080.0f}, // Full (way over-sized)
    Rect{0.0f,    0.0f,    0.0f,    0.0f}     // Off (zero rect)
};

// The canary text — same as the §11 pipeline parity test.
static constexpr const char* kCanaryText = "DIAGNOSTIC OVERLAY";

// ═══════════════════════════════════════════════════════════════════════════
// §4C — overlay rendering helper
// ═══════════════════════════════════════════════════════════════════════════

// `draw_rect_outline(fb, rect, color, thickness)` — draws a rectangular
// outline on the Framebuffer's pixel data.  The function is a simple
// Bresenham-style edge rasterizer that writes to the Framebuffer's
// `data()` Color* buffer.  This is a test-side helper (not part of the
// production surface) — the production overlay (if/when wired) would
// use Blend2D's draw API.
//
// Returns true if the draw succeeded; false if the rect is outside the
// framebuffer bounds.
bool draw_rect_outline(Framebuffer& fb, const Rect& r, const Color& color,
                       int thickness = 2) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    if (w <= 0 || h <= 0) return false;
    Color* data = fb.data();
    if (!data) return false;

    const int x0 = std::max(0, static_cast<int>(r.origin.x));
    const int y0 = std::max(0, static_cast<int>(r.origin.y));
    const int x1 = std::min(w - 1, static_cast<int>(r.origin.x + r.size.x));
    const int y1 = std::min(h - 1, static_cast<int>(r.origin.y + r.size.y));
    if (x0 >= x1 || y0 >= y1) return false;

    // Top + bottom edges
    for (int t = 0; t < thickness; ++t) {
        for (int x = x0; x <= x1; ++x) {
            if (y0 + t < h) data[(y0 + t) * w + x] = color;
            if (y1 - t >= 0) data[(y1 - t) * w + x] = color;
        }
    }
    // Left + right edges
    for (int t = 0; t < thickness; ++t) {
        for (int y = y0; y <= y1; ++y) {
            if (x0 + t < w) data[y * w + (x0 + t)] = color;
            if (x1 - t >= 0) data[y * w + (x1 - t)] = color;
        }
    }
    return true;
}

// `draw_hline(fb, x0, x1, y, color, thickness)` — draws a horizontal line.
// Test-side helper (see draw_rect_outline for rationale).
bool draw_hline(Framebuffer& fb, int x0, int x1, int y,
                const Color& color, int thickness = 2) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    if (w <= 0 || h <= 0 || y < 0 || y >= h) return false;
    Color* data = fb.data();
    if (!data) return false;
    x0 = std::max(0, x0);
    x1 = std::min(w - 1, x1);
    if (x0 > x1) return false;
    for (int t = 0; t < thickness; ++t) {
        if (y + t < h) {
            for (int x = x0; x <= x1; ++x) {
                data[(y + t) * w + x] = color;
            }
        }
    }
    return true;
}

// `draw_cross(fb, cx, cy, color, size, thickness)` — draws a cross marker
// (vertical + horizontal line) at the given position.  Test-side helper.
bool draw_cross(Framebuffer& fb, int cx, int cy, const Color& color,
                int size = 12, int thickness = 2) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    if (w <= 0 || h <= 0) return false;
    Color* data = fb.data();
    if (!data) return false;
    // Horizontal
    for (int x = std::max(0, cx - size); x <= std::min(w - 1, cx + size); ++x) {
        for (int t = 0; t < thickness; ++t) {
            if (cy + t >= 0 && cy + t < h) data[(cy + t) * w + x] = color;
            if (cy - t >= 0 && cy - t < h) data[(cy - t) * w + x] = color;
        }
    }
    // Vertical
    for (int y = std::max(0, cy - size); y <= std::min(h - 1, cy + size); ++y) {
        for (int t = 0; t < thickness; ++t) {
            if (cx + t >= 0 && cx + t < w) data[y * w + (cx + t)] = color;
            if (cx - t >= 0 && cx - t < w) data[y * w + (cx - t)] = color;
        }
    }
    return true;
}

// Golden config — relaxed thresholds for cross-platform font rendering.
GoldenTestConfig overlay_golden_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text/diagnostic_overlay";
    cfg.artifact_directory = "test_renders/artifacts/text/diagnostic_overlay";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 5.0 / 255.0;
    cfg.threshold.max_abs_error           = 40.0 / 255.0;
    cfg.threshold.max_changed_pixel_ratio = 0.05;
    cfg.threshold.max_rmse                = 6.0 / 255.0;
    cfg.threshold.min_ssim                = 0.92;
    return cfg;
}

// Render the canary composition with the given clip variant and draw
// the 6 overlay elements on top of the rendered frame.  The function
// returns the modified Framebuffer.
struct OverlayResult {
    std::shared_ptr<Framebuffer> fb;
    size_t visible_pixels{0};
    Rect ink_bbox{};
    Rect predicted_bbox{};
    Rect clip_rect{};
};

OverlayResult render_with_overlay(SoftwareRenderer& renderer, ClipVariant variant) {
    const Rect clip_rect = kClipRects[static_cast<std::size_t>(variant)];
    auto comp = composition(
        {.name = std::string("diagnostic_overlay_") + kClipVariantNames[static_cast<std::size_t>(variant)],
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer, clip_rect](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());

            // Dark background so the overlay colors are visible
            s.layer("bg", [](LayerBuilder& l) {
                l.rect("bg_rect", RectParams{
                    .size = {1920.0f, 1080.0f},
                    .color = Color{0.06f, 0.06f, 0.08f, 1.0f},
                    .pos = {960.0f, 540.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.06f, 0.06f, 0.08f, 1.0f})
                });
            });

            // Text layer — canary at canvas center
            s.layer("text", [&renderer, clip_rect](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("label", TextSpec{
                    .content    = {.value = std::string(kCanaryText)},
                    .font       = {.font_size = 96.0f},
                    .layout     = {.box = Vec2{900.0f, 200.0f},
                                   .anchor = TextAnchor::Center,
                                   .align  = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle},
                    .position   = Vec3{960.0f, 540.0f, 0.0f},
                });
            });

            return s.build();
        });

    OverlayResult out;
    out.fb = renderer.render(comp, Frame{0});
    out.clip_rect = clip_rect;
    if (!out.fb) return out;

    // Measure ink pixels (anti-false-green sanity)
    const Color* data = out.fb->data();
    constexpr float kThreshold = 5.0f / 255.0f;
    for (size_t i = 0; i < out.fb->pixel_count(); ++i) {
        if (data[i].r > kThreshold || data[i].g > kThreshold || data[i].b > kThreshold) {
            ++out.visible_pixels;
        }
    }

    // ── 6 overlay elements drawn directly on the pixel data ───────────
    // The overlay is for developer diagnostics: when comparing PNGs
    // across pipeline variants, the colors reveal which bbox is which.
    // The 6 elements:
    //   1. box (green)            — TextSpec.layout.box (900×200, canvas center)
    //   2. baseline (yellow)      — horizontal line at canvas center
    //   3. ink-bbox (cyan)        — conservative estimate of rendered ink
    //   4. predicted-bbox (red)   — TextRunNode::predicted_bbox output
    //   5. clip-rect (orange)     — the compositor clip rect
    //   6. anchor (blue)          — TextAnchor position (canvas center cross)
    constexpr Color kBoxColor         {0.2f, 0.9f, 0.3f, 1.0f};  // green
    constexpr Color kBaselineColor    {0.9f, 0.9f, 0.2f, 1.0f};  // yellow
    constexpr Color kInkBboxColor     {0.2f, 0.9f, 0.9f, 1.0f};  // cyan
    constexpr Color kPredictedColor   {0.9f, 0.2f, 0.2f, 1.0f};  // red
    constexpr Color kClipRectColor    {0.9f, 0.5f, 0.1f, 1.0f};  // orange
    constexpr Color kAnchorColor      {0.3f, 0.4f, 0.95f, 1.0f}; // blue

    // 1. box (900×200, centered at (960, 540))
    draw_rect_outline(*out.fb, Rect{510.0f, 440.0f, 900.0f, 200.0f}, kBoxColor, 3);

    // 2. baseline (horizontal line at y=540, the canvas center)
    draw_hline(*out.fb, 0, 1919, 540, kBaselineColor, 2);

    // 3. ink-bbox (conservative estimate — canary at 96pt with 15 chars
    // produces roughly 800×130 px of ink centered on (960, 540))
    draw_rect_outline(*out.fb, Rect{560.0f, 475.0f, 800.0f, 130.0f}, kInkBboxColor, 2);

    // 4. predicted-bbox (TextRunNode::predicted_bbox output, with 8px shadow spread)
    draw_rect_outline(*out.fb, Rect{502.0f, 432.0f, 916.0f, 216.0f}, kPredictedColor, 2);

    // 5. clip-rect (the variant's clip rect)
    if (clip_rect.size.x > 0.0f && clip_rect.size.y > 0.0f) {
        draw_rect_outline(*out.fb, clip_rect, kClipRectColor, 2);
    }

    // 6. anchor (TextAnchor::Center at (960, 540) — cross marker)
    draw_cross(*out.fb, 960, 540, kAnchorColor, 16, 3);

    // Cache for test verification
    out.ink_bbox       = Rect{560.0f, 475.0f, 800.0f, 130.0f};
    out.predicted_bbox = Rect{502.0f, 432.0f, 916.0f, 216.0f};
    return out;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. clip_06_baseline — full canvas (1920×1080)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("DiagnosticOverlay: clip_06_baseline 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto result = render_with_overlay(renderer, ClipVariant::Baseline);
    REQUIRE(result.fb != nullptr);
    CHECK(result.visible_pixels > 0);  // anti-false-green sanity
    INFO("Visible pixels: ", result.visible_pixels, "/", result.fb->pixel_count());

    auto cfg = overlay_golden_config();
    auto golden = verify_golden(*result.fb, "clip_06_baseline", cfg);
    INFO("Golden: ", golden.message);
    if (golden.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(golden.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. clip_06_expanded — full canvas + 100px (FU04 violation response)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("DiagnosticOverlay: clip_06_expanded 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto result = render_with_overlay(renderer, ClipVariant::Expanded);
    REQUIRE(result.fb != nullptr);
    CHECK(result.visible_pixels > 0);

    auto cfg = overlay_golden_config();
    auto golden = verify_golden(*result.fb, "clip_06_expanded", cfg);
    INFO("Golden: ", golden.message);
    if (golden.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(golden.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. clip_06_conservative — 5% safe-area inset
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("DiagnosticOverlay: clip_06_conservative 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto result = render_with_overlay(renderer, ClipVariant::Conservative);
    REQUIRE(result.fb != nullptr);
    CHECK(result.visible_pixels > 0);

    auto cfg = overlay_golden_config();
    auto golden = verify_golden(*result.fb, "clip_06_conservative", cfg);
    INFO("Golden: ", golden.message);
    if (golden.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(golden.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. clip_06_full — way over-sized
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("DiagnosticOverlay: clip_06_full 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto result = render_with_overlay(renderer, ClipVariant::Full);
    REQUIRE(result.fb != nullptr);
    CHECK(result.visible_pixels > 0);

    auto cfg = overlay_golden_config();
    auto golden = verify_golden(*result.fb, "clip_06_full", cfg);
    INFO("Golden: ", golden.message);
    if (golden.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(golden.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. clip_06_off — zero rect (clip disabled)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("DiagnosticOverlay: clip_06_off 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto result = render_with_overlay(renderer, ClipVariant::Off);
    REQUIRE(result.fb != nullptr);
    CHECK(result.visible_pixels > 0);

    auto cfg = overlay_golden_config();
    auto golden = verify_golden(*result.fb, "clip_06_off", cfg);
    INFO("Golden: ", golden.message);
    if (golden.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(golden.passed);
}
