// ═══════════════════════════════════════════════════════════════════════════
// tests/authoring/test_asset_api.cpp
//
// Audit §10 — process-wide asset root ripout + thin authoring API.
// Validates the `authoring::asset(...)` family + `Layer::image(name,
// AssetRef)` / `Text::font(FontRef, f32)` overloads.
//
// Coverage:
//   TC1: `asset("path")` carries path / owner / required metadata.
//   TC2: `asset<assets::AssetKind::Font>("...")` returns a typed FontRef.
//   TC3: ImageRef round-trips through the canonical ImageParams::asset_path.
//   TC4: Text::font(FontRef, size) keeps its typed bridge contract.
//   TC5: the same unqualified `asset("...")` marker is accepted by both the
//        image and font authoring overloads through contextual conversion.
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

TEST_CASE("audit-§10: asset(\"path\") carries logical path metadata") {
    auto ref = asset("images/logo.png");
    static_assert(decltype(ref)::kind == AssetKind::Image,
                  "asset() compatibility marker must remain Image");
    CHECK(ref.path() == "images/logo.png");
    CHECK(ref.owner() == "");
    CHECK(ref.required() == true);

    SUBCASE("owner argument forwarded") {
        auto ref2 = asset("images/hero.png", "scrollable/hero");
        CHECK(ref2.path() == "images/hero.png");
        CHECK(ref2.owner() == "scrollable/hero");
    }

    SUBCASE("owner argument forwarded for explicit Font kind") {
        auto f = asset<AssetKind::Font>("fonts/Inter-Bold.ttf", "title/font");
        CHECK(f.path() == "fonts/Inter-Bold.ttf");
        CHECK(f.owner() == "title/font");
        static_assert(decltype(f)::kind == AssetKind::Font,
                      "asset<Font> K must be Font");
    }
}

TEST_CASE("audit-§10: asset<K> dispatches compile-time AssetKind") {
    auto img = asset<AssetKind::Image>("images/x.png");
    auto fnt = asset<AssetKind::Font>("fonts/x.ttf");
    auto vid = asset<AssetKind::Video>("videos/x.mp4");
    auto aud = asset<AssetKind::Audio>("audio/x.wav");

    static_assert(decltype(img)::kind == AssetKind::Image, "K=Image");
    static_assert(decltype(fnt)::kind == AssetKind::Font, "K=Font");
    static_assert(decltype(vid)::kind == AssetKind::Video, "K=Video");
    static_assert(decltype(aud)::kind == AssetKind::Audio, "K=Audio");

    CHECK(img.path() == "images/x.png");
    CHECK(fnt.path() == "fonts/x.ttf");
    CHECK(vid.path() == "videos/x.mp4");
    CHECK(aud.path() == "audio/x.wav");
}

TEST_CASE("audit-§10: Text::font(FontRef, size) keeps typed bridge") {
    namespace authoring = chronon3d::authoring;

    auto fref = FontRef{"fonts/Inter-Bold.ttf", "title", /*required=*/true};
    CHECK(fref.path() == "fonts/Inter-Bold.ttf");
    CHECK(fref.required() == true);

    using AuthoringTextFontSig =
        authoring::Text& (authoring::Text::*)(
            chronon3d::assets::FontRef, chronon3d::f32);
    AuthoringTextFontSig fp = &authoring::Text::font;
    static_assert(
        std::is_invocable_r_v<
            authoring::Text&,
            decltype(fp),
            authoring::Text&,
            FontRef,
            chronon3d::f32>,
        "Text::font(FontRef, f32) must be invocable returning Text&");
}

TEST_CASE("audit-§10: ImageRef maps to canonical ImageParams asset_path") {
    ImageRef ref = asset("images/logo.png");

    chronon3d::ImageParams p;
    p.asset_path = ref.path();
    CHECK(p.asset_path == "images/logo.png");
}

TEST_CASE("audit-§10: asset(path) is context-typed for image and font authoring") {
    namespace authoring = chronon3d::authoring;

    using LogicalAsset = decltype(asset("logical/path"));
    static_assert(std::is_convertible_v<LogicalAsset, ImageRef>,
                  "logical asset must convert to ImageRef");
    static_assert(std::is_convertible_v<LogicalAsset, FontRef>,
                  "logical asset must convert to FontRef");

    using LayerImageSig = chronon3d::NodeHandle (authoring::Layer::*)(
        std::string, ImageRef);
    LayerImageSig image_fn = &authoring::Layer::image;
    static_assert(
        std::is_invocable_r_v<
            chronon3d::NodeHandle,
            decltype(image_fn),
            authoring::Layer&,
            std::string,
            LogicalAsset>,
        "layer.image(name, asset(path)) must compile");

    using TextFontSig = authoring::Text& (authoring::Text::*)(
        FontRef, chronon3d::f32);
    TextFontSig font_fn = &authoring::Text::font;
    static_assert(
        std::is_invocable_r_v<
            authoring::Text&,
            decltype(font_fn),
            authoring::Text&,
            LogicalAsset,
            chronon3d::f32>,
        "text.font(asset(path), size) must compile");

    ImageRef image = asset("images/logo.png", "logo");
    FontRef font = asset("fonts/Inter.ttf", "headline");
    CHECK(image.path() == "images/logo.png");
    CHECK(font.path() == "fonts/Inter.ttf");
    CHECK(image.owner() == "logo");
    CHECK(font.owner() == "headline");
}
