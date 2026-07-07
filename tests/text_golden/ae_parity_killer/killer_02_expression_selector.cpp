// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity_killer/killer_02_expression_selector.cpp
//
// TICKET-AE-PARITY-KILLER-EXPRESSION-SELECTOR — Phase 2 Killer 2.
//
// LOCK WHAT IS LOCKABLE TODAY, MARK WHAT IS FORWARD-ONLY.
// ───────────────────────────────────────────────────────────────────────────
// ExpressionSelector (per-character `amount = selectorValue * textIndex /
// textTotal`) is PLANNED, NOT YET IMPL.  Verified via find-grep: zero
// `expression_evaluator.cpp` files in src/text/.  Source anchor per
// docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md Decision 2 Killer 2:
// `src/text/selector/expression_evaluator.cpp` (NEW; resolver from
// `AnimatorResolver` per Blocco 6 of
// docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md).
//
// What IS IMPL (and what this killer test locks):
//   1. The per-character formula contract (test-internal pure function
//      `amount = selectorValue * textIndex / textTotal`):
//        - byte-identical output for the same input (cross-call)
//        - zero global mutable state (pure function)
//        - reentrant across serial/parallel (4 threads × 16 iterations)
//   2. The end-to-end pipeline substrate (evaluate_animator with
//      GlyphSelectorSpec{unit=Character, shape=RampUp, start=0, end=100,
//      amount=AnimatedValue{selectorValue*100}} + PositionProperty):
//        - random-access frame 30 == sequential up to frame 30
//        - zero global mutable state in evaluator
//        - reentrant across serial/parallel
//
// 2 TEST_CASEs, each with 3-SUBCASE determinism lock pattern:
//   CASE 1: per-character formula substrate (3 SUBCASEs: byte-identity +
//           no-mutable-state + reentrant)
//   CASE 2: end-to-end pipeline (3 SUBCASEs: random-access==sequential +
//           no-mutable-state + reentrant)
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public symbols
// in include/chronon3d/, zero disallowed #include, zero new
// singleton/registry/resolver/cache. Test-only surface.
//
// Forward-only contract:
//   When TICKET-EXPRESSION-IMPL lands, this file's TEST_CASE 2 can be
//   extended to use the production ExpressionSelector surface directly
//   (instead of the GlyphSelectorSpec emulating the per-character
//   linear ramp).  The cell-level substrate (CASE 1) is the contract
//   that ExpressionSelector must implement.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/animation/glyph_instance_state.hpp>
#include <chronon3d/text/animation/text_animator_evaluator.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>
#include <chronon3d/core/types/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using chronon3d::f32;

// Test constants.
constexpr std::uint32_t kTestUnits = 100;
constexpr f32 kTestSelectorValue = 0.5f;

