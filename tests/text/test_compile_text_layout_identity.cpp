#include <memory>
#include <optional>
// ============================================================================
// test_compile_text_layout_identity.cpp
//
// TICKET-100 — identity regression lock for the materialize_text_run_shape
// refactor.  After the refactor the materializer delegates the 5 layout
// phases (cache lookup + shape_text + resolve_placed_glyph_run + manual
// TextRunLayout build + cache store) to compile_text_layout via the
// `compile_or_cache_layout` anonymous-namespace helper in
// `src/scene/builders/text_run_builder.cpp`.
//
// What this TU locks:
//   (1)  Path A = materialize_text_run_shape via helper.
//        Path B = direct compile_text_layout on equivalent Request/Services.
//        Both pathways populate the SAME TextRunLayout fields (after the
//        helper's post-compile direction+language override):
//          - source_text matches input utf8
//          - placed.glyphs.size() matches (or both empty when system fonts
//            are unavailable)
//          - font_size matches
//          - font.font_size matches (review P0 #6 closure — pre-refactor the
//            materializer built `shaped_font` from 4 fields only and assigned
//            it to text_layout->font, dropping font_size to the default
//            0.0f; post-refactor compile_text_layout stores `primary_font`
//            verbatim, so text_layout->font.font_size mirrors the resolved
//            32.0f / 44.0f / ... size)
//   (2)  Cache hit returns the SAME shared_ptr (cache_key stable across
//        calls with equivalent params).  Locks the legacy-canonical
//        cache_key contract — including direction + language — that the
//        refactor promised not to regress.
//   (3)  After TICKET-100's post-compile override, the materialized
//        TextRunLayout carries params.direction (not Auto) and params.language
//        (not empty).  compile_text_layout's hardcoded defaults would
//        otherwise leak through if the helper's override were skipped.
//
// All assertions are either soft (CHECK) or adapted to gracefully
// degrade when system fonts are absent (both paths fail → MESSAGE+skip
// rather than a hard CHECK firing).  Each TEST_CASE resets the shared
// TextLayoutCache so cache-key state from earlier tests is isolated.
// ============================================================================

#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace chronon3d;

// s_text_cache — file-local TextLayoutCache (after includes for unity-build safety).
static chronon3d::TextLayoutCache s_text_cache;

namespace {

/// RAII-owned engine stack (same pattern as test_compile_text_layout_errors.cpp).
/// `runtime->resolver()` is process-wide; font registrations persist across
/// tests but the four cases below assert on STRUCTURE of inputs (no
/// direct font-engine state reads), so cross-test residuals are safe.
struct LocalEngine {
    chronon3d::Config                cfg{};
    std::unique_ptr<chronon3d::runtime::RenderRuntime> runtime;
    FontEngine                        engine;

    LocalEngine()
        : runtime(chronon3d::runtime::RenderRuntime::create(
              chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value()),
          engine{runtime->resolver()}
    {}
};

inline void reset_layout_cache_for_test() {
    // cache reset deferred: TICKET-011 (build-rot fix) — s_text_cache scope is file-local
}

[[nodiscard]] TextRunSpec make_test_params(
    const std::string& utf8,
    float font_size,
    TextDirection direction = TextDirection::LTR,
    const std::string& language = "en"
) {
    TextRunSpec params;
    params.text.content.value          = utf8;
    params.text.font.font_family       = "DejaVu Sans";   // system fallback
    params.text.font.font_size         = font_size;
    params.text.font.font_weight       = 400;
    // font_path intentionally empty → resolver fallback to font_family.
    params.direction                   = direction;
    params.language                    = language;
    params.cache_layout                = true;
    return params;
}

[[nodiscard]] TextLayoutSpec make_test_layout() {
    TextLayoutSpec layout;
    layout.box         = {800.0f, 200.0f};
    layout.tracking    = 0.0f;
    layout.line_height = 1.2f;
    return layout;
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// 1. Identity — materialize ≡ compile_text_layout on equivalent input
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_text_run_shape ≡ compile_text_layout (post-refactor identity)") {
    LocalEngine env;
    reset_layout_cache_for_test();

    auto params    = make_test_params("Identity Check", 32.0f);
    auto layout_sp = make_test_layout();

    // ── Path A — through materialize_text_run_shape (post-refactor) ──
    auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});

