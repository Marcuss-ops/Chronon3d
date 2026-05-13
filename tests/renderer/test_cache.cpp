#include <doctest/doctest.h>
#include <chronon3d/renderer/assets/image_cache.hpp>
#include <chronon3d/renderer/assets/font_cache.hpp>
#include <filesystem>

using namespace chronon3d;

static const std::string kCheckerPath  = "assets/images/checker.png";
static const std::string kFontBoldPath = "assets/fonts/Inter-Bold.ttf";

static bool assets_available() {
    return std::filesystem::exists(kCheckerPath) && std::filesystem::exists(kFontBoldPath);
}

// ---------------------------------------------------------------------------
TEST_CASE("ImageCache returns null for missing file") {
    ImageCache cache;
    const auto* img = cache.get_or_load("does_not_exist_xyz.png");
    CHECK(img == nullptr);
    CHECK(cache.size() == 1); // sentinel inserted so no retry
}

TEST_CASE("ImageCache does not retry missing file") {
    ImageCache cache;
    cache.get_or_load("missing_file.png");
    cache.get_or_load("missing_file.png"); // second call
    CHECK(cache.size() == 1); // still 1 — no duplicate entry
}

TEST_CASE("ImageCache loads real image") {
    if (!assets_available()) { MESSAGE("Skipping: assets not found"); return; }
    ImageCache cache;
    const auto* img = cache.get_or_load(kCheckerPath);
    REQUIRE(img != nullptr);
    CHECK(img->width  > 0);
    CHECK(img->height > 0);
    CHECK(img->pixels != nullptr);
}

TEST_CASE("ImageCache returns same pointer on repeated access") {
    if (!assets_available()) { MESSAGE("Skipping: assets not found"); return; }
    ImageCache cache;
    const auto* a = cache.get_or_load(kCheckerPath);
    const auto* b = cache.get_or_load(kCheckerPath);
    REQUIRE(a != nullptr);
    CHECK(a == b);
}

TEST_CASE("ImageCache clear empties the cache") {
    ImageCache cache;
    cache.get_or_load("assets/images/checker.png");
    REQUIRE(cache.size() == 1);
    cache.clear();
    CHECK(cache.size() == 0);
}

// ---------------------------------------------------------------------------
TEST_CASE("FontCache returns null for missing file") {
    FontCache cache;
    const auto* f = cache.get_or_load("does_not_exist.ttf");
    CHECK(f == nullptr);
    CHECK(cache.size() == 1); // sentinel
}

TEST_CASE("FontCache does not retry missing file") {
    FontCache cache;
    cache.get_or_load("missing_font.ttf");
    cache.get_or_load("missing_font.ttf");
    CHECK(cache.size() == 1);
}

TEST_CASE("FontCache loads real font") {
    if (!assets_available()) { MESSAGE("Skipping: assets not found"); return; }
    FontCache cache;
    const auto* f = cache.get_or_load(kFontBoldPath);
    REQUIRE(f != nullptr);
    CHECK(!f->data.empty());
}

TEST_CASE("FontCache returns same pointer on repeated access") {
    if (!assets_available()) { MESSAGE("Skipping: assets not found"); return; }
    FontCache cache;
    const auto* a = cache.get_or_load(kFontBoldPath);
    const auto* b = cache.get_or_load(kFontBoldPath);
    REQUIRE(a != nullptr);
    CHECK(a == b);
}

TEST_CASE("FontCache clear empties the cache") {
    if (!assets_available()) { MESSAGE("Skipping: assets not found"); return; }
    FontCache cache;
    cache.get_or_load(kFontBoldPath);
    REQUIRE(cache.size() == 1);
    cache.clear();
    CHECK(cache.size() == 0);
}
