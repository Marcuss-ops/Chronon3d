// ═══════════════════════════════════════════════════════════════════════════
// tests/authoring/test_asset_api.cpp
//
// Audit §10 — logical authoring paths + per-runtime resolution contract.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/authoring/asset.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/assets/asset_ref.hpp>

#include <string>
#include <type_traits>

using chronon3d::assets::AssetKind;
using chronon3d::assets::FontRef;
using chronon3d::assets::ImageRef;
using chronon3d::authoring::asset;

namespace {

template <typename T>
concept HasIntrinsicAssetKind = requires { T::kind; };

} // namespace

TEST_CASE("audit-§10: asset(path) carries only logical path metadata") {
    auto logical = asset("images/logo.png");
    CHECK(logical.path() == "images/logo.png");
    CHECK(logical.owner().empty());
    CHECK(logical.required());

    SUBCASE("owner is forwarded without resolution") {
        auto owned = asset("images/hero.png", "scrollable/hero");
        CHECK(owned.path() == "images/hero.png");
        CHECK(owned.owner() == "scrollable/hero");
    }
}

TEST_CASE("audit-§10: explicit asset<K> remains concretely typed") {
    auto image = asset<AssetKind::Image>("images/x.png");
    auto font = asset<AssetKind::Font>("fonts/x.ttf");
    auto video = asset<AssetKind::Video>("videos/x.mp4");
    auto audio = asset<AssetKind::Audio>("audio/x.wav");

    static_assert(decltype(image)::kind == AssetKind::Image);
    static_assert(decltype(font)::kind == AssetKind::Font);
    static_assert(decltype(video)::kind == AssetKind::Video);
    static_assert(decltype(audio)::kind == AssetKind::Audio);

    CHECK(image.path() == "images/x.png");
    CHECK(font.path() == "fonts/x.ttf");
    CHECK(video.path() == "videos/x.mp4");
    CHECK(audio.path() == "audio/x.wav");
}

TEST_CASE("audit-§10: Text::font keeps the typed FontRef bridge") {
    namespace authoring = chronon3d::authoring;

    using TextFontSignature = authoring::Text& (authoring::Text::*)(
        FontRef, chronon3d::f32);
    TextFontSignature font_function = &authoring::Text::font;

    static_assert(std::is_invocable_r_v<
        authoring::Text&,
        decltype(font_function),
        authoring::Text&,
        FontRef,
        chronon3d::f32>);
}

TEST_CASE("audit-§10: logical asset converts to the consumer-requested kind") {
    namespace authoring = chronon3d::authoring;

    using LogicalAsset = decltype(asset("logical/path"));
    static_assert(!HasIntrinsicAssetKind<LogicalAsset>,
                  "logical asset paths must not pretend to have an intrinsic kind");
    static_assert(std::is_convertible_v<LogicalAsset, ImageRef>);
    static_assert(std::is_convertible_v<LogicalAsset, FontRef>);

    using LayerImageSignature = chronon3d::NodeHandle (authoring::Layer::*)(
        std::string, ImageRef);
    LayerImageSignature image_function = &authoring::Layer::image;
    static_assert(std::is_invocable_r_v<
        chronon3d::NodeHandle,
        decltype(image_function),
        authoring::Layer&,
        std::string,
        LogicalAsset>);

    using TextFontSignature = authoring::Text& (authoring::Text::*)(
        FontRef, chronon3d::f32);
    TextFontSignature font_function = &authoring::Text::font;
    static_assert(std::is_invocable_r_v<
        authoring::Text&,
        decltype(font_function),
        authoring::Text&,
        LogicalAsset,
        chronon3d::f32>);

    ImageRef image = asset("images/logo.png", "logo");
    FontRef font = asset("fonts/Inter.ttf", "headline");
    CHECK(image.path() == "images/logo.png");
    CHECK(font.path() == "fonts/Inter.ttf");
    CHECK(image.owner() == "logo");
    CHECK(font.owner() == "headline");
}

TEST_CASE("audit-§10: logical image path reaches canonical ImageParams") {
    ImageRef image = asset("images/logo.png");
    chronon3d::ImageParams params;
    params.asset_path = image.path();
    CHECK(params.asset_path == "images/logo.png");
}
