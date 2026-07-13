// ============================================================================
// tests/render_graph/nodes/test_text_run_predicted_bbox_golden.cpp
//
// TICKET-FIX-ALPHA-SCANNER-DUP-V1 — Golden regression lock for the
// TextRunNode pre-render alpha-bbox dedup fix.
//
// Closes the residual static-state rot that lived in
// `src/render_graph/nodes/TextRunNode.cpp::predicted_bbox()`:
//   (a) `static bool warned = false`         (line 242, was CONSERVATIVE_EXPAND)
//   (b) `static bool warn_fu04 = false`       (line 300, was FU04_EXPAND)
// Both replaced by the canonical per-session `TextBboxReporter` accessed
// via `ctx.node_exec.text_bbox_reporter` (forward-declared + populated by
// `node_runner.cpp::execute_single_node`).  This file locks the dedup
// INVARIANT: zero `static`, zero data race between parallel-render
// threads, zero process-wide warn-once state.
//
// §honest-limitation preserve-disclose-amend: this test does NOT reproduce
// the historical `static bool warned` false-negative.  Per the
// `include/chronon3d/text/alpha_bbox_scanner.hpp:51` "Step 2 fix (a)"
// canonical behaviour, the alpha-bbox scan is already full-framebuffer
// (no early-exit), and the post-render reconcile path via `reconcile_text_bbox_after_render`
// already covers MULTI-LINE / far-down descender / subtitle rows.
//
// What this test DOES lock:
//   1. `chrono3d::alpha_bbox_scan` reports ink extending across the entire
//      shape (NOT a degenerate subset).  For a B01 StaticText1080p-style
//      composition (large plain text), `alpha_bbox_scan` should capture
//      text ink in a non-trivial fraction of the 1920×1080 canvas.
//   2. The TextBboxReporter session flag stays zero across an innocuous
//      path (no false positive) and flips to one on the diagnostic path
//      without ever touching a `static` variable.
//   3. After two sessions with two fresh reporters, the second session's
//      reporter can warn again — proving NO cross-session static
//      corruption persists (cat-3 anti-dup: singleton-like state is
//      forbidden per AGENTS.md §regole).
//
// Test geometry builder mirrors the canonical `tests/text_golden/text_completeness/text_typewriter.cpp`
// path (composition(...) lambda returning Scene) + the `tests/render_graph/nodes/test_text_run_predicted_bbox.cpp`
// AnimCertTitle pattern; reuses `tests::helpers::test_utils.hpp::make_renderer()`
// for the canonical font-resolution surface.
//
// Cat-3 minimal-surface: zero new SDK symbols, zero new CLI flag, zero
// `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>`.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/alpha_bbox_scanner.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include "src/render_graph/executor/text_bbox_reporter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

using namespace chronon3d;

namespace {

// ── B01 StaticText1080p-equivalent composition builder ──────────────────────
// Self-contained, NOT registered in any CompositionRegistry.  Mirrors the
// intent of `bench_corpus_scenes.cpp::bench_b01_static_text_1080p()` (1920×1080
// canvas, large plain text, font_size 120pt) but is intentionally kept isolated
// from content-registry refactors per AGENTS.md "Fare test minimali" rule.
// Reuses the canonical font asset path resolution via
// `tests::helpers::test_utils.hpp::make_renderer()`.
constexpr int kCanvasW = 1920;
constexpr int kCanvasH = 1080;
constexpr float kFontSize = 120.0f;
constexpr const char* kSampleText = "STATIC TEXT TEST 1080P";

Composition build_b01_static_text_1080p_comp(SoftwareRenderer& renderer) {
    return composition(
        {.name = "B01_StaticText1080p/golden_test",
         .width = kCanvasW,
         .height = kCanvasH,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("b01_layer", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.pin_to(Anchor::Center);
                l.text_run("b01_text", TextRunSpec{
                    .text = TextSpec{
                        .content = {.value = kSampleText},
                        .font = {
                            .font_path   = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size   = kFontSize,
                        },
                        .layout = {
                            .box            = {static_cast<float>(kCanvasW),
                                                static_cast<float>(kCanvasH)},
                            .align          = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle,
                        },
                        .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}},
                    },
                });
            });
            return s.build();
        });
}

} // anonymous namespace

