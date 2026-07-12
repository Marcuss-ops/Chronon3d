// tests/sabotage/test_sabotage_font_missing_or_corrupt.cpp
// ============================================================================
// Sabotage scenario: font missing OR font file corrupt — through the REAL
// FontEngine (FreeType + HarfBuzz), NOT a stub.
//
// This suite REPLACES the previous false-green version that used:
//   bool attempt_font_load(const char*) { return false; }
// which always returned false regardless of the real engine state, so the
// suite passed even when the FontEngine was broken.  See
// TICKET-SABOTAGE-FONT-REAL-ENGINE for the closure lineage.
//
// The new tests use the production FontEngine API:
//   renderer->font_engine()->shape_text(...)      // production path
// (mirrored below via `FontEngine{resolver}.shape_text(...)` for TU-local
// standalone use — the same helper pattern as
// `tests/text/test_text_font_determinism.cpp::make_determinism_engine()`).
//
// Distinguishes "broken engine" from "working engine":
//   * OLD test: `attempt_font_load` stub ALWAYS returned false → suite
//     always passed (false-green), even when the real engine was broken.
//   * NEW tests: the real `engine.shape_text(...)` API is wired to
//     FreeType + HarfBuzz.  If the engine is broken (FreeType fails to
//     init, or a future regression replaces `shape_text` with
//     always-success), the Guard test FAILS and the Missing/Corrupt
//     tests would produce misleading results.  The Guard test is the
//     positive sanity that anchors the 2 failure-mode tests.
//
// Fixtures:
//   * tests/fixtures/fonts/does-not-exist.ttf  — path does NOT exist on
//                                                 disk (the asset is
//                                                 intentionally absent).
//   * tests/fixtures/fonts/corrupt.ttf         — REAL corrupt TTF:
//                                                  broken sfVersion magic
//                                                  0xDEADBEEF (valid TTF
//                                                  = 0x00010000) + 256
//                                                  bytes of zero padding
//                                                  (clearly truncated
//                                                  structure).  This is
//                                                  a non-empty binary
//                                                  file (260 bytes), NOT
//                                                  a 0-byte stub.
// ============================================================================
#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>

namespace {

/// Build a FontEngine bound to a RenderRuntime — mirrors the production
/// path `SoftwareRenderer::font_engine()` (which returns `const FontEngine&`)
/// by constructing the same FontEngine from the canonical RenderRuntime.
/// Same 3-line helper pattern as
/// `tests/text/test_text_font_determinism.cpp::make_determinism_engine()`
/// (static `Config` + static `RenderRuntime` + `FontEngine{runtime.resolver()}`).
chronon3d::FontEngine make_sabotage_engine() {
    static const chronon3d::Config cfg;
    static const chronon3d::runtime::RenderRuntime runtime(cfg);
    return chronon3d::FontEngine{runtime.resolver()};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Guard test: ensure the real FontEngine works on a KNOWN-GOOD font.
// ═══════════════════════════════════════════════════════════════════════════
//
// If this test fails, the engine is broken and the missing/corrupt tests
// below would produce misleading results.  This guard makes the suite
// distinguish "broken engine" (FAIL on guard) from "working engine"
// (PASS on guard, then exercise the 2 failure modes).

TEST_CASE(
    "sabotage/font_missing_or_corrupt: guard — real FontEngine works on "
    "known-good Inter-Bold.ttf [comp=TextComposition][layer=TextLayoutLayer]"
) {
    INFO("Comp=TextComposition");
    INFO("Layer=TextLayoutLayer");
    INFO("Forward-point: TICKET-SABOTAGE-FONT-REAL-ENGINE");

    auto engine = make_sabotage_engine();

    chronon3d::FontSpec inter;
    inter.font_path   = "assets/fonts/Inter-Bold.ttf";
    inter.font_family = "Inter";
    inter.font_weight = 700;

    auto result = engine.shape_text("HELLO", inter, 72.0f);
    REQUIRE(result.has_value());
    REQUIRE(!result->glyphs.empty());
    CHECK(result->width > 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Missing font: path does not exist on disk.
// ═══════════════════════════════════════════════════════════════════════════
//
// This test uses the real FontEngine::shape_text() API.  It will FAIL if:
//   * The real engine is replaced with a stub that always returns success
//   * The real engine crashes / throws on missing files (should return
//     std::nullopt, not crash)
//   * The real engine silently loads a fallback font instead of returning
//     nullopt (silent-fallback would mask the asset error per Cat-3
//     fail-loud contract)

TEST_CASE(
    "sabotage/font_missing_or_corrupt: Missing font fails through the real "
    "FontEngine [comp=TextComposition][layer=TextLayoutLayer][node=BlFontFaceCache]"
) {
    INFO("Comp=TextComposition");
    INFO("Layer=TextLayoutLayer");
    INFO("Node=BlFontFaceCache");
    INFO("Forward-point: TICKET-SABOTAGE-FONT-REAL-ENGINE");

    auto engine = make_sabotage_engine();

    auto result = engine.shape_text(
        "HELLO",
        chronon3d::FontSpec{
            "tests/fixtures/fonts/does-not-exist.ttf",
            "Missing",
            400
        },
        72.0f
    );

    REQUIRE_FALSE(result);
}

// ═══════════════════════════════════════════════════════════════════════════
// Corrupt font: file exists on disk but is NOT a valid TTF (broken magic
// number + truncated structure).
// ═══════════════════════════════════════════════════════════════════════════
//
// Fixture: tests/fixtures/fonts/corrupt.ttf — generated binary with a
// broken sfVersion magic number (0xDEADBEEF instead of 0x00010000) and
// 256 bytes of zero padding.  This is a REAL corrupt TTF, not a stub:
// the file is a non-empty binary (260 bytes) that FreeType must explicitly
// reject at the magic-number check.

TEST_CASE(
    "sabotage/font_missing_or_corrupt: Corrupt font fails through the real "
    "FontEngine [comp=TextComposition][layer=TextLayoutLayer][node=BlFontFaceCache]"
) {
    INFO("Comp=TextComposition");
    INFO("Layer=TextLayoutLayer");
    INFO("Node=BlFontFaceCache");
    INFO("Forward-point: TICKET-SABOTAGE-FONT-REAL-ENGINE");

    auto engine = make_sabotage_engine();

    auto result = engine.shape_text(
        "HELLO",
        chronon3d::FontSpec{
            "tests/fixtures/fonts/corrupt.ttf",
            "Corrupt",
            400
        },
        72.0f
    );

    REQUIRE_FALSE(result);
}
