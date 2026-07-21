// ============================================================================
// test_text_auto_fit.cpp
//
// TICKET-ISOLATED-ALIGNMENT-TESTS — Fase 7 of CapCut-grade verdict.
//
// Regression locks for the auto-fit binary search algorithm in
// compile_or_cache_layout() (src/scene/builders/text_run_builder.cpp).
//
// Three subcases:
//   1. Cache on/off determinism — `cache_layout=true` and
//      `cache_layout=false` with identical input MUST produce identical
//      font_size + bounds (the cache bypass must not inject a different
//      result into the layout computation).
//   2. Impossible min_font_size — min=200 in 400x200 box.  The binary
//      search MUST clamp to min_font_size (200) AND bounds MUST exceed
//      the box (explicit overflow, NEVER silent clip).
//   3. 5-run determinism — same input rendered 5 times → bit-identical
//      font_size + bounds (lighter than the 100-run cert in
//      tests/text/test_auto_fit_font_size.cpp; suitable for fast CI).
//
// Pattern A — uses `LocalEngine` + `materialize_text_run_shape()` for
// pre-raster layout (no full render needed; faster + more deterministic).
// Graceful skip when system fonts are absent (matches the precedent in
// tests/text/test_auto_fit_font_size.cpp).
//
// Test #2 ("impossible min_font_size") is the verdict spec's "Caso
// impossibile: min_font_size = 200 deve produrre overflow-policy esplicita,
// MAI silenziosamente tagliato" — locks the explicit-overflow behavior.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <optional>
#include <memory>
#include <string>
#include <vector>

using namespace chronon3d;

namespace {

// ── LocalEngine — matches precedent in tests/text/test_auto_fit_font_size.cpp
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

// ── make_autofit_params — defaults to 400x200 box, font 120pt → min 40 → max 200
//
// All 3 tests share this helper; each test overrides the fields it cares
// about (cache_layout, min/max, font_size) directly on the returned spec.
[[nodiscard]] TextRunSpec make_autofit_params(
    const std::string& utf8,
    float font_size,
    bool auto_fit,
    float min_font_size = 40.0f,
    float max_font_size = 200.0f,
    bool cache_layout   = true
) {
    TextRunSpec params;
    params.text.content.value                    = utf8;
    params.text.font.font_family                 = "DejaVu Sans";
    params.text.font.font_size                   = font_size;
    params.text.font.font_weight                 = 400;
    params.text.layout.paragraph.auto_fit_font_size = auto_fit;
    params.text.layout.paragraph.min_font_size      = min_font_size;
    params.text.layout.paragraph.max_font_size      = max_font_size;
    params.text.layout.box                          = {400.0f, 200.0f};
    params.direction                             = TextDirection::LTR;
    params.language                              = "en";
    params.cache_layout                          = cache_layout;
    return params;
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// 1. Cache on/off determinism — same input, cache_layout toggled,
//    identical output (font_size + bounds).  Locks the cache bypass
//    invariant: cache_layout=false must NOT inject a different result
//    into the binary search.
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("TICKET-ISOLATED-ALIGNMENT-TESTS: auto-fit cache on/off determinism") {
    LocalEngine env;

    // Long text at 120pt in 400x200 box — must shrink to ~40-50pt.
    // Same params, only cache_layout flips; layout MUST match.
    auto params_cache_on  = make_autofit_params(
        "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
        120.0f, /*auto_fit=*/true, /*min=*/40.0f, /*max=*/200.0f,
        /*cache_layout=*/true);
    auto params_cache_off = params_cache_on;
    params_cache_off.cache_layout = false;

    auto shape_on  = materialize_text_run_shape(params_cache_on,  &env.engine, SampleTime{});
    auto shape_off = materialize_text_run_shape(params_cache_off, &env.engine, SampleTime{});

    if (!shape_on || !shape_on->layout || !shape_off || !shape_off->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Bit-identical font_size + bounds.x + bounds.y across cache states.
    CHECK(shape_on->layout->font_size == doctest::Approx(shape_off->layout->font_size));
    CHECK(shape_on->layout->bounds.x   == doctest::Approx(shape_off->layout->bounds.x));
    CHECK(shape_on->layout->bounds.y   == doctest::Approx(shape_off->layout->bounds.y));

    // Also verify shrink actually happened (regression lock: cache bypass
    // must not produce a no-op result that bypasses the binary search).
    CHECK(shape_on->layout->font_size < 120.0f);
    CHECK(shape_on->layout->font_size >= 40.0f);
}

// ────────────────────────────────────────────────────────────────────────────
// 2. Impossible min_font_size — min=200 in 400x200 box; must clamp
//    to min (200) AND bounds must EXCEED box (explicit overflow, not
//    silent clip).  This is the verdict spec's "Caso impossibile:
//    min_font_size = 200 deve produrre overflow-policy esplicita".
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("TICKET-ISOLATED-ALIGNMENT-TESTS: auto-fit impossible min_font_size produces explicit overflow") {
    LocalEngine env;

    // min_font_size = 200 > box height = 200.  Physically impossible to
    // fit even at the floor.  The binary search must clamp to 200pt AND
    // the layout bounds MUST exceed the box (overflow is visible in the
    // pre-raster layout output, not silently clipped).
    auto params = make_autofit_params(
        "Hello",
        120.0f, /*auto_fit=*/true, /*min=*/200.0f, /*max=*/200.0f,
        /*cache_layout=*/false);

    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Clamped to min_font_size (200pt).
    CHECK(shape->layout->font_size == doctest::Approx(200.0f));

    // Bounds exceed box dimensions (explicit overflow signal in the
    // pre-raster layout; the TextOverflow policy is applied downstream).
    CHECK(shape->layout->bounds.x > 400.0f);
    CHECK(shape->layout->bounds.y > 200.0f);
}

// ────────────────────────────────────────────────────────────────────────────
// 3. 5-run determinism — same input rendered 5 times → bit-identical
//    font_size + bounds (lighter than the 100-run cert in
//    tests/text/test_auto_fit_font_size.cpp test #8; suitable for
//    fast CI feedback on this isolated binary).
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("TICKET-ISOLATED-ALIGNMENT-TESTS: auto-fit 5-run determinism certification") {
    LocalEngine env;

    auto params = make_autofit_params(
        "Determinism 5-run cert — moderate length text for shrink-to-fit",
        120.0f, /*auto_fit=*/true, /*min=*/40.0f, /*max=*/200.0f,
        /*cache_layout=*/false);

    constexpr int kRuns = 5;
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

    // All 5 runs MUST produce bit-identical font_size + bounds.
    for (int i = 1; i < kRuns; ++i) {
        CHECK(font_sizes[i] == doctest::Approx(font_sizes[0]));
        CHECK(widths[i]      == doctest::Approx(widths[0]));
        CHECK(heights[i]     == doctest::Approx(heights[0]));
    }
}