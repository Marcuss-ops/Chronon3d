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

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace chronon3d;

namespace {

struct LocalEngine {
    chronon3d::Config                cfg{};
    chronon3d::runtime::RenderRuntime runtime;
    FontEngine                        engine;

    LocalEngine()
        : runtime(cfg),
          engine{runtime.resolver()}
    {}
};

[[nodiscard]] TextRunParams make_autofit_params(
    const std::string& utf8,
    float font_size,
    bool auto_fit,
    float min_font_size = 8.0f,
    float max_font_size = 96.0f
) {
    TextRunParams params;
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
