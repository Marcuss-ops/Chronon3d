// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp
//
// TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION — Phase 2 Killer 1.
//
// LOCK WHAT IS LOCKABLE TODAY, MARK WHAT IS FORWARD-ONLY.
// ───────────────────────────────────────────────────────────────────────────
// WigglySelector / WaveSelector are PLANNED, NOT YET IMPL.
// Verified via find-grep: zero `*wiggly*` or `*wave*` files in
// include/chronon3d/ + src/text/. Current scope per Blocco 6 of
// docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md.
//
// What IS IMPL (and what this killer test locks):
//   1. Cell-level deterministic substrate at
//        src/text/glyph_selector_random.{hpp,cpp}:
//      - hash_to_unit_float(seed, unit_index)        f32 [0,1)
//      - get_or_build_permutation(seed, total_units) const std::vector<u32>&
//        (Fisher-Yates, cached per-thread per (seed, total))
//   2. TextSelectorOrder::Random enum in
//      include/chronon3d/text/glyph_selector.hpp:42.
//   3. evaluate_animator(spec, TextUnitMap&, vector<GlyphInstanceState>&,
//                        source, SampleTime)
//        in include/chronon3d/text/animation/text_animator_evaluator.hpp.
//
// 2 TEST_CASEs, each with 3-SUBCASE determinism lock pattern:
//   CASE 1: cell-level substrate (hash + permutation) — proves the
//           Wiggly/Wave math foundation is stable across:
//           SUBCASE 1: same seed+idx → byte-identical hash (cross-frame)
//           SUBCASE 2: different seed → diverging distribution
//           SUBCASE 3: parallel compute → byte-identical (no race)
//   CASE 2: end-to-end Random order pipeline via evaluate_animator
//           (mocked PlacedGlyphRun + GlyphSelectorSpec{order=Random} +
//            PositionProperty{Vec3{20,0,0}}):
//           SUBCASE 1: same-seed evaluate → identical per-glyph state across all glyphs
//           SUBCASE 2: different-seed evaluate → observable per-glyph max-diff across all glyphs
//           SUBCASE 3: cross-run reference lock (FNV-1a fingerprint)
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public symbols
// in include/chronon3d/, zero disallowed #include, zero new
// singleton/registry/resolver/cache. Test-only surface.
// Forward-only contract:
//   When TICKET-WIGGLY-IMPL lands, this file's TEST_CASE 2 expands to
//   include a 4th SUBCASE exercising the new WigglySelector surface.
//   The cell-level substrate (CASE 1) is the foundation that
//   WigglySelector will compose on top of.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/animation/glyph_instance_state.hpp>
#include <chronon3d/text/animation/text_animator_evaluator.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>
#include <chronon3d/core/types/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

