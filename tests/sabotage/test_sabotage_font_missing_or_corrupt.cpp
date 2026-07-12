// tests/sabotage/test_sabotage_font_missing_or_corrupt.cpp
// ============================================================================
// Sabotage scenario: font missing OR font file corrupt.
//
// Closed forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS (closed 2026-07-12 by
// this cat-1 extension).  The production fail-loud hook is wired:
// `attempt_font_load_real()` calls the canonical `chronon3d::FontEngine::can_load()`
// plus `chronon3d::FontEngine::shape_text()` through the real `AssetResolver` —
// NOT a stub that returns a hard-coded value.
//
// §Honest spec-variant (this chore, Option A from ask_user disposition):
//   The user verbatim contract uses `render_composition_with_font(...)` and
//   `RenderErrorCode::FontNotFound`.  Neither is canonical:
//     * `render_composition_with_font` — 0 matches in the codebase.  The
//       closest equivalent is the canonical `FontEngine::can_load(spec)`
//       + `FontEngine::shape_text(...)` API.  This extension calls those
//       directly through `AssetResolver::mount()` + `FontEngine(resolver)`.
//     * `RenderErrorCode::FontNotFound` — 0 matches in EITHER
//       `chronon3d::sdk::RenderErrorCode` (sdk/render_error.hpp)
//       OR `chronon3d::render::RenderErrorCode` (render/render_diagnostic.hpp).
//       There is no typed-error-code surface for font-loading failures;
//       `FontEngine::can_load` returns `bool` and `shape_text` returns
//       `std::optional<GlyphRun>` (free `std::nullopt` on failure, with
//       a spdlog warning).  The faithful failure expression is therefore
//       `can_load(spec) == false` + `shape_text(...) == std::nullopt`,
//       NOT `RenderErrorCode::FontNotFound` — Cat-4 surface-freeze rule
//       preserved (no new public enum values without ADR).
//
// CHRONON3D_ENABLE_TEXT-aware behavior:
//   * When CHRONON3D_ENABLE_TEXT=ON: real FreeType + HarfBuzz pipeline.
//     `FT_New_Face` fails on missing file + fails on 'WXYZ' magic header
//     in `corrupt-font.ttf`.  Both SUBCASEs produce REQUIRE_FALSE.
//   * When CHRONON3D_ENABLE_TEXT=OFF: the FontEngine stub Impl returns
//     `false` from can_load() and `std::nullopt` from shape_text()
//     unconditionally.  Both SUBCASEs still produce REQUIRE_FALSE
//     (the pipeline correctly reports "no FreeType lib wired").
//
// Determinism contract for the corrupt-font fixture:
//   `tests/fixtures/fonts/corrupt-font.ttf` is 256 bytes:
//     * Bytes 0-3: ASCII "WXYZ" (NOT a valid TTF magic — TTF=0x00010000
//       / OTF=OTTO / TTC=ttcf / MacTT=true / PS-Typ1=typ1).
//     * Bytes 4-255: 252 zero bytes (deterministic — NOT random).
//   sha256: ead7718e729ee25fae4e05ae7c95c9094a0cfb61ed9523e5defc74f422c34932
// ============================================================================
#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <filesystem>
#include <string>

namespace chronon3d::sabotage::font {

// Cat-1 source extension (TICKET-TEXT-V1-CERT-STEP10-NEGATIVE-FONT, 2026-07-12).
// Real pipeline call: AssetResolver -> FontEngine::can_load -> FontEngine::shape_text.
// ALL call sites go through the canonical production code path; nothing here is
// hard-coded to return a deterministic value.  The stub `attempt_font_load(const char*)`
// that previously returned `false` unconditionally has been DELETED in this commit.
//
// Failure semantics:
//   * Missing font file: `AssetResolver::resolve_lexical` returns the candidate
//     path; `FT_New_Face(ft_library, resolved.c_str(), 0, &face)` returns
//     nonzero error; spdlog warn; load_face returns std::nullopt; can_load
//     returns `false`.  `shape_text` short-circuits via the face_cache miss
//     + load_face failure and returns std::nullopt.
//   * Corrupt font file: same path — FT_New_Face rejects the "WXYZ" magic
//     header (`FT_Err_Unknown_File_Format`) and returns std::nullopt.
[[nodiscard]] bool attempt_font_load_real(
    const char* font_path,
    const chronon3d::assets::AssetResolver& resolver)
{
    if (font_path == nullptr) {
        return false;
    }
    chronon3d::FontEngine engine{resolver};
    chronon3d::FontSpec spec{
        font_path,
        "<sabotage-missing-fixture>",
        /*font_weight=*/400,
        /*font_style=*/"normal",
        /*font_size=*/16.0f
    };
    if (!engine.can_load(spec)) {
        return false;
    }
    // Second-chance probe — even if FT_New_Face accepts the file (rare, but
    // possible for a corrupt file that happens to parse partially), the shaping
    // call will return std::nullopt on any post-init failure.
    auto shaped = engine.shape_text("Agg", spec, 16.0f);
    return shaped.has_value();
}

} // namespace chronon3d::sabotage::font

TEST_CASE(
    "sabotage/font_missing_or_corrupt: missing font fails loudly via real pipeline "
    "[comp=TextComposition][layer=TextLayoutLayer][node=BlFontFaceCache]"
) {
    INFO("Comp=TextComposition");
    INFO("Layer=TextLayoutLayer");
    INFO("Node=BlFontFaceCache");
    INFO("Closed: TICKET-SABOTAGE-PRODUCTION-HOOKS (cat-1 extension this commit)");
    INFO("TICKET-TEXT-V1-CERT-STEP10-NEGATIVE-FONT — real pipeline cert");

    // Mount the resolver at the workspace root (absolutely required by
    // AssetResolver::mount() per PR 8.0 contract — relative mount root
    // throws std::invalid_argument).  CTest runs each test with
    // WORKING_DIRECTORY = ${CMAKE_SOURCE_DIR}, so the workspace root IS
    // the test process's cwd.
    const std::filesystem::path workspace_root =
        std::filesystem::absolute(std::filesystem::current_path());
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(workspace_root);

    SUBCASE("missing font file returns failure through real pipeline") {
        REQUIRE_FALSE(chronon3d::sabotage::font::attempt_font_load_real(
            "tests/fixtures/fonts/does-not-exist.ttf", resolver));
    }

    SUBCASE("corrupt font file returns failure through real pipeline") {
        REQUIRE_FALSE(chronon3d::sabotage::font::attempt_font_load_real(
            "tests/fixtures/fonts/corrupt-font.ttf", resolver));
    }
}
