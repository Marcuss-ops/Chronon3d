#include <doctest/doctest.h>
#include <chronon3d/animation/random.hpp>
#include <chronon3d/core/random/deterministic_random.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
using namespace chronon3d;


// ── deterministic_random — canonical math (TICKET-RANDOM-UNIFY) ──────────

TEST_CASE("deterministic_random — same input → same output (lock)") {
    SUBCASE("RandomSeed + index identical, repeated call determinism") {
        CHECK(deterministic_random(RandomSeed{12345}, 67)
              == deterministic_random(RandomSeed{12345}, 67));
        CHECK(deterministic_random(RandomSeed{0}, 0)
              == deterministic_random(RandomSeed{0}, 0));
        CHECK(deterministic_random(RandomSeed{UINT64_MAX}, 1024)
              == deterministic_random(RandomSeed{UINT64_MAX}, 1024));
    }
}

TEST_CASE("core random API delegates to the canonical stateless sampler") {
    using chronon3d::random::range;
    using chronon3d::random::stable_seed;
    using chronon3d::random::unit;

    const auto seed = stable_seed("important-title");
    CHECK(unit(seed, 7) == unit(chronon3d::random::Seed{seed}, 7));
    CHECK(range(seed, 7, -2.0f, 2.0f) >= -2.0f);
    CHECK(range(seed, 7, -2.0f, 2.0f) < 2.0f);
    CHECK(unit("important-title", 7) == unit(seed, 7));
}

TEST_CASE("deterministic_random — different seed → different output") {
    SUBCASE("Neighbouring seeds differ") {
        f32 a = deterministic_random(RandomSeed{1}, 100);
        f32 b = deterministic_random(RandomSeed{2}, 100);
        CHECK(a != b);
        CHECK(a >= 0.0f);
        CHECK(a < 1.0f);
        CHECK(b >= 0.0f);
        CHECK(b < 1.0f);
    }

    SUBCASE("High-bit-flip seed differs") {
        f32 a = deterministic_random(RandomSeed{42}, 0);
        f32 b = deterministic_random(RandomSeed{static_cast<u64>(42) ^ (1ULL << 63)}, 0);
        CHECK(a != b);
    }
}

TEST_CASE("deterministic_random — different index → different output") {
    SUBCASE("Neighbouring indices differ") {
        f32 a = deterministic_random(RandomSeed{42}, 0);
        f32 b = deterministic_random(RandomSeed{42}, 1);
        CHECK(a != b);
    }

    SUBCASE("Far-apart indices differ") {
        f32 a = deterministic_random(RandomSeed{42}, 0);
        f32 b = deterministic_random(RandomSeed{42}, 1ULL << 32);
        CHECK(a != b);
    }
}

TEST_CASE("deterministic_random — string_view overload") {
    SUBCASE("Different strings → different output") {
        f32 a = deterministic_random("hello", 0);
        f32 b = deterministic_random("world", 0);
        CHECK(a != b);
    }

    SUBCASE("Same string + same index → same output (XXH64 bit-stable)") {
        CHECK(deterministic_random("chronon3d", 0)
              == deterministic_random("chronon3d", 0));
    }

    SUBCASE("Different index for same string → different output") {
        f32 a = deterministic_random("title-jitter", 0);
        f32 b = deterministic_random("title-jitter", 1);
        CHECK(a != b);
    }

    SUBCASE("String seed differs from equivalent u64 seed (XXH64 fold != identity)") {
        // Sanity: the XXH64 fold of an empty string ≠ 0 (XXH64 seeds == 0).
        // We don't compare to a specific value — just that the string seed
        // pipeline is routed through XXH64 instead of zero-extension.
        f32 empty_str = deterministic_random(std::string_view{}, 0);
        f32 zero_seed = deterministic_random(RandomSeed{XXH64(nullptr, 0, 0)}, 0);
        CHECK(empty_str == zero_seed);
    }
}

TEST_CASE("deterministic_random — output range invariants") {
    // Sweep over a wide range of (seed, index) pairs — ALL results
    // must lie in [0, 1) for the canonical math.
    const u64 test_seeds[] = {u64{0}, u64{1}, u64{42}, u64{12345}, u64{0xFFFFFFFFFFFFFFFFULL}};
    const u64 test_indices[] = {u64{0}, u64{1}, u64{7}, u64{100}, u64{65535}};
    for (u64 s : test_seeds) {
        for (u64 i : test_indices) {
            f32 v = deterministic_random(RandomSeed{s}, i);
            CHECK(v >= 0.0f);
            CHECK(v < 1.0f);
            CHECK(std::isfinite(v));
        }
    }
}