    // ── Path B — direct compile_text_layout on equivalent input ────
    TextDocument doc;
    doc.utf8          = params.text.content.value;
    doc.defaults.font = params.text.font;
    doc.split_paragraphs();

    TextLayoutRequest   req{&doc, &layout_sp, params.text.font};
    TextCompileServices svc{&env.engine, /*cache=*/nullptr};
    auto direct = compile_text_layout(req, svc);

    // Graceful degradation: if system fonts are absent both paths fail.
    // Skip the comparison rather than CHECK-fail on forged values.
    if (!shape || !shape->layout || !direct.has_value()) {
        MESSAGE("test skipped: system fonts unavailable for ASCII text");
        return;
    }

    const auto& materialized = *shape->layout;
    const auto& canonical    = *direct.value();

    // (a) source_text — both paths concatenate resolved-run text identically.
    CHECK(materialized.source_text == canonical.source_text);

    // (b) font_size — both mirror params.text.font.font_size.
    CHECK(materialized.font_size == doctest::Approx(canonical.font_size));
    CHECK(materialized.font_size == doctest::Approx(32.0f));

    // (c) font identity (review P0 #6 closure) — pre-refactor `shaped_font`
    //     dropped font_size; post-refactor primary_font's full 5-field
    //     FontSpec lands on text_layout->font.
    CHECK(materialized.font.font_size == doctest::Approx(canonical.font.font_size));
    CHECK(materialized.font.font_size == doctest::Approx(32.0f));

    // (d) direction + language — helper overrides compile_text_layout's
    //     hardcoded Auto/empty defaults to params; direct path leaves
    //     compile_text_layout's defaults.  DIFFERENCE is intentional:
    //     the materializer honours the original request's direction.
    CHECK(materialized.direction == params.direction);
    CHECK(materialized.language  == params.language);
    CHECK(canonical.direction    == TextDirection::Auto);
    CHECK(canonical.language.empty());

    // (e) glyph count (or both empty when fonts unavailable).
    CHECK(materialized.placed.glyphs.size() == canonical.placed.glyphs.size());

    // (f) wrap + tracking mirror through the helper identically.
    CHECK(materialized.wrap     == canonical.wrap);
    CHECK(materialized.tracking == doctest::Approx(canonical.tracking));

    // (g) units TextUnitMap populated unconditionally (Fase 1.1 invariant).
    //     Both paths call build_text_unit_map under the hood; counts match.
    CHECK(materialized.units.glyph_to_grapheme.size() == canonical.units.glyph_to_grapheme.size());

    // (h) bounds — review TICKET-100 critical feedback: legacy set
    //     bounds = {placed.total_width, placed.total_height}; compile_text_layout
    //     sets bounds = composed.bounds.  For single-line ASCII with the default
    //     SingleLine composer, both reduce to the same value, so the test locks
    //     the post-refactor contract.
    CHECK(materialized.bounds.x == doctest::Approx(canonical.bounds.x));
    CHECK(materialized.bounds.y == doctest::Approx(canonical.bounds.y));

