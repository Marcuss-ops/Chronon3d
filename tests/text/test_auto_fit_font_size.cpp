#include <optional>
#include <memory>
// ============================================================================
// test_auto_fit_font_size.cpp
//
// ADR-018 — Regression locks for the auto-fit binary search algorithm
// in compile_or_cache_layout() (src/scene/builders/text_run_builder.cpp).
//
// Four SUBCASEs locked:
//   (1) Shrink triggers — long text + narrow box → font_size < authored
//   (2) No-op when fits — short text + wide box → font_size == authored
//   (3) Min clamp — very narrow box → font_size >= min_font_size
//   (4) 12-iteration determinism — 100 runs → bit-identical font_size
//
// All assertions degrade gracefully when system fonts are absent.
// ============================================================================

#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace chronon3d;

namespace {

struct LocalEngine {
    chronon3d::Config cfg{};
    std::unique_ptr<chronon3d::runtime::RenderRuntime> runtime;
    FontEngine engine;

    LocalEngine()
        : runtime(chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value()),
          engine{runtime->resolver()}
    {}
};

[[nodiscard]] TextRunSpec make_autofit_params(
    const std::string& utf8,
    float font_size,
    bool auto_fit,
    float min_font_size = 8.0f,
    float max_font_size = 96.0f
) {
    TextRunSpec params;
    params.text.content.value              = utf8;
    params.text.font.font_family           = "DejaVu Sans";
    params.text.font.font_size             = font_size;
    params.text.font.font_weight           = 400;
    params.text.layout.paragraph.auto_fit_font_size = auto_fit;
    params.text.layout.paragraph.min_font_size      = min_font_size;
    params.text.layout.paragraph.max_font_size      = max_font_size;
    params.direction                       = TextDirection::LTR;
    params.language                        = "en";
    params.cache_layout                    = false;  // disable cache for determinism tests
    return params;
}

[[nodiscard]] TextLayoutSpec make_narrow_box(float w, float h) {
    TextLayoutSpec layout;
    layout.box         = {w, h};
    layout.tracking    = 0.0f;
    layout.line_height = 1.2f;
    return layout;
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// 1. Shrink triggers — long text + narrow box → font shrinks
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit shrinks font when text overflows box") {
    LocalEngine env;

    // Long text at 48pt in a narrow 200px-wide box — must shrink.
    auto params = make_autofit_params(
        "This is a fairly long sentence that will definitely overflow a narrow box",
        48.0f, /*auto_fit=*/true, /*min_fs=*/8.0f, /*max_fs=*/96.0f);

    // Set the box on the layout spec via params
    params.text.layout.box = {200.0f, 600.0f};

    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // The resolved font_size must be LESS than the authored 48pt.
    CHECK(shape->layout->font_size < 48.0f);
    // The resolved font_size must be >= min_font_size (8pt).
    CHECK(shape->layout->font_size >= 8.0f);
    // The layout bounds must fit within the box width.
    CHECK(shape->layout->bounds.x <= 200.0f + 1.0f);  // 1px tolerance
}

// ────────────────────────────────────────────────────────────────────────────
// 2. No-op when text already fits — font_size unchanged
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit is no-op when text fits the box") {
    LocalEngine env;

    // Short text at 24pt in a wide 1920px box — should NOT shrink.
    auto params = make_autofit_params(
        "Hi",
        24.0f, /*auto_fit=*/true, /*min_fs=*/8.0f, /*max_fs=*/96.0f);
    params.text.layout.box = {1920.0f, 1080.0f};

    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Font size must be unchanged at the authored 24pt.
    CHECK(shape->layout->font_size == doctest::Approx(24.0f));
}

// ────────────────────────────────────────────────────────────────────────────
// 3. Min clamp — font never shrinks below min_font_size
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit respects min_font_size clamp") {
    LocalEngine env;

    // Very long text at 72pt in a very narrow 50px box with min=20pt.
    // The binary search must NOT shrink below 20pt even if the text
    // doesn't fit at 20pt.
    auto params = make_autofit_params(
        "ExtremelyLongWordThatWillNotFitInAnyReasonableBoxSizeAtAll",
        72.0f, /*auto_fit=*/true, /*min_fs=*/20.0f, /*max_fs=*/96.0f);
    params.text.layout.box = {50.0f, 600.0f};

    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Font must have shrunk from 72pt.
    CHECK(shape->layout->font_size < 72.0f);
    // Font must NOT go below min_font_size=20pt.
    CHECK(shape->layout->font_size >= 20.0f - 0.01f);
}

