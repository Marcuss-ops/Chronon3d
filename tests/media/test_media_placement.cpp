#include <doctest/doctest.h>
#include <chronon3d/media/media_placement.hpp>
#include <cmath>

using namespace chronon3d;

TEST_CASE("compute_media_placement tests") {
    Vec2 source_size{200.0f, 100.0f}; // Aspect ratio 2:1
    Vec2 box_size{100.0f, 100.0f};   // Aspect ratio 1:1

    SUBCASE("FitMode::Stretch") {
        auto res = compute_media_placement(source_size, box_size, FitMode::Stretch);
        CHECK(res.src_rect.origin.x == 0.0f);
        CHECK(res.src_rect.origin.y == 0.0f);
        CHECK(res.src_rect.size.x == 200.0f);
        CHECK(res.src_rect.size.y == 100.0f);
        CHECK(res.dst_rect.origin.x == 0.0f);
        CHECK(res.dst_rect.origin.y == 0.0f);
        CHECK(res.dst_rect.size.x == 100.0f);
        CHECK(res.dst_rect.size.y == 100.0f);
    }

    SUBCASE("FitMode::None") {
        auto res = compute_media_placement(source_size, box_size, FitMode::None);
        CHECK(res.src_rect.size.x == 200.0f);
        CHECK(res.src_rect.size.y == 100.0f);
        CHECK(res.dst_rect.size.x == 200.0f);
        CHECK(res.dst_rect.size.y == 100.0f);
        CHECK(res.dst_rect.origin.x == -50.0f); // Centered
        CHECK(res.dst_rect.origin.y == 0.0f);
    }

    SUBCASE("FitMode::Contain") {
        auto res = compute_media_placement(source_size, box_size, FitMode::Contain);
        // Scaled to fit box. Scale factor = min(100/200, 100/100) = 0.5
        // dst size = 200*0.5, 100*0.5 = 100, 50
        CHECK(res.dst_rect.size.x == 100.0f);
        CHECK(res.dst_rect.size.y == 50.0f);
        CHECK(res.dst_rect.origin.x == 0.0f);
        CHECK(res.dst_rect.origin.y == 25.0f); // Center-aligned vertically
        CHECK(res.src_rect.size.x == 200.0f);
        CHECK(res.src_rect.size.y == 100.0f);
    }

    SUBCASE("FitMode::Cover with different focal points") {
        // Scale factor = max(100/200, 100/100) = 1.0
        // src crop size = box_size / 1.0 = 100, 100
        
        {
            // Focal point center
            auto res = compute_media_placement(source_size, box_size, FitMode::Cover, {0.5f, 0.5f});
            CHECK(res.dst_rect.size.x == 100.0f);
            CHECK(res.dst_rect.size.y == 100.0f);
            CHECK(res.src_rect.size.x == 100.0f);
            CHECK(res.src_rect.size.y == 100.0f);
            CHECK(res.src_rect.origin.x == 50.0f); // Center cropped: (200 - 100) * 0.5
            CHECK(res.src_rect.origin.y == 0.0f);
        }

        {
            // Focal point left
            auto res = compute_media_placement(source_size, box_size, FitMode::Cover, {0.0f, 0.5f});
            CHECK(res.src_rect.origin.x == 0.0f);
        }

        {
            // Focal point right
            auto res = compute_media_placement(source_size, box_size, FitMode::Cover, {1.0f, 0.5f});
            CHECK(res.src_rect.origin.x == 100.0f);
        }
    }
}
