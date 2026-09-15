// ==============================================================================
// tests/text/test_mtsdf_font_identity.cpp
//
// Prepared-font identity contract (P0-MTSDF).
//
// The canonical MTSDF preparation authority owns two caches: a glyph LRU and a
// bounded FreeType face LRU. Both used to be keyed on the FONT PATH, so a
// daemon that saw `/assets/foo.ttf` change bytes mid-life kept answering with
// glyphs and faces built from the previous bytes.
//
// The identity is now the canonical prepared-font CONTENT digest
// (text::prepared_font_content_digest), which is resolved once per
// (path, size, mtime) stamp and shared with the DirectYUV packed-texture
// lowering. These cases pin:
//   * the digest is the SHA-256 of the actual font bytes;
//   * it is stable for one file version and changes when the bytes change;
//   * it is resolved through the canonical AssetResolver (fails closed outside
//     the mounted root);
//   * a glyph prepared from the old bytes can never be served after the file at
//     the same path has different bytes.
// ==============================================================================

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/text/mtsdf_preparation.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr const char* kFixtureFont = "tests/fixtures/Inter-Bold.ttf";

class ScratchRoot {
public:
    ScratchRoot() {
        const auto base = std::filesystem::temp_directory_path();
        const auto stamp =
            std::chrono::system_clock::now().time_since_epoch().count();
        root = base / ("chronon3d_test_mtsdf_identity_" +
                       std::to_string(stamp));
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
    }

    ~ScratchRoot() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    [[nodiscard]] std::filesystem::path root;
};

ScratchRoot& scratch() {
    static ScratchRoot instance;
    return instance;
}

void write_relative(const std::filesystem::path& rel, std::string_view bytes) {
    const auto full = scratch().root / rel;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::vector<char> read_absolute(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>(std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>());
}

std::uint32_t glyph_index_of(char character) {
    FT_Library library = nullptr;
    REQUIRE(FT_Init_FreeType(&library) == 0);
    FT_Face face = nullptr;
    REQUIRE(FT_New_Face(library, kFixtureFont, 0, &face) == 0);
    const auto index = FT_Get_Char_Index(face, static_cast<FT_ULong>(character));
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return static_cast<std::uint32_t>(index);
}

} // namespace

TEST_CASE("prepared font identity is the SHA-256 of the font bytes") {
    write_relative("fonts/identity.ttf", "font-bytes-v1");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(scratch().root);

    const auto digest =
        chronon3d::text::prepared_font_content_digest(resolver, "fonts/identity.ttf");
    REQUIRE(digest.has_value());
    CHECK(*digest == chronon3d::assets::sha256_string("font-bytes-v1"));
}

TEST_CASE("prepared font identity is stable for one file version") {
    write_relative("fonts/stable.ttf", "font-bytes-stable");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(scratch().root);

    const auto first =
        chronon3d::text::prepared_font_content_digest(resolver, "fonts/stable.ttf");
    REQUIRE(first.has_value());
    // Resolving the same version repeatedly (as the per-glyph lookup does) must
    // return the identical identity — and must not re-read the file, which is
    // what makes a per-glyph lookup affordable at all.
    for (int repeat = 0; repeat < 8; ++repeat) {
        const auto again =
            chronon3d::text::prepared_font_content_digest(resolver, "fonts/stable.ttf");
        REQUIRE(again.has_value());
        CHECK(*again == *first);
    }
}

TEST_CASE("prepared font identity changes when the bytes at the same path change") {
    write_relative("fonts/mutable.ttf", "font-bytes-v1");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(scratch().root);

    const auto before =
        chronon3d::text::prepared_font_content_digest(resolver, "fonts/mutable.ttf");
    REQUIRE(before.has_value());

    // Different length so the (size, mtime) stamp differs even on a filesystem
    // with coarse timestamp granularity.
    write_relative("fonts/mutable.ttf", "font-bytes-v2-different-length");

    const auto after =
        chronon3d::text::prepared_font_content_digest(resolver, "fonts/mutable.ttf");
    REQUIRE(after.has_value());
    CHECK_FALSE(*after == *before);
    CHECK(*after == chronon3d::assets::sha256_string("font-bytes-v2-different-length"));
}

TEST_CASE("prepared font identity fails closed instead of inventing an identity") {
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(scratch().root);

    CHECK_FALSE(
        chronon3d::text::prepared_font_content_digest(resolver, "").has_value());
    // Escaping the mounted root has no identity at all.
    CHECK_FALSE(
        chronon3d::text::prepared_font_content_digest(resolver, "../escape.ttf")
            .has_value());

    chronon3d::assets::AssetResolver unmounted;
    CHECK_FALSE(
        chronon3d::text::prepared_font_content_digest(unmounted, "fonts/any.ttf")
            .has_value());
}

TEST_CASE("a glyph prepared from the old bytes is never served after the font changes") {
    const auto fixture = read_absolute(kFixtureFont);
    REQUIRE_FALSE(fixture.empty());
    const std::string_view font_bytes(fixture.data(), fixture.size());
    write_relative("fonts/live.ttf", font_bytes);

    chronon3d::assets::AssetResolver resolver;
    resolver.mount(scratch().root);

    const auto glyph_index = glyph_index_of('H');
    REQUIRE(glyph_index != 0);

    const auto identity_v1 =
        chronon3d::text::prepared_font_content_digest(resolver, "fonts/live.ttf");
    REQUIRE(identity_v1.has_value());

    const auto prepared = chronon3d::text::prepare_mtsdf_glyph(
        resolver, "fonts/live.ttf", glyph_index,
        chronon3d::text::kCanonicalMtsdfProfile);
    REQUIRE(prepared != nullptr);
    CHECK_FALSE(prepared->rgba8.empty());

    // Same path, different bytes, different size: a path-keyed cache would
    // return the glyph above. It must not.
    write_relative("fonts/live.ttf", "this-is-not-a-font-and-is-longer-than-v1");

    const auto identity_v2 =
        chronon3d::text::prepared_font_content_digest(resolver, "fonts/live.ttf");
    REQUIRE(identity_v2.has_value());
    CHECK_FALSE(*identity_v2 == *identity_v1);

    CHECK(chronon3d::text::prepare_mtsdf_glyph(
              resolver, "fonts/live.ttf", glyph_index,
              chronon3d::text::kCanonicalMtsdfProfile) == nullptr);
}