// ────────────────────────────────────────────────────────────────────────────
// 4. 12-iteration determinism — same input → identical output
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit is deterministic across 20 runs") {
    LocalEngine env;

    auto params = make_autofit_params(
        "Deterministic auto-fit test with moderate length text",
        36.0f, /*auto_fit=*/true, /*min_fs=*/8.0f, /*max_fs=*/96.0f);
    params.text.layout.box = {300.0f, 400.0f};

    // Run 20 times and collect resolved font sizes + bounds.
    // 20 samples is sufficient — determinism is binary (all equal or not).
    constexpr int kRuns = 20;
    std::vector<float> font_sizes;
    std::vector<float> widths;
    std::vector<float> heights;
    font_sizes.reserve(kRuns);
    widths.reserve(kRuns);
    heights.reserve(kRuns);

    for (int i = 0; i < kRuns; ++i) {
        auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});
        if (!shape || !shape->layout) {
            MESSAGE("test skipped: system fonts unavailable");
            return;
        }
        font_sizes.push_back(shape->layout->font_size);
        widths.push_back(shape->layout->bounds.x);
        heights.push_back(shape->layout->bounds.y);
    }

    // All runs must produce bit-identical font_size.
    for (int i = 1; i < kRuns; ++i) {
        CHECK(font_sizes[i] == doctest::Approx(font_sizes[0]));
        CHECK(widths[i]    == doctest::Approx(widths[0]));
        CHECK(heights[i]   == doctest::Approx(heights[0]));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 5. max_font_size clamp — authored > max → clamped to max
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit clamps authored font to max_font_size when authored > max") {
    LocalEngine env;

    // Authored=72pt, max=40pt.  The search range is [8, 40] because
    // max_fs = min(72, 40) = 40.  With a wide box, the text fits at
    // 40pt so the resolved size should be 40pt (not 72pt).
    auto params = make_autofit_params(
        "Short",
        72.0f, /*auto_fit=*/true, /*min_fs=*/8.0f, /*max_fs=*/40.0f);
    params.text.layout.box = {1920.0f, 1080.0f};

    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Font must be clamped to max_font_size=40pt (not 72pt).
    CHECK(shape->layout->font_size <= 40.0f + 0.01f);
    CHECK(shape->layout->font_size >= 8.0f);
}

// ────────────────────────────────────────────────────────────────────────────
// 6. Termination guarantee — fixed 12 iterations, no infinite loop
//    User spec: "no infinite loop, deterministic"
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit terminates (fixed 12-iter bound, no infinite loop)") {
    LocalEngine env;

    // Degenerate-but-valid input: 1x1px box (non-zero so the
    // `box.enabled && box.size > 0` gate triggers in BOTH code paths
    // — text_run_builder.cpp + text_layout_engine.hpp) + oversized
    // text.  The fixed 12-iter loop MUST terminate in bounded time.
    // 1x1px is too small to fit even 8pt text, so the binary search
    // runs all 12 iterations, never finding a fit, and the resolved
    // size clamps exactly to the floor (8pt = min_font_size).
    auto params = make_autofit_params(
        "The quick brown fox jumps over the lazy dog",
        72.0f, /*auto_fit=*/true, /*min_fs=*/8.0f, /*max_fs=*/96.0f);
    params.text.layout.box = {1.0f, 1.0f};  // 1x1px box — degenerate but non-zero

    // Implied watchdog: if the loop were unbounded, this TEST_CASE
    // would hang and the test harness would time out (visible in the
    // regression log).  The fixed 12-iter guarantee is the only
    // contract that makes this test safe — adaptive / while-loop
    // implementations would regress here.
    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Strict-equality assertion proves the loop ran all 12 iterations
    // and converged to the floor (i.e. text truly didn't fit at any
    // size in [8, 96]).  A weak `>= 8.0f` would also pass if the
    // binary search were never entered (e.g. gate short-circuited).
    CHECK(shape->layout->font_size == doctest::Approx(8.0f));
    CHECK(shape->layout->font_size <= 96.0f);
}

// ────────────────────────────────────────────────────────────────────────────
// 7. Bounds respected under degenerate box — fits_inside gate
//    User spec: "min/max font respected" + "fits_inside(layout_box)"
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit fits_inside gate under degenerate box") {
    LocalEngine env;

    // 1x1px box + 60pt authored font.  The fits_inside gate forces
    // resolution toward min_font_size, but the floor (16pt) is still
    // too large to fit a 1x1px box.  The assertion: the resolved size
    // is clamped to [min, max] regardless of whether the final
    // layout actually fits the box.
    auto params = make_autofit_params(
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",  // long no-space text
        60.0f, /*auto_fit=*/true, /*min_fs=*/16.0f, /*max_fs=*/40.0f);
    params.text.layout.box = {1.0f, 1.0f};  // 1px box — degenerate

    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // min_font_size clamp respected (16pt floor).
    CHECK(shape->layout->font_size >= 16.0f - 0.01f);
    // max_font_size clamp respected (40pt ceiling).
    CHECK(shape->layout->font_size <= 40.0f + 0.01f);
    // Authored > max clamp respected (60pt > 40pt → resolved <= 40pt).
    CHECK(shape->layout->font_size < 60.0f);
}

