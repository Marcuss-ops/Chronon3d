#include <doctest/doctest.h>
#include <chronon3d/assets/asset_registry.hpp>

using namespace chronon3d;

TEST_CASE("AssetRegistry: import_image returns stable id") {
    AssetRegistry reg;
    const auto id1 = reg.import_image("assets/images/checker.png");
    const auto id2 = reg.import_image("assets/images/checker.png");
    CHECK(id1 == id2);
    CHECK(reg.size() == 1);
}

TEST_CASE("AssetRegistry: different paths get different ids") {
    AssetRegistry reg;
    const auto a = reg.import_image("assets/a.png");
    const auto b = reg.import_image("assets/b.png");
    CHECK(a != b);
    CHECK(reg.size() == 2);
}

TEST_CASE("AssetRegistry: metadata type is set correctly") {
    AssetRegistry reg;
    const auto img  = reg.import_image("a.png");
    const auto font = reg.import_font("f.ttf");
    const auto vid  = reg.import_video("v.mp4");

    CHECK(reg.metadata(img).type  == AssetType::Image);
    CHECK(reg.metadata(font).type == AssetType::Font);
    CHECK(reg.metadata(vid).type  == AssetType::Video);
}

TEST_CASE("AssetRegistry: metadata color_space and alpha_mode") {
    AssetRegistry reg;
    const auto img = reg.import_image("bg.png");
    CHECK(reg.metadata(img).color_space == ColorSpace::SRGB);
    CHECK(reg.metadata(img).alpha_mode  == AlphaMode::Straight);

    const auto font = reg.import_font("f.ttf");
    CHECK(reg.metadata(font).alpha_mode == AlphaMode::None);
}

TEST_CASE("AssetRegistry: metadata path preserved") {
    AssetRegistry reg;
    const auto id = reg.import_image("assets/images/checker.png");
    CHECK(reg.metadata(id).path == std::filesystem::path("assets/images/checker.png"));
}

TEST_CASE("AssetRegistry: metadata path_hash is a 16-char hex string") {
    AssetRegistry reg;
    const auto id = reg.import_image("x.png");
    CHECK(reg.metadata(id).path_hash.size() == 16);
}

TEST_CASE("AssetRegistry: find_by_path returns id when registered") {
    AssetRegistry reg;
    const auto id = reg.import_image("some/image.png");
    const auto found = reg.find_by_path("some/image.png");
    REQUIRE(found.has_value());
    CHECK(*found == id);
}

TEST_CASE("AssetRegistry: find_by_path returns nullopt when not registered") {
    AssetRegistry reg;
    CHECK_FALSE(reg.find_by_path("missing.png").has_value());
}

TEST_CASE("AssetRegistry: contains reflects registration") {
    AssetRegistry reg;
    const auto id = reg.import_image("x.png");
    CHECK(reg.contains(id));
    CHECK_FALSE(reg.contains(id + 1));
}

TEST_CASE("AssetRegistry: legacy declare_image keeps compat") {
    AssetRegistry reg;
    reg.declare_image("background", "assets/bg.png");
    const auto found = reg.find_by_path("assets/bg.png");
    REQUIRE(found.has_value());
    CHECK(reg.metadata(*found).type == AssetType::Image);
}

TEST_CASE("AssetRegistry: get_path returns original string") {
    AssetRegistry reg;
    const auto id = reg.import_image("assets/images/checker.png");
    CHECK(reg.get_path(id) == "assets/images/checker.png");
}

TEST_CASE("AssetRegistry: metadata throws on unknown id") {
    AssetRegistry reg;
    CHECK_THROWS_AS(reg.metadata(9999), std::out_of_range);
}
