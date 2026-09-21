#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_text_font_fixture.hpp — Shared font fixture for text tests
// ═══════════════════════════════════════════════════════════════════════════
//
// Cat-1 build fix: resolves unity-build redefinition of inter_bold()
// previously defined in anonymous namespace across 4 files:
//   - test_crossfade_stroke.cpp
//   - test_draw_text_run_crossfade_stroke.cpp
//   - test_draw_text_run_scratch_state.cpp
//   - test_prewarm_text_layout_cache.cpp
//
// All helpers are inline to satisfy ODR when multiple TUs include this
// header in non-unity builds.

#include <chronon3d/text/font_engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace test_text_fixture {

constexpr const char* kInterBoldPath = "tests/fixtures/Inter-Bold.ttf";
// When assets/ has been bootstrapped this resolves to the fleet entry
// Chronon3d/assets/fonts/Inter-Bold.ttf — same bytes as the fixture above.

inline bool fixture_exists(const char* p) noexcept {
    if (p == nullptr) return false;
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

inline void skip_if_missing(const char* fixture, const char* what) noexcept {
    if (!fixture_exists(fixture)) {
        MESSAGE("Skipping: ", what, " requires ", fixture,
                " which is unavailable.");
    }
}

/// Canonical Inter-Bold font fixture for deterministic text tests.
/// Font path: tests/fixtures/Inter-Bold.ttf (tracked in git).
///
/// Rationale vs. assets/fonts/Inter-Bold.ttf: the bootstrap fleet under
/// assets/ is gitignored and materialized on demand by
/// tools/bootstrap_test_fonts.py (blob SHA-1 pinned). This fixture is a
/// self-contained tracked copy so text unit tests run on a fresh checkout
/// without a prior bootstrap step; the MD5-duplicate with the fleet is
/// intentional and documented (see DEDUP.md font-fleet policy).
inline chronon3d::FontSpec inter_bold() {
    const auto fixture = std::filesystem::absolute(kInterBoldPath).string();
    return chronon3d::FontSpec{
        .font_path   = fixture,
        .font_family = "Inter",
        .font_weight = 700,
        .font_style  = "normal",
        .font_size   = 32.0f,
    };
}

} // namespace test_text_fixture
