// ═══════════════════════════════════════════════════════════════════════════
// test_glyph_atlas_metadata.cpp — TU regression per Fase 1.3 del piano text
//
// Chiusura del verdict Issue #4: prima di questo commit, il cache
// GlyphAtlas conservava SOLO std::shared_ptr<BLImage> e ricostruiva un
// GlyphAtlasEntry con x_offset/y_offset/advance_x/fill_color_rgba tutti
// zero al lookup.  Dopo il refactor, la cache conserva l'intera
// GlyphAtlasEntry e tutti i 5 metadata vengono preservati attraverso
// store + lookup.
//
// Test cases (Catch2):
//   1. lookup di un glyph_id MAI memorizzato → std::nullopt.
//   2. store di un entry con TUTTI i 5 campi popolati (image allocata,
//      x_offset != 0, y_offset != 0, advance_x != 0, fill_color_rgba
//      specifico);
//      lookup con stessa chiave ritorna entry IDENTICA — nessun campo
//      azzerato.  Questo è il test che falliva prima del refactor.
//   3. clear resetta la cache; lookup dopo clear ritorna std::nullopt
//      anche per la chiave appena memorizzata.
//   4. cache stats: entry_count increment dopo store, azzerato dopo clear.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/glyph_atlas.hpp>

#include <blend2d.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>

namespace {

// Helper: build a tiny 4×4 BLImage (PRGB32) so the GlyphAtlasEntry has
// a valid image that size() returns width*height*4 for the weight.
std::shared_ptr<BLImage> make_test_glyph_image() {
    auto img = std::make_shared<BLImage>(/*w=*/4, /*h=*/4, BL_FORMAT_PRGB32);
    return img;
}

// Helper: construct a fully-populated GlyphAtlasEntry with all 5 fields
// at distinct non-default values.  Lift by literal so the test can read
// the constants at the assertion sites.
constexpr int    kTestXOffset       = 7;     // pen-relative bbox.x0
constexpr int    kTestYOffset       = -3;    // pen-relative bbox.y0
constexpr float  kTestAdvanceX      = 42.5f;
constexpr std::uint32_t kTestFillRgba = 0xAABBCCDDu;

} // namespace

TEST_CASE("glyph_atlas: empty cache lookup returns nullopt") {
    const std::string font_path = "/nonexistent/font/path.ttf";
    const std::uint32_t glyph_id = 0xDEADBEEFu;
    const std::uint32_t font_size = 24;

    // Force-clear first so we know state.
    chronon3d::glyph_atlas_clear();

    auto hit = chronon3d::glyph_atlas_lookup(font_path, glyph_id, font_size);
    REQUIRE_FALSE(hit.has_value());
}

TEST_CASE("glyph_atlas: lookup preserves x_offset / y_offset / advance_x / fill_color_rgba") {
    const std::string font_path = "/test/store/roundtrip.ttf";
    const std::uint32_t glyph_id = 0xC0FFEEu;
    const std::uint32_t font_size = 32;

    chronon3d::glyph_atlas_clear();

    // ── Store an entry with FULL metadata (all non-default) ─────────────
    chronon3d::GlyphAtlasEntry entry_in;
    entry_in.image            = make_test_glyph_image();
    entry_in.x_offset         = kTestXOffset;
    entry_in.y_offset         = kTestYOffset;
    entry_in.advance_x        = kTestAdvanceX;
    entry_in.fill_color_rgba  = kTestFillRgba;
    REQUIRE(entry_in.image != nullptr);
    REQUIRE(entry_in.image->width() == 4);
    REQUIRE(entry_in.image->height() == 4);

    chronon3d::glyph_atlas_store(font_path, glyph_id, font_size, entry_in);

    // ── Lookup: every metadata field MUST round-trip ────────────────
    auto hit = chronon3d::glyph_atlas_lookup(font_path, glyph_id, font_size);
    REQUIRE(hit.has_value());

    const chronon3d::GlyphAtlasEntry& entry_out = *hit;
    CHECK(entry_out.image != nullptr);
    CHECK(entry_out.image->width() == 4);
    CHECK(entry_out.image->height() == 4);

    // These four checks are the regression catchers: if the cache still
    // stored only the BLImage and reconstructed a default-init entry on
    // lookup, all four would be 0 / 0.0f / 0u.
    CHECK(entry_out.x_offset        == kTestXOffset);
    CHECK(entry_out.y_offset        == kTestYOffset);
    CHECK(entry_out.advance_x       == kTestAdvanceX);
    CHECK(entry_out.fill_color_rgba == kTestFillRgba);
}

TEST_CASE("glyph_atlas: clear resets cache, subsequent lookup misses") {
    const std::string font_path = "/test/clear/path.ttf";
    const std::uint32_t glyph_id = 0xFEEDFACEu;
    const std::uint32_t font_size = 48;

    chronon3d::GlyphAtlasEntry entry_in;
    entry_in.image            = make_test_glyph_image();
    entry_in.x_offset         = 1;
    entry_in.y_offset         = 2;
    entry_in.advance_x        = 3.0f;
    entry_in.fill_color_rgba  = 0x01020304u;

    chronon3d::glyph_atlas_store(font_path, glyph_id, font_size, entry_in);

    REQUIRE(chronon3d::glyph_atlas_lookup(font_path, glyph_id, font_size).has_value());

    chronon3d::glyph_atlas_clear();

    auto miss = chronon3d::glyph_atlas_lookup(font_path, glyph_id, font_size);
    REQUIRE_FALSE(miss.has_value());

    auto stats_after = chronon3d::glyph_atlas_stats();
    CHECK(stats_after.entry_count == 0);
    CHECK(stats_after.total_weight == 0);
}

TEST_CASE("glyph_atlas: lookup is empty after no store") {
    // Sanity: a freshly cleared cache stays empty for unknown keys.
    const std::string font_path = "/t/unfilled/path.ttf";
    const std::uint32_t glyph_id = 0x00FACADEu;
    const std::uint32_t font_size = 16;

    chronon3d::glyph_atlas_clear();
    REQUIRE_FALSE(chronon3d::glyph_atlas_lookup(font_path, glyph_id, font_size).has_value());

    auto stats_empty = chronon3d::glyph_atlas_stats();
    CHECK(stats_empty.entry_count == 0);
    CHECK(stats_empty.total_weight == 0);
}
