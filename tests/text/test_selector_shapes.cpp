#include "glyph_selector_helpers.hpp"
using namespace chronon3d;
using namespace test_glyph_sel;

// ═══════════════════════════════════════════════════════════════════════════
// Selector shape tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Shape: Square") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.5f, 0.2f, 0.8f) == doctest::Approx(1.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.1f, 0.2f, 0.8f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.9f, 0.2f, 0.8f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.2f, 0.2f, 0.8f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.8f, 0.2f, 0.8f) == doctest::Approx(0.0f));
}

TEST_CASE("Shape: RampUp") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::RampUp, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampUp, 0.5f, 0.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampUp, 1.0f, 0.0f, 1.0f) == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("Shape: RampDown") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::RampDown, 0.0f, 0.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampDown, 0.5f, 0.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampDown, 1.0f, 0.0f, 1.0f) == doctest::Approx(0.0f).epsilon(0.001f));
}

TEST_CASE("Shape: Triangle") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 0.5f, 0.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 1.0f, 0.0f, 1.0f) == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 0.25f, 0.0f, 1.0f) == doctest::Approx(0.5f));
}

TEST_CASE("Shape: Round") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::Round, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    f32 centre = evaluate_selector_shape(TextSelectorShape::Round, 0.5f, 0.0f, 1.0f);
    CHECK(centre == doctest::Approx(1.0f));
    f32 left = evaluate_selector_shape(TextSelectorShape::Round, 0.25f, 0.0f, 1.0f);
    f32 right = evaluate_selector_shape(TextSelectorShape::Round, 0.75f, 0.0f, 1.0f);
    CHECK(left == doctest::Approx(right));
}

TEST_CASE("Shape: Smooth") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::Smooth, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    f32 centre = evaluate_selector_shape(TextSelectorShape::Smooth, 0.5f, 0.0f, 1.0f);
    CHECK(centre == doctest::Approx(1.0f));
    f32 left = evaluate_selector_shape(TextSelectorShape::Smooth, 0.25f, 0.0f, 1.0f);
    f32 right = evaluate_selector_shape(TextSelectorShape::Smooth, 0.75f, 0.0f, 1.0f);
    CHECK(left == doctest::Approx(right));
}

TEST_CASE("Shape: reversed range (start > end)") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.1f, 0.8f, 0.2f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.5f, 0.8f, 0.2f) == doctest::Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// Order tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Order: Forward") {
    using namespace detail;
    CHECK(apply_selector_order(TextSelectorOrder::Forward, 0, 5, 0) == 0);
    CHECK(apply_selector_order(TextSelectorOrder::Forward, 4, 5, 0) == 4);
}

TEST_CASE("Order: Reverse") {
    using namespace detail;
    CHECK(apply_selector_order(TextSelectorOrder::Reverse, 0, 5, 0) == 4);
    CHECK(apply_selector_order(TextSelectorOrder::Reverse, 4, 5, 0) == 0);
}

TEST_CASE("Order: FromCenter even count") {
    using namespace detail;
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 4, 0) == 2);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 4, 0) == 0);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 2, 4, 0) == 1);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 3, 4, 0) == 3);

    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 2, 6, 0) == 0);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 3, 6, 0) == 1);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 6, 0) == 2);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 4, 6, 0) == 3);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 6, 0) == 4);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 5, 6, 0) == 5);

    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 3, 8, 0) == 0);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 4, 8, 0) == 1);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 2, 8, 0) == 2);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 5, 8, 0) == 3);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 8, 0) == 4);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 6, 8, 0) == 5);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 8, 0) == 6);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 7, 8, 0) == 7);

    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 2, 0) == 0);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 2, 0) == 1);
}

TEST_CASE("Order: Random is bijective (true permutation, no collisions)") {
    using namespace detail;
    for (u32 n : {1u, 2u, 3u, 5u, 10u, 16u, 50u, 100u}) {
        const auto& perm = get_or_build_permutation(42, n);
        REQUIRE(perm.size() == n);

        std::vector<u32> sorted(perm.begin(), perm.end());
        std::sort(sorted.begin(), sorted.end());
        for (u32 i = 0; i < n; ++i) {
            CHECK(sorted[i] == i);
        }
    }
}

TEST_CASE("Order: Random same seed -> same permutation; shuffle is non-trivial") {
    using namespace detail;
    constexpr u32 N = 32;
    constexpr u64 SEED_A = 12345;

    const auto& a1 = get_or_build_permutation(SEED_A, N);
    const auto& a2 = get_or_build_permutation(SEED_A, N);
    REQUIRE(a1.size() == N);
    REQUIRE(a2.size() == N);
    for (u32 i = 0; i < N; ++i) {
        CHECK(a1[i] == a2[i]);
    }

    size_t differ_count = 0;
    for (size_t i = 0; i < a1.size(); ++i) {
        if (a1[i] != static_cast<u32>(i)) ++differ_count;
    }
    CHECK(differ_count > 0);
    CHECK(std::is_sorted(a1.begin(), a1.end()) == false);
}

TEST_CASE("Order: Random via apply_selector_order is consistent with bijection") {
    using namespace detail;
    for (u32 n : {4u, 7u, 12u}) {
        std::vector<u32> outputs;
        outputs.reserve(n);
        for (u32 i = 0; i < n; ++i) {
            outputs.push_back(apply_selector_order(
                TextSelectorOrder::Random, i, n, 99));
        }
        std::vector<u32> sorted(outputs.begin(), outputs.end());
        std::sort(sorted.begin(), sorted.end());
        for (u32 i = 0; i < n; ++i) {
            CHECK(sorted[i] == i);
        }
    }
}

TEST_CASE("Order: FromCenter odd count") {
    using namespace detail;
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 2, 5, 0) == 0);
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 1, 5, 0) == 1);
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 3, 5, 0) == 2);
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 0, 5, 0) == 3);
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 4, 5, 0) == 4);
}

TEST_CASE("Order: ToCenter") {
    using namespace detail;
    auto fc = apply_selector_order(TextSelectorOrder::FromCenter, 0, 4, 0);
    auto tc = apply_selector_order(TextSelectorOrder::ToCenter, 0, 4, 0);
    CHECK(tc == 4 - 1 - fc);
}

TEST_CASE("Order: Random is deterministic via apply_selector_order") {
    using namespace detail;

    u64 seed = 42;
    auto a = apply_selector_order(TextSelectorOrder::Random, 3, 10, seed);
    auto b = apply_selector_order(TextSelectorOrder::Random, 3, 10, seed);
    CHECK(a == b);

    auto c = apply_selector_order(TextSelectorOrder::Random, 3, 10, seed + 1);
    CHECK(c < 10);
}

// ═══════════════════════════════════════════════════════════════════════════
// Hash determinism tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Hash: same input → same output") {
    using namespace detail;

    CHECK(hash_to_unit_float(12345, 67) == hash_to_unit_float(12345, 67));
}

TEST_CASE("Hash: different seed → different output") {
    using namespace detail;

    f32 a = hash_to_unit_float(1, 100);
    f32 b = hash_to_unit_float(2, 100);
    CHECK(a != b);
    CHECK(a >= 0.0f);
    CHECK(a < 1.0f);
    CHECK(b >= 0.0f);
    CHECK(b < 1.0f);
}

TEST_CASE("Hash: different unit_index → different output") {
    using namespace detail;

    f32 a = hash_to_unit_float(42, 0);
    f32 b = hash_to_unit_float(42, 1);
    CHECK(a != b);
}