// The contract formula: amount = selectorValue * textIndex / textTotal.
// Pure function, no side effects, no global state. The future production
// ExpressionSelector (src/text/selector/expression_evaluator.cpp) will
// implement this same formula; the test-internal helper locks the
// contract so any future implementation must match.
[[nodiscard]] constexpr f32 expression_ramp(
    f32 selector_value,
    std::uint32_t text_index,
    std::uint32_t text_total
) noexcept {
    if (text_total == 0) return 0.0f;
    return (selector_value * static_cast<f32>(text_index))
         / static_cast<f32>(text_total);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST_CASE 1 — Per-character formula substrate
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("KILLER 02 expression_selector — per-character formula substrate") {
    SUBCASE("formula byte-identity (cross-call predictability)") {
        // Lock contract: same (selectorValue, textIndex, textTotal) → byte-
        // identical output across any invocation order, thread, or run.
        // The future production ExpressionSelector implementation must
        // produce byte-identical results for the same input.
        for (std::uint32_t i = 0; i < kTestUnits; ++i) {
            const f32 a = expression_ramp(kTestSelectorValue, i, kTestUnits);
            const f32 b = expression_ramp(kTestSelectorValue, i, kTestUnits);
            const f32 c = expression_ramp(kTestSelectorValue, i, kTestUnits);
            CHECK(a == b);
            CHECK(b == c);
        }
        // Boundary contract: textIndex=0 → amount=0 (no shift at start).
        CHECK(expression_ramp(0.75f, 0, kTestUnits) == doctest::Approx(0.0f));
        // Boundary contract: textIndex=textTotal-1 → amount=selectorValue*(textTotal-1)/textTotal.
        const f32 expected_max = 0.75f * static_cast<f32>(kTestUnits - 1)
                               / static_cast<f32>(kTestUnits);
        CHECK(expression_ramp(0.75f, kTestUnits - 1, kTestUnits)
              == doctest::Approx(expected_max));
        // Defensive contract: textTotal=0 → amount=0 (no division by zero).
        // The future production ExpressionSelector must also handle this
        // empty-input case gracefully (degenerate text, 0-length source).
        CHECK(expression_ramp(0.5f, 0, 0) == doctest::Approx(0.0f));
        CHECK(expression_ramp(0.5f, 5, 0) == doctest::Approx(0.0f));
    }

    SUBCASE("formula has zero global mutable state (pure function)") {
        // Lock contract: the formula reads NO global mutable state.
        // Call the formula in 2 passes; if the formula had global
        // mutable state, the second pass's output would differ from
        // the first.  This is a behavioural check (the function is
        // declared `constexpr` so the compiler verifies purity at
        // compile time, but the runtime check guards against any
        // future refactor that adds hidden state).
        std::array<f32, kTestUnits> pass_a{};
        std::array<f32, kTestUnits> pass_b{};
        for (std::uint32_t i = 0; i < kTestUnits; ++i) {
            pass_a[i] = expression_ramp(kTestSelectorValue, i, kTestUnits);
        }
        for (std::uint32_t i = 0; i < kTestUnits; ++i) {
            pass_b[i] = expression_ramp(kTestSelectorValue, i, kTestUnits);
        }
        for (std::uint32_t i = 0; i < kTestUnits; ++i) {
            CHECK(pass_a[i] == pass_b[i]);
        }
    }

    SUBCASE("formula reentrant across serial/parallel (4 threads × 16 iterations)") {
        constexpr int kParallelThreads = 4;
        constexpr int kIterationsPerThread = 16;

        // Reference: single-threaded sequential run.
        std::array<f32, kTestUnits> reference{};
        for (std::uint32_t i = 0; i < kTestUnits; ++i) {
            reference[i] = expression_ramp(kTestSelectorValue, i, kTestUnits);
        }

        // Parallel: 4 threads × 16 iterations each, writing into a
        // per-thread slot.  If the formula is reentrant (no shared
        // mutable state), all per-thread results match the reference.
        std::array<std::array<f32, kTestUnits>, kParallelThreads> results{};
        std::vector<std::thread> threads;
        threads.reserve(kParallelThreads);
        for (int t = 0; t < kParallelThreads; ++t) {
            threads.emplace_back([&, t]() {
                for (int it = 0; it < kIterationsPerThread; ++it) {
                    for (std::uint32_t i = 0; i < kTestUnits; ++i) {
                        results[t][i] = expression_ramp(
                            kTestSelectorValue, i, kTestUnits);
                    }
                }
            });
        }
        for (auto& th : threads) th.join();

        for (int t = 0; t < kParallelThreads; ++t) {
            for (std::uint32_t i = 0; i < kTestUnits; ++i) {
                CHECK(results[t][i] == reference[i]);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST_CASE 2 — End-to-end pipeline (evaluate_animator)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Build a TextUnitMap with kTestUnits glyphs (1:1 grapheme mapping).
[[nodiscard]] chronon3d::TextUnitMap make_unit_map(
    std::uint32_t total_units = kTestUnits
) {
    chronon3d::TextUnitMap unit_map;
    unit_map.glyph_to_grapheme.resize(total_units);
    unit_map.grapheme_count = total_units;
    for (std::uint32_t i = 0; i < total_units; ++i) {
        unit_map.glyph_to_grapheme[i] = i;
    }
    return unit_map;
}

// Build a vector of GlyphInstanceState with default values.
[[nodiscard]] std::vector<chronon3d::GlyphInstanceState> make_glyph_states(
    std::uint32_t total_units = kTestUnits
) {
    std::vector<chronon3d::GlyphInstanceState> gs(total_units);
    for (std::uint32_t i = 0; i < total_units; ++i) {
        gs[i].glyph_id = i;
        gs[i].layout_position = {static_cast<f32>(i), 0.0f};
        gs[i].opacity = 1.0f;
    }
    return gs;
}

// Build a TextAnimatorSpec that emulates the per-character linear ramp
// using the production GlyphSelectorSpec surface (Character unit +
// RampUp shape + start=0 + end=100 + amount=AnimatedValue{selectorValue*100}).
// The PositionProperty's X-axis is the carrier of the per-glyph shift:
//   position.x = 1.0 * weight * selectorValue
//              = selectorValue * (textIndex + 0.5) / textTotal   (RampUp)
//
// When TICKET-EXPRESSION-IMPL lands (production ExpressionSelector per
// Blocco 6 of TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md), this helper is
// replaced with a direct ExpressionSelector construction; the per-glyph
// contract is unchanged.
[[nodiscard]] chronon3d::TextAnimatorSpec make_animator_spec(
    f32 selector_value
) {
    chronon3d::TextAnimatorSpec spec;
    spec.id = "killer_02_test";
    spec.enabled = true;
    // Replace blend mode: each call to evaluate_animator sets the per-
    // glyph position to the current frame's weighted value (NOT cumulative
    // across frames). The default Add blend mode for transforms would
    // accumulate position across sequential frames, breaking the
    // random-access == sequential contract (Add is the AE convention for
    // per-frame animation accumulation; the per-character ramp contract
    // is independent of the temporal accumulation contract, so we isolate
    // them by using Replace here).
    spec.transform_mode = chronon3d::TextPropertyBlendMode::Replace;
    spec.color_mode    = chronon3d::TextPropertyBlendMode::Replace;

    chronon3d::GlyphSelectorSpec sel;
    sel.unit   = chronon3d::TextSelectorUnit::Character;
    sel.shape  = chronon3d::TextSelectorShape::RampUp;
    sel.order  = chronon3d::TextSelectorOrder::Forward;
    sel.start  = chronon3d::f32{0.0f};
    sel.end    = chronon3d::f32{100.0f};
    sel.amount = chronon3d::f32{selector_value * 100.0f};
    sel.exclude_spaces = false;
    sel.randomize_order = false;

    spec.selectors.push_back(sel);
    spec.properties.emplace_back(
        chronon3d::PositionProperty{chronon3d::Vec3{1.0f, 0.0f, 0.0f}}
    );
    return spec;
}

// Placeholder source: the content is irrelevant because
// `exclude_spaces = false` bypasses the cluster-bytespace lookup and the
// Character selector does not read the source bytes.  The length (14 chars)
// is also irrelevant — `unit_map.grapheme_count` is the source of truth
// for the per-glyph weight, NOT the source text length.
constexpr std::string_view kSource = "0123456789...";

}  // namespace

TEST_CASE("KILLER 02 expression_selector — end-to-end pipeline (evaluate_animator)") {
    SUBCASE("random-access frame 30 == sequential up to frame 30 (byte-identical)") {
        // Lock contract: evaluating the spec at frame 30 directly
        // (random-access) MUST produce a per-glyph state vector that is
        // byte-identical to evaluating frames 0..30 in sequence
        // (sequential).  This is the determinism contract for the
        // production evaluate_animator pipeline.
        const auto spec = make_animator_spec(kTestSelectorValue);
        const chronon3d::SampleTime t30 = chronon3d::SampleTime::from_frame_int(
            30, chronon3d::FrameRate{30, 1});

        // Sequential: evaluate frames 0..30 in order.
        std::vector<chronon3d::GlyphInstanceState> seq_gs = make_glyph_states();
        chronon3d::TextUnitMap seq_map = make_unit_map();
        for (std::uint32_t f = 0; f <= 30; ++f) {
            const chronon3d::SampleTime t = chronon3d::SampleTime::from_frame_int(
                static_cast<std::int32_t>(f), chronon3d::FrameRate{30, 1});
            chronon3d::evaluate_animator(spec, seq_map, seq_gs, kSource, t);
        }

        // Random-access: evaluate frame 30 directly.
        std::vector<chronon3d::GlyphInstanceState> ra_gs = make_glyph_states();
        chronon3d::TextUnitMap ra_map = make_unit_map();
        chronon3d::evaluate_animator(spec, ra_map, ra_gs, kSource, t30);

        // Both should produce byte-identical per-glyph state vectors.
        REQUIRE(seq_gs.size() == ra_gs.size());
        for (std::uint32_t i = 0; i < seq_gs.size(); ++i) {
            CHECK(seq_gs[i].position.x == doctest::Approx(ra_gs[i].position.x));
            CHECK(seq_gs[i].position.y == doctest::Approx(ra_gs[i].position.y));
            CHECK(seq_gs[i].position.z == doctest::Approx(ra_gs[i].position.z));
        }

        // Anti-greenwashing ramp-SHAPE lock (code-reviewer round 1):
        // verify the per-character linear ramp is actually applied at the
        // end-to-end level.  Without these CHECKs, a future regression
        // that collapses the per-glyph weight to a constant (e.g.
        // `weight = spec.amount / 100` for every glyph) would still pass
        // the determinism contract above but silently break the
        // per-character linear ramp contract.  With Replace blend mode,
        // the per-glyph position is `selectorValue * (i + 0.5) / total`
        // (the +0.5 offset is the production RampUp shape's unit-center).
        const f32 expected_first = kTestSelectorValue * 0.5f
                                 / static_cast<f32>(kTestUnits);
        const f32 expected_last  = kTestSelectorValue
                                 * (static_cast<f32>(kTestUnits) - 0.5f)
                                 / static_cast<f32>(kTestUnits);
        CHECK(seq_gs[0].position.x == doctest::Approx(expected_first));
        CHECK(seq_gs[kTestUnits - 1].position.x
              == doctest::Approx(expected_last));
        for (std::uint32_t i = 1; i < seq_gs.size(); ++i) {
            // Monotonic non-decreasing ramp (strict increase because the
            // RampUp shape produces strictly increasing weights for
            // strictly increasing glyph indices).
            CHECK(seq_gs[i].position.x > seq_gs[i - 1].position.x);
        }
    }

    SUBCASE("zero global mutable state in evaluator (sequential vs polluted-then-sequential)") {
        // Lock contract: the production evaluate_animator reads NO
        // global mutable state.  Verify by running the spec twice in
        // sequence, with a "pollution" pass with a different
        // selectorValue in between.  If the evaluator had hidden
        // global mutable state, the second run's output would differ
        // from the first.
        const auto spec = make_animator_spec(kTestSelectorValue);
        const chronon3d::SampleTime t30 = chronon3d::SampleTime::from_frame_int(
            30, chronon3d::FrameRate{30, 1});

        // First run.
        std::vector<chronon3d::GlyphInstanceState> gs_a = make_glyph_states();
        chronon3d::TextUnitMap map_a = make_unit_map();
        chronon3d::evaluate_animator(spec, map_a, gs_a, kSource, t30);

        // Pollution pass with a different selectorValue (would mutate
        // any hidden global state if it existed).
        {
            const auto pollution_spec = make_animator_spec(0.99f);
            std::vector<chronon3d::GlyphInstanceState> pollution_gs = make_glyph_states();
            chronon3d::TextUnitMap pollution_map = make_unit_map();
            chronon3d::evaluate_animator(
                pollution_spec, pollution_map, pollution_gs, kSource, t30);
        }

        // Second run (should match the first).
        std::vector<chronon3d::GlyphInstanceState> gs_b = make_glyph_states();
        chronon3d::TextUnitMap map_b = make_unit_map();
        chronon3d::evaluate_animator(spec, map_b, gs_b, kSource, t30);

        REQUIRE(gs_a.size() == gs_b.size());
        for (std::uint32_t i = 0; i < gs_a.size(); ++i) {
            CHECK(gs_a[i].position.x == doctest::Approx(gs_b[i].position.x));
            CHECK(gs_a[i].position.y == doctest::Approx(gs_b[i].position.y));
            CHECK(gs_a[i].position.z == doctest::Approx(gs_b[i].position.z));
        }
    }

    SUBCASE("reentrant across serial/parallel (4 threads × 16 iterations)") {
        constexpr int kParallelThreads = 4;
        constexpr int kIterationsPerThread = 16;

        const auto ref_spec = make_animator_spec(kTestSelectorValue);
        const chronon3d::SampleTime t30 = chronon3d::SampleTime::from_frame_int(
            30, chronon3d::FrameRate{30, 1});

        // Reference: single-threaded sequential run.
        std::vector<chronon3d::GlyphInstanceState> ref_gs = make_glyph_states();
        chronon3d::TextUnitMap ref_map = make_unit_map();
        chronon3d::evaluate_animator(ref_spec, ref_map, ref_gs, kSource, t30);

        // Parallel: 4 threads × 16 iterations each.  Each thread
        // owns its local glyph_states + unit_map; the spec is
        // read-only (const-ref), so the only shared state is the
        // spec's AnimatedValue keyframe tables (which are immutable
        // post-construction).
        std::array<std::vector<chronon3d::GlyphInstanceState>, kParallelThreads> results;
        std::vector<std::thread> threads;
        threads.reserve(kParallelThreads);
        for (int t = 0; t < kParallelThreads; ++t) {
            threads.emplace_back([&, t]() {
                std::vector<chronon3d::GlyphInstanceState> local_gs = make_glyph_states();
                chronon3d::TextUnitMap local_map = make_unit_map();
                for (int it = 0; it < kIterationsPerThread; ++it) {
                    // Reset glyph state and unit_map each iteration to
                    // match the reference (reset to fresh defaults).
                    local_gs = make_glyph_states();
                    local_map = make_unit_map();
                    chronon3d::evaluate_animator(
                        ref_spec, local_map, local_gs, kSource, t30);
                }
                results[t] = std::move(local_gs);
            });
        }
        for (auto& th : threads) th.join();

        for (int t = 0; t < kParallelThreads; ++t) {
            REQUIRE(results[t].size() == ref_gs.size());
            for (std::uint32_t i = 0; i < ref_gs.size(); ++i) {
                CHECK(results[t][i].position.x == doctest::Approx(ref_gs[i].position.x));
            }
        }
    }
}