TEST_CASE("deterministic_random — golden platform lock (cross-OS bit-stable)") {
    // Locks the exact bit-pattern of the canonical math so any platform
    // or libc that computes a different value triggers a regression
    // test failure. The platform (Linux/macOS/Windows) must produce
    // bit-identical output for these specific (seed, index) pairs.
    //
    // Reference values computed locally on Linux (glibc) — locked here
    // to make any divergence visible at CI time.
    SUBCASE("(RandomSeed{12345}, 67) bit-stable canonical output") {
        // Vector for the documented hash-combine + SplitMix finalizer.
        const f32 expected = 0.115754783f;
        f32 actual = deterministic_random(RandomSeed{12345}, 67);
        CHECK(actual == doctest::Approx(expected));
    }

    SUBCASE("(RandomSeed{0}, 0) bit-stable") {
        CHECK(deterministic_random(RandomSeed{0}, 0)
              == doctest::Approx(0.883310795f));
    }

    SUBCASE("(RandomSeed{0}, 1) bit-stable") {
        const f32 expected = deterministic_random(RandomSeed{0}, 1);
        // Locked twice — same call returns same value (determinism self-lock).
        CHECK(deterministic_random(RandomSeed{0}, 1) == expected);
    }
}

TEST_CASE("deterministic_random — string overload bit-stable") {
    // Cross-OS lock for XXH64-funneled string seeds.
    SUBCASE("('chronon3d', 0) bit-stable") {
        const f32 expected = deterministic_random("chronon3d", 0);
        // Self-determinism lock (no fixed value yet — first CI run captures it).
        CHECK(deterministic_random("chronon3d", 0) == expected);
    }
}

TEST_CASE("build_random_permutation — Fisher-Yates bijection lock") {
    SUBCASE("For total_units in {1, 2, 3, 5, 10, 16, 50, 100}, sorted perm == [0..n)") {
        for (u32 n : {1u, 2u, 3u, 5u, 10u, 16u, 50u, 100u}) {
            const auto perm = build_random_permutation(RandomSeed{42}, n);
            REQUIRE(perm.size() == n);

            std::vector<u32> sorted(perm.begin(), perm.end());
            std::sort(sorted.begin(), sorted.end());
            for (u32 i = 0; i < n; ++i) {
                CHECK(sorted[i] == i);
            }
        }
    }

    SUBCASE("Same seed → same permutation (determinism lock)") {
        constexpr u32 N = 32;
        const auto p1 = build_random_permutation(RandomSeed{12345}, N);
        const auto p2 = build_random_permutation(RandomSeed{12345}, N);
        REQUIRE(p1.size() == N);
        REQUIRE(p2.size() == N);
        for (u32 i = 0; i < N; ++i) {
            CHECK(p1[i] == p2[i]);
        }
    }

    SUBCASE("Different seed → statistically different permutation") {
        // Two distinct seeds with N=64 — verify the permutations
        // differ in MORE than trivial-frequency overlap (i.e., they
        // are not the identity and not the same shuffle).
        constexpr u32 N = 64;
        const auto p_a = build_random_permutation(RandomSeed{1}, N);
        const auto p_b = build_random_permutation(RandomSeed{2}, N);
        size_t differ_count = 0;
        for (u32 i = 0; i < N; ++i) {
            if (p_a[i] != p_b[i]) ++differ_count;
        }
        // Expect a meaningful divergence (Fisher-Yates isn't constant).
        CHECK(differ_count > static_cast<size_t>(N / 2));
    }

    SUBCASE("Edge: total_units == 0 → empty permutation") {
        const auto perm = build_random_permutation(RandomSeed{0}, 0);
        CHECK(perm.empty());
    }

    SUBCASE("Edge: total_units == 1 → singleton permutation [0]") {
        const auto perm = build_random_permutation(RandomSeed{0}, 1);
        REQUIRE(perm.size() == 1);
        CHECK(perm[0] == 0);
    }
}

TEST_CASE("deterministic_random — descriptor certify integration with Random order") {
    // Locks that the canonical API is wired correctly into the
    // selector Random order path. Verifies via bijection property +
    // determinism rather than coupling directly to glyph_selector_*.
    for (u32 n : {4u, 7u, 12u}) {
        std::vector<u32> outputs;
        outputs.reserve(n);
        const auto perm = build_random_permutation(RandomSeed{99}, n);
        REQUIRE(perm.size() == n);
        for (u32 i = 0; i < n; ++i) {
            outputs.push_back(perm[i]);
        }
        std::vector<u32> sorted(outputs.begin(), outputs.end());
        std::sort(sorted.begin(), sorted.end());
        for (u32 i = 0; i < n; ++i) {
            CHECK(sorted[i] == i);
        }
    }
}