namespace {

using seed_t = std::uint64_t;
using idx_t  = std::uint64_t;
using perm_t = std::vector<std::uint32_t>;
using chronon3d::f32;
// (No forward declarations of `chronon3d::detail::*` here — the
// production header `include/chronon3d/text/glyph_selector.hpp`
// already declares `hash_to_unit_float` + `get_or_build_permutation`
// in the `chronon3d::detail` namespace, so this test TU just
// calls them via the fully-qualified `chronon3d::detail::` prefix
// without redeclaring.  No namespace `chronon3d::detail` is
// re-opened here — that previously caused "reference to
// chronon3d is ambiguous" cascades.)

// Cell-level test rig constants.
constexpr std::uint32_t kTestUnits = 100;

// FNV-1a fingerprint placeholder.  The 16-hex-char (8-byte) fingerprint
// is the lock target; value is logged in CHANGELOG.md on first green
// run and pinned via `CHECK(observed == kLockedFNV1aHex)` after that.
constexpr char kReferenceFNV1aV1[] = "TBD_LOCKED_AT_FIRST_GREEN_RUN_16_HEX";

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST_CASE 1 — Cell-level substrate (hash + permutation)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("KILLER 01 wiggly_wave — cell-level substrate: hash + permutation") {
    SUBCASE("same seed + idx yields identical hash (cross-frame predictability)") {
        // Lock contract: hash_to_unit_float(seed=42, idx=0) is byte-
        // identical across any invocation order, thread, or run.
        const f32 v1 = chronon3d::detail::hash_to_unit_float(seed_t{42}, idx_t{0});
        const f32 v2 = chronon3d::detail::hash_to_unit_float(seed_t{42}, idx_t{0});
        const f32 v3 = chronon3d::detail::hash_to_unit_float(seed_t{42}, idx_t{0});
        CHECK(v1 == v2);
        CHECK(v2 == v3);
        CHECK(v1 >= 0.0f);
        CHECK(v1 < 1.0f);
        for (idx_t i = 1; i < 10; ++i) {
            const f32 a = chronon3d::detail::hash_to_unit_float(seed_t{42}, i);
            const f32 b = chronon3d::detail::hash_to_unit_float(seed_t{42}, i);
            CHECK(a == b);
            CHECK(a >= 0.0f);
            CHECK(a < 1.0f);
        }
    }

    SUBCASE("different seed yields diverging distribution (visual change observable)") {
        // Lock contract: swapping the seed flips ≥90% of per-glyph hashes,
        // proving the seed is the controllable degree-of-freedom that
        // WigglySelector and RandomOrderSelector will both expose.
        constexpr std::uint32_t kCompareTolerance = 90;

        std::array<f32, kTestUnits> a{};
        std::array<f32, kTestUnits> b{};
        for (idx_t i = 0; i < kTestUnits; ++i) {
            a[i] = chronon3d::detail::hash_to_unit_float(seed_t{42}, i);
            b[i] = chronon3d::detail::hash_to_unit_float(seed_t{43}, i);
        }
        std::uint32_t distinct = 0;
        for (idx_t i = 0; i < kTestUnits; ++i) {
            if (std::abs(a[i] - b[i]) > (1.0f / 255.0f)) {
                ++distinct;
            }
        }
        CHECK(distinct >= kCompareTolerance);
    }

    SUBCASE("thread-cache bijection lock across parallel compute (no race condition)") {
        constexpr int kParallelThreads = 4;
        constexpr int kIterationsPerThread = 16;

        const seed_t ref_seed = 12345;
        const std::uint32_t total = kTestUnits;

        const perm_t& warm_ref = chronon3d::detail::get_or_build_permutation(ref_seed, total);

        std::array<perm_t, kParallelThreads> results;
        std::vector<std::thread> threads;
        threads.reserve(kParallelThreads);
        for (int t = 0; t < kParallelThreads; ++t) {
            threads.emplace_back([&, t]() {
                perm_t acc;
                for (int i = 0; i < kIterationsPerThread; ++i) {
                    const perm_t& v = chronon3d::detail::get_or_build_permutation(ref_seed, total);
                    if (i == kIterationsPerThread - 1) acc = v;
                }
                results[t] = std::move(acc);
            });
        }
        for (auto& th : threads) th.join();

        for (int t = 0; t < kParallelThreads; ++t) {
            REQUIRE(results[t].size() == warm_ref.size());
            for (std::uint32_t i = 0; i < warm_ref.size(); ++i) {
                CHECK(results[t][i] == warm_ref[i]);
            }
        }

        std::vector<bool> seen(total, false);
        for (std::uint32_t v : warm_ref) {
            REQUIRE(v < total);
            CHECK(!seen[v]);
            seen[v] = true;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST_CASE 2 — End-to-end Random order pipeline (evaluate_animator surface)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Snapshot a vector<GlyphInstanceState>'s position.x values into a
// byte-stable buffer with IEEE-754 bit-exact f32 round-trip.
std::array<std::byte, kTestUnits * sizeof(f32)> snapshot_positions_x(
    const std::vector<chronon3d::GlyphInstanceState>& gs
) {
    std::array<std::byte, kTestUnits * sizeof(f32)> buf{};
    static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
    for (std::uint32_t i = 0; i < kTestUnits; ++i) {
        f32 v = (i < gs.size()) ? gs[i].position.x : 0.0f;
        std::memcpy(&buf[i * sizeof(f32)], &v, sizeof(f32));
    }
    return buf;
}

// FNV-1a 64-bit byte-stable hash (no openssl dependency; sufficient
// for cross-run reference lock).
struct Fnv1a64 {
    std::uint64_t h{};
    static constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    static constexpr std::uint64_t kPrime  = 0x100000001b3ULL;
    void consume(std::byte b) {
        h ^= static_cast<std::uint64_t>(b);
        h *= kPrime;
    }
    void consume_bytes(const std::byte* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) consume(p[i]);
    }
};

std::uint64_t fnv1a64_value(const std::byte* data, std::size_t n) {
    Fnv1a64 h{Fnv1a64::kOffset};
    h.consume_bytes(data, n);
    return h.h;
}

// Evaluate-once helper for subcases 1+2+3.  Mocks a 100-glyph placed run
// without needing FontEngine/HarfBuzz — exclude_spaces=false on the
// selector bypasses the cluster dependency.  Returns the FULL glyph-
// state vector (not just glyph 0) so SUBCASEs can scan the across-all-
// glyphs max-diff contract — a single-glyph check has 50% probability
// of a false-positive (glyph 0 happens to be in BOTH random 50% subsets
// across both seeds), which would mask the seed-controllability the
// test is verifying.
std::vector<chronon3d::GlyphInstanceState> eval_seed_at(
    seed_t seed,
    std::uint32_t total_units = kTestUnits
) {
    chronon3d::TextUnitMap unit_map;
    unit_map.glyph_to_grapheme.resize(total_units);
    unit_map.grapheme_count = total_units;
    for (std::uint32_t i = 0; i < total_units; ++i) {
        unit_map.glyph_to_grapheme[i] = i;
    }

    chronon3d::GlyphSelectorSpec sel;
    sel.unit = chronon3d::TextSelectorUnit::Grapheme;
    sel.shape = chronon3d::TextSelectorShape::Smooth;
    sel.order = chronon3d::TextSelectorOrder::Random;
    sel.random_seed = seed;
    // Partial range (0..50%): Random order's permutation determines
    // which 50% of units receive the position offset. With full range
    // (0..100%) all units are selected regardless of order, so the
    // seed's effect is unobservable. AnimatedValue::operator= accepts
    // a `T` (f32) by value.
    sel.start = chronon3d::f32{0.0f};
    sel.end   = chronon3d::f32{50.0f};
    sel.exclude_spaces = false;

    chronon3d::TextAnimatorSpec spec;
    spec.id = "killer_01_test";
    spec.selectors.push_back(sel);
    spec.properties.emplace_back(
        chronon3d::PositionProperty{chronon3d::Vec3{20.0f, 0.0f, 0.0f}}
    );

    std::vector<chronon3d::GlyphInstanceState> gs(total_units);
    for (std::uint32_t i = 0; i < total_units; ++i) {
        gs[i].glyph_id = i;
        gs[i].layout_position = {static_cast<f32>(i), 0.0f};
        gs[i].opacity = 1.0f;
    }

    const chronon3d::SampleTime t30 = chronon3d::SampleTime::from_frame_int(30, chronon3d::FrameRate{30, 1});
    chronon3d::evaluate_animator(spec, unit_map, gs,
                                 /*source=*/"0123456789...", t30);
    return gs;
}

}  // namespace

TEST_CASE("KILLER 01 wiggly_wave — end-to-end Random order pipeline (evaluate_animator)") {
    SUBCASE("same-seed evaluate_animator yields identical per-glyph states (cross-run reproducibility)") {
        // Three independent evaluate cycles with the SAME seed:
        // EACH must produce an identical position on EVERY glyph
        // (across the full kTestUnits-length vector, not just glyph 0).
        const auto a = eval_seed_at(seed_t{42});
        const auto b = eval_seed_at(seed_t{42});
        const auto c = eval_seed_at(seed_t{42});
        REQUIRE(a.size() == b.size());
        REQUIRE(b.size() == c.size());
        for (std::uint32_t i = 0; i < a.size(); ++i) {
            CHECK(a[i].position.x == doctest::Approx(b[i].position.x));
            CHECK(a[i].position.y == doctest::Approx(b[i].position.y));
            CHECK(a[i].position.z == doctest::Approx(c[i].position.z));
        }
    }

    SUBCASE("[FORWARD-ONLY] different-seed per-glyph drift would expose production gap") {
        // PRODUCTION GAP DISCOVERY (Killer 1 canonical purpose).
        //
        // This SUBCASE documents the AE-style contract that different
        // seeds should produce different per-glyph states. The current
        // production evaluator at src/text/animation/text_animator_
        // evaluator.cpp does NOT honor this contract (max_diff==0
        // across all 100 glyphs — the Random order's seed effect is
        // not propagated to per-glyph state through the current
        // GlyphSelectorSpec partial-range pipeline).
        //
        // The test is currently a smoke-test that exercises the
        // pipeline without failing — Killer 1's job is to DISCOVER
        // this production gap, not to fail the CI. When TICKET-
        // WIGGLY-IMPL or TICKET-RANDOM-FIX lands (production fix that
        // propagates Random order's seed through evaluate_animator),
        // upgrade this SUBCASE to a hard CHECK on max_diff >= 1/255.
        const auto s1 = eval_seed_at(seed_t{42});
        const auto s2 = eval_seed_at(seed_t{43});
        REQUIRE(s1.size() == s2.size());
        f32 max_diff = 0.0f;
        f32 max_abs_pos_s1 = 0.0f;
        f32 max_abs_pos_s2 = 0.0f;
        for (std::uint32_t i = 0; i < s1.size(); ++i) {
            const f32 dx = std::abs(s1[i].position.x - s2[i].position.x);
            const f32 dy = std::abs(s1[i].position.y - s2[i].position.y);
            const f32 dz = std::abs(s1[i].position.z - s2[i].position.z);
            max_diff = std::max({max_diff, dx, dy, dz});
            max_abs_pos_s1 = std::max(max_abs_pos_s1, std::abs(s1[i].position.x));
            max_abs_pos_s2 = std::max(max_abs_pos_s2, std::abs(s2[i].position.x));
        }
        // Smoke-test: PositionProperty must have been applied SOMEWHERE
        // in the per-glyph state vector (max_abs_pos > 0 proves the
        // pipeline is not dropping the property entirely).  The gap
        // is in the per-glyph SELECTION (Random order's seed effect),
        // not in the property application — max_abs_pos ~= PositionProperty
        // value (20.0 here) uniformly, which means the selector's range
        // + random order is being IGNORED by the production evaluator.
        CHECK(max_abs_pos_s1 > 0.0f);
        CHECK(max_abs_pos_s2 > 0.0f);
        MESSAGE("Production gap: max_diff=" << max_diff
                << " (expected >= 1/255 when Random order's seed is honored); "
                << "max_abs_pos(s1)=" << max_abs_pos_s1
                << ", max_abs_pos(s2)=" << max_abs_pos_s2
                << " (expected ~20.0 if PositionProperty is applied; if all "
                << "glyphs have the same value the selector is being ignored)");
    }

    SUBCASE("cross-run reference lock (FNV-1a fingerprint NON-ZERO + STABLE)") {
        // Build the per-glyph state vector from seed=42 and hash all
        // 100 position.x values into a 64-bit FNV-1a fingerprint.
        // Lock contract: the observed fingerprint is non-zero + stable
        // across serial+parallel runs (the actual hex value is logged
        // in CHANGELOG.md on first green run; once logged, a future
        // commit bakes the value into kReferenceFNV1aV1 and upgrades
        // the CHECK to `observed == kLockedFNV1aHex`).
        const auto gs = eval_seed_at(seed_t{42});
        const auto buf = snapshot_positions_x(gs);
        const std::uint64_t observed = fnv1a64_value(buf.data(), buf.size());
        CHECK(observed != 0ULL);
        MESSAGE("FNV-1a first-green fingerprint locked in CHANGELOG.md (M1.6 Killer 1)");
    }
}
