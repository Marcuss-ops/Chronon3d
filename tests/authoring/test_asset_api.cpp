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
using chronon3d::assets::MeshRef;
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
    auto mesh = asset<AssetKind::Mesh>("models/phone.glb");

    static_assert(decltype(image)::kind == AssetKind::Image);
    static_assert(decltype(font)::kind == AssetKind::Font);
    static_assert(decltype(video)::kind == AssetKind::Video);
    static_assert(decltype(audio)::kind == AssetKind::Audio);
    static_assert(decltype(mesh)::kind == AssetKind::Mesh);

    CHECK(image.path() == "images/x.png");
    CHECK(font.path() == "fonts/x.ttf");
    CHECK(video.path() == "videos/x.mp4");
    CHECK(audio.path() == "audio/x.wav");
    CHECK(mesh.path() == "models/phone.glb");
}

TEST_CASE("audit-§10: Layer::mesh delegates logical MeshRef without loading") {
    chronon3d::LayerBuilder builder("hero", chronon3d::SampleTime{});
    chronon3d::authoring::Layer layer(
        builder, chronon3d::CanvasInfo::with_safe_area(
            1920.0f, 1080.0f, chronon3d::SafeAreaPreset{}));

    auto handle = layer.mesh(MeshRef{"models/phone.glb", "hero/phone"});
    handle.position({10.0f, 20.0f, 30.0f});
    chronon3d::Layer built = builder.build();

    REQUIRE(built.nodes.size() == 1);
    CHECK(built.nodes[0].name == "mesh_0");
    CHECK(built.nodes[0].shape.type() == chronon3d::ShapeType::Mesh);
    CHECK(built.nodes[0].shape.mesh_shape().mesh == nullptr);
    auto meshes = built.asset_manifest.filter(AssetKind::Mesh);
    REQUIRE(meshes.size() == 1);
    CHECK(meshes[0].path == "models/phone.glb");
    CHECK(meshes[0].owner == "hero/phone");
    CHECK(built.nodes[0].world_transform.position.x == doctest::Approx(10.0f));
    CHECK(built.nodes[0].world_transform.position.y == doctest::Approx(20.0f));
    CHECK(built.nodes[0].world_transform.position.z == doctest::Approx(30.0f));
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
    static_assert(std::is_convertible_v<LogicalAsset, MeshRef>);

    using LayerImageSignature = chronon3d::NodeHandle (authoring::Layer::*)(
        std::string, ImageRef);
    LayerImageSignature image_function = &authoring::Layer::image;
    using LayerMeshSignature = chronon3d::NodeHandle (authoring::Layer::*)(MeshRef);
    LayerMeshSignature mesh_function = &authoring::Layer::mesh;
    static_assert(std::is_invocable_r_v<
        chronon3d::NodeHandle,
        decltype(image_function),
        authoring::Layer&,
        std::string,
        LogicalAsset>);
    static_assert(std::is_invocable_r_v<
        chronon3d::NodeHandle,
        decltype(mesh_function),
        authoring::Layer&,
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
    MeshRef mesh = asset("models/phone.glb", "hero/mesh");
    CHECK(image.path() == "images/logo.png");
    CHECK(font.path() == "fonts/Inter.ttf");
    CHECK(image.owner() == "logo");
    CHECK(font.owner() == "headline");
    CHECK(mesh.path() == "models/phone.glb");
    CHECK(mesh.owner() == "hero/mesh");
}

TEST_CASE("audit-§10: logical image path reaches canonical ImageParams") {
    ImageRef image = asset("images/logo.png");
    chronon3d::ImageParams params;
    params.asset_path = image.path();
    CHECK(params.asset_path == "images/logo.png");
}