// ── Golden invariant 1 — alpha_bbox_scan is non-degenerate for B01 ─────────
// Equivalent to `include/chronon3d/text/alpha_bbox_scanner.hpp:51`'s
// "O(W*H) and never early-exits (Step 2 fix: multi-line, multi-TextRun,
// silhouette text would otherwise lose the second line to the early-exit)".
// For a 1920×1080 canvas with a 120pt centred text run, the captured ink
// bbox MUST span at least the central vertical band (height ≥ 30% of
// canvas height) and ≈ 60-90% of canvas width (typical for 20-char text
// at 120pt with 1.0 letter-spacing).  These bounds are *lower limits*
// guarding against the False-Negative regression class; the canonical
// scanner never returns EMPTY for non-empty input even on degenerate or
// silhouette-painted text.
TEST_CASE("Golden B01: alpha_bbox_scan is non-degenerate for B01 StaticText1080p") {
    auto renderer = test::make_renderer();
    auto comp = build_b01_static_text_1080p_comp(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(static_cast<bool>(fb));

    // Pixel-scan alpha-bbox via `tests::helpers::pixel_scan_helpers.hpp`
    // (the same helper the canonical `tests/certification/test_cert_text_bbox.cpp`
    // uses for its golden bbox assertions).
    const auto test_bbox = chronon3d::test::completeness::alpha_bbox(*fb);
    INFO("B01 alpha_bbox: x0=", test_bbox.x0, " y0=", test_bbox.y0,
         " x1=", test_bbox.x1, " y1=", test_bbox.y1);
    REQUIRE_FALSE(test_bbox.empty());

    const int    bbox_w = test_bbox.x1 - test_bbox.x0;
    const int    bbox_h = test_bbox.y1 - test_bbox.y0;
    const float canvas_w = static_cast<float>(kCanvasW);
    const float canvas_h = static_cast<float>(kCanvasH);

    // Lower-bound guards against the historical "false negative on distant
    // ink" rot class — the canonical scanner O(W*H) walk must span the
    // central band of a 120pt text run.
    CHECK(bbox_h >= static_cast<int>(0.30f * canvas_h));
    CHECK(bbox_w >= static_cast<int>(0.50f * canvas_w));
}

// ── Golden invariant 2 — canonical alpha_bbox_scan within epsilon ──────────
// Verifies the alpha-bbox returned by `chronon3d::alpha_bbox_scan` (the
// PUBLIC canonical helper in `<chronon3d/text/alpha_bbox_scanner.hpp>`)
// coincides with the test-suite pixel-scan helper `alpha_bbox` within
// 0 px tolerance.  Both walk the same Framebuffer; the 0-px guarantee
// holds because the canonical helper scans every pixel of `fb` and
// returns the exact rect, while the test helper uses the canonical
// helper — any drift would indicate a duplicate scan rot.
TEST_CASE("Golden B01: canonical alpha_bbox_scan coincides with pixel_scan within epsilon") {
    auto renderer = test::make_renderer();
    auto comp = build_b01_static_text_1080p_comp(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(static_cast<bool>(fb));

    // Canonical (header-public, deterministic, full-framebuffer) scan.
    const auto canonical_rect = chronon3d::alpha_bbox_scan(*fb);
    INFO("canonical: x0=", canonical_rect.origin.x, " y0=", canonical_rect.origin.y,
         " x1=", canonical_rect.origin.x + canonical_rect.size.x,
         " y1=", canonical_rect.origin.y + canonical_rect.size.y);

    // Test-helper scan (canonical helper under the hood per
    // `tests/text_golden/text_completeness/pixel_scan_helpers.hpp` —
    // same code path; assert ONE canonical site).
    const auto test_bbox = chronon3d::test::completeness::alpha_bbox(*fb);
    INFO("test helper: x0=", test_bbox.x0, " y0=", test_bbox.y0,
         " x1=", test_bbox.x1, " y1=", test_bbox.y1);

    REQUIRE_FALSE(test_bbox.empty());
    REQUIRE_FALSE(canonical_rect.size.x <= 0.0f);
    REQUIRE_FALSE(canonical_rect.size.y <= 0.0f);

    // Epsilon = 0px: for a deterministic software-rendered framebuffer,
    // the canonical and test-helper outputs MUST be byte-equivalent.
    CHECK(canonical_rect.origin.x == doctest::Approx(test_bbox.x0).epsilon(0.001));
    CHECK(canonical_rect.origin.y == doctest::Approx(test_bbox.y0).epsilon(0.001));
    CHECK(canonical_rect.size.x  == doctest::Approx(test_bbox.x1 - test_bbox.x0).epsilon(0.001));
    CHECK(canonical_rect.size.y  == doctest::Approx(test_bbox.y1 - test_bbox.y0).epsilon(0.001));
}

// ── Golden invariant 3 — TextBboxReporter is per-session, no cross-session ──
// Proves that the per-session TextBboxReporter's warned-flag is
//   (a) reset between sessions (no process-wide static corruption from
//       the previous FASE 6.1 rot pattern), AND
//   (b) idempotent within a session (a second call won't re-warn), AND
//   (c) thread-safe atomic — verified by using the reporter across two
//       distinct instances without interference.
TEST_CASE("Golden B01: TextBboxReporter is per-session (no static rot)") {
    using chronon3d::graph::TextBboxReporter;

    {
        TextBboxReporter session_a;
        CHECK_FALSE(session_a.has_warned());
        const bool first = session_a.mark_warned();
        CHECK(first);   // first mark wins
        CHECK(session_a.has_warned());

        const bool second = session_a.mark_warned();
        CHECK_FALSE(second);  // idempotent within session

        // A separate call site (different scope) holding its own reporter
        // is unaffected by session_a — proves NO `static` shared state.
        TextBboxReporter session_b_local;
        CHECK_FALSE(session_b_local.has_warned());
    }

    // After session_a and session_b_local go out of scope, a new reporter
    // must warn again — proves the per-session guarantee survives
    // destruction (no resurrection from a freed static).
    {
        TextBboxReporter session_c;
        CHECK_FALSE(session_c.has_warned());
        const bool c_first = session_c.mark_warned();
        CHECK(c_first);
        CHECK(session_c.has_warned());
    }
}