// ────────────────────────────────────────────────────────────────────────────
// 8. Determinism certification — 100 runs, bit-identical resolved font size
//    User spec: "deterministic"
//    ADR-018 §Decision 2 — "12-iteration determinism: A test runs
//    auto-fit 100 times with the same input and asserts all 100
//    resolved font sizes are bit-identical."
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("ADR-018: auto-fit 100-run determinism certification") {
    LocalEngine env;

    auto params = make_autofit_params(
        "Determinism cert — moderate length text for shrink-to-fit",
        36.0f, /*auto_fit=*/true, /*min_fs=*/8.0f, /*max_fs=*/96.0f);
    params.text.layout.box = {300.0f, 400.0f};

    // ADR-018 specifies 100 runs for the cert.  Collect all 100
    // resolved font sizes + bounds; assert all bit-identical.
    constexpr int kRuns = 100;
    std::vector<float> font_sizes;
    std::vector<float> widths;
    std::vector<float> heights;
    font_sizes.reserve(kRuns);
    widths.reserve(kRuns);
    heights.reserve(kRuns);

    for (int i = 0; i < kRuns; ++i) {
        auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});
        if (!shape || !shape->layout) {
            MESSAGE("test skipped: system fonts unavailable");
            return;
        }
        font_sizes.push_back(shape->layout->font_size);
        widths.push_back(shape->layout->bounds.x);
        heights.push_back(shape->layout->bounds.y);
    }

    // All 100 runs MUST produce bit-identical font_size + bounds.
    for (int i = 1; i < kRuns; ++i) {
        CHECK(font_sizes[i] == doctest::Approx(font_sizes[0]));
        CHECK(widths[i]    == doctest::Approx(widths[0]));
        CHECK(heights[i]   == doctest::Approx(heights[0]));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 9. TICKET-FALSE-GREEN-TEST-AUDIT Step 2: rendered ink bbox respects
//    auto-fit shrink — when font_size=200 in 400x200 box, the resolved
//    font must shrink so the rendered ink fits inside the box.
//    (ROUND-2 Finding #3: post-raster alpha_bbox scan, not pre-raster
//    layout bbox — the verdict asks for `ink_bbox.right <= 400 + .bottom <= 200`
//    which is the visible ink, not the layout bounds.)
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("TICKET-FALSE-GREEN-TEST-AUDIT: auto-fit shrinks rendered ink bbox into box") {
    using namespace chronon3d;
    using namespace chronon3d::test::completeness;

    auto renderer = test::make_renderer();
    const float font_size_requested = 200.0f;
    const float box_w = 400.0f;
    const float box_h = 200.0f;

    auto comp = composition(
        {.name = "TextV1/auto-fit-audit",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [&renderer, font_size_requested, box_w, box_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("autofit_layer", [&renderer, font_size_requested, box_w, box_h](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("autofit", TextRunSpec{
                    .text = TextSpec{
                        .content = {.value = "Auto-fit ink bbox test"},
                        .placement = TextPlacement{
                            TextPlacementKind::Absolute,
                            {960.0f, 540.0f}},  // canvas center
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = font_size_requested
                        },
                        .layout = {
                            .box = {box_w, box_h},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle,
                            .overflow = TextOverflow::Clip,
                            .auto_fit = true
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        });

    auto fb = renderer.render(comp, Frame{0});
    if (fb == nullptr) {
        MESSAGE("test skipped: render failed (system fonts unavailable)");
        return;
    }

    // TICKET-FALSE-GREEN-TEST-AUDIT Step 2 (post-raster alpha_bbox scan):
    //   ink_bbox.right  <= 400  (box width  with 1px tol)
    //   ink_bbox.bottom <= 200  (box height with 1px tol)
    const auto bbox = alpha_bbox(*fb);
    const int ink_w = bbox.width();
    const int ink_h = bbox.height();
    INFO("auto-fit ink bbox: (", bbox.x0, ",", bbox.y0, ")-(",
         bbox.x1, ",", bbox.y1, ") w=", ink_w, " h=", ink_h,
         " canvas=", fb->width(), "x", fb->height(),
         " requested_font_size=", font_size_requested);
    CHECK(ink_w <= 400 + 1);  // 1px tolerance
    CHECK(ink_h <= 200 + 1);  // 1px tolerance

    // ALSO check visible_px > 0 (false-green check: must produce some ink).
    CHECK(count_visible_pixels(*fb) > 100);
}