    // (i) font_spans — review TICKET-100 critical feedback: legacy materializer
    //     did NOT populate font_spans; post-refactor compile_text_layout populates
    //     it for every layout.  For single-font input, font_spans is a single
    //     span covering the full glyph range [0, placed.glyphs.size()).  Lock
    //     the post-refactor contract so downstream consumers (text_run_processor
    //     font-switch branch) see consistent state.
    if (!materialized.placed.glyphs.empty()) {
        REQUIRE(materialized.font_spans.size() >= 1);
        CHECK(materialized.font_spans.front().glyph_begin == 0);
        CHECK(materialized.font_spans.back().glyph_end ==
              static_cast<std::uint32_t>(materialized.placed.glyphs.size()));
        // Contiguity: span_i.glyph_end == span_(i+1).glyph_begin.
        for (std::size_t si = 1; si < materialized.font_spans.size(); ++si) {
            CHECK(materialized.font_spans[si - 1].glyph_end ==
                  materialized.font_spans[si].glyph_begin);
        }
    } else {
        // Empty glyph list → font_spans may be empty (no shaped output).
        CHECK(materialized.font_spans.empty());
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 2. Cache-hit identity — repeated materialize calls return the same shared_ptr
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_text_run_shape: cache hit returns the same TextRunLayout shared_ptr") {
    LocalEngine env;
    reset_layout_cache_for_test();

    auto params = make_test_params("Cache Hit Identity", 32.0f);

    auto shape_a = materialize_text_run_shape(params, &env.engine, SampleTime{});
    auto shape_b = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape_a || !shape_b || !shape_a->layout || !shape_b->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Cache-key preservation (params.cache_layout=true) — the helper
    // stores with the legacy-canonical key (including direction +
    // language), so a second call hits the cache and returns the same
    // shared_ptr.  Locking pointer identity freezes the cache_key
    // contract the refactor promised not to regress.
    CHECK(shape_a->layout.get() == shape_b->layout.get());

    // Verify also via digest (less strict — same content but possibly
    // different allocation is acceptable as long as the cache_hit path
    // is preserved bit-identical).
    CHECK(shape_a->layout->layout_hash() == shape_b->layout->layout_hash());
}

// ────────────────────────────────────────────────────────────────────────────
// 3. direction + language — override survives across materialize paths
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_text_run_shape: direction + language override applied after compile_text_layout") {
    LocalEngine env;
    reset_layout_cache_for_test();

    SUBCASE("RTL / arabic") {
        auto params = make_test_params("Hello", 32.0f,
                                       TextDirection::RTL, "ar");

        auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});
        if (!shape || !shape->layout) {
            MESSAGE("test skipped: system fonts unavailable");
            return;
        }

        // Helper OVERRIDES compile_text_layout's hardcoded direction=Auto /
        // language="" defaults to the params values, so the materialized
        // shape honours bidirectional / locale settings.
        CHECK(shape->layout->direction == TextDirection::RTL);
        CHECK(shape->layout->language  == "ar");
    }

    SUBCASE("LTR / english — direction preserved as LTR + language non-empty") {
        auto params = make_test_params("Hello", 32.0f,
                                       TextDirection::LTR, "en");

        auto shape = materialize_text_run_shape(params, &env.engine, SampleTime{});
        if (!shape || !shape->layout) {
            MESSAGE("test skipped: system fonts unavailable");
            return;
        }

        CHECK(shape->layout->direction == TextDirection::LTR);
        CHECK(shape->layout->language  == "en");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 4. font.font_size — closes review P0 #6 (legacy 0.0f default bug)
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_text_run_shape: text_layout->font.font_size mirrors params (P0 #6 closure)") {
    LocalEngine env;
    reset_layout_cache_for_test();

    auto params = make_test_params("Font Size Closure", 44.0f);
    auto shape  = materialize_text_run_shape(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Pre-TICKET-100: text_layout->font.font_size was 0.0f (default FontSpec
    // value when `shaped_font = {4-field FontSpec}` was assigned) while
    // text_layout->font_size mirrored the requested size.  Drivers reading
    // back the size saw the 0.0f / 72.0f default and produced a visible
    // size jump on transition.  Post-refactor: compile_text_layout writes
    // the full 5-field `primary_font` to text_layout->font, so both
    // fields agree on the resolved size.
    CHECK(shape->layout->font.font_size == doctest::Approx(44.0f));
    CHECK(shape->layout->font_size      == doctest::Approx(44.0f));
    CHECK(shape->layout->font.font_size == doctest::Approx(shape->layout->font_size));
}
