// DirectYUV text preparation is immutable and reusable across short-lived
// programs. This test locks the canonical layout fingerprint boundary and
// prevents a future implementation from returning to pointer-keyed caches.

#include <doctest/doctest.h>

#include "src/media/video/direct_yuv/text_texture_cache.hpp"

#include <memory>

using namespace chronon3d;
using namespace chronon3d::media::video;

TEST_CASE("DirectYUV text cache reuses immutable layout fingerprints") {
    DirectYuvTextTextureCache cache;

    TextRunLayout original;
    original.source_text = "direct-yuv-cache";
    original.font.font_path = "/fonts/Inter-Bold.ttf";

    auto texture = std::make_shared<RasterizedTextTexture>();
    texture->width = 4;
    texture->height = 4;
    texture->display_width = 4.0f;
    texture->display_height = 4.0f;
    texture->mtsdf = true;
    texture->gpu_rgba.resize(16, 0.5f);
    cache.store(original, texture);

    // A distinct layout object with identical immutable content must hit the
    // same entry. Pointer identity is not a valid residency authority.
    TextRunLayout equivalent = original;
    CHECK(cache.find(equivalent) == texture);
    CHECK(cache.stats().hits >= 1);
    CHECK(cache.size() == 1);
}

TEST_CASE("DirectYUV text cache rejects incomplete stored payloads") {
    DirectYuvTextTextureCache cache;
    TextRunLayout layout;
    layout.source_text = "incomplete";

    cache.store(layout, std::make_shared<RasterizedTextTexture>());

    CHECK(cache.size() == 0);
    CHECK_FALSE(cache.find(layout));
}
