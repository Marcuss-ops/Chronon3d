// ── tests/assets/test_asset_preflight_resolver.cpp
//
// Tests for Sequence V2: AssetPreflightResolver.
//
// Coverage:
//   - FullComposition mode: checks all assets in scene manifest
//   - FrameOnly mode: checks only assets from active layers
//   - Missing asset produces PreflightIssue with Error severity
//   - Empty manifest returns ok
//   - Multiple asset types (font, image)

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
using namespace chronon3d;
using chronon3d::assets::AssetManifest;

// ── Helper: build a FrameContext ──────────────────────────────────────
static FrameContext preflight_ctx(Frame frame) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = {30, 1};
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

// ── Helper: create a resolver that resolves nothing (all paths missing)
static assets::AssetResolver make_empty_resolver() {
    assets::AssetResolver resolver;
    // No mounts — everything will fail to resolve
    return resolver;
}

// ═══════════════════════════════════════════════════════════════════════════
// FullComposition mode
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AssetPreflightResolver — empty manifest: ok") {
    SceneBuilder s(preflight_ctx(Frame{0}));
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("r", {.size = {100, 100}, .color = Color::white()});
    });
    Scene scene = s.build();

    auto resolver = make_empty_resolver();
    auto result = AssetPreflightResolver::check(scene, resolver,
        PreflightMode::FullComposition);
    CHECK(result.ok());
    CHECK(result.empty());
}

TEST_CASE("AssetPreflightResolver — missing font: error") {
    SceneBuilder s(preflight_ctx(Frame{0}));
    s.layer("title", [](LayerBuilder& l) {
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Nonexistent.ttf";
        p.text.content.value = "Hello";
        (void)l.text_run("label", std::move(p));
    });
    Scene scene = s.build();

    auto resolver = make_empty_resolver();
    auto result = AssetPreflightResolver::check(scene, resolver,
        PreflightMode::FullComposition);
    CHECK_FALSE(result.ok());
    CHECK(result.size() >= 1);
    CHECK(result.issues[0].type == PreflightAssetType::Font);
    CHECK(result.issues[0].code == "ASSET_NOT_FOUND");
}

TEST_CASE("AssetPreflightResolver — missing image: error") {
    SceneBuilder s(preflight_ctx(Frame{0}));
    s.layer("bg", [](LayerBuilder& l) {
        l.image("photo", {.path = "assets/missing.png", .size = {100, 100}});
    });
    Scene scene = s.build();

    auto resolver = make_empty_resolver();
    auto result = AssetPreflightResolver::check(scene, resolver,
        PreflightMode::FullComposition);
    CHECK_FALSE(result.ok());
    CHECK(result.issues[0].type == PreflightAssetType::Image);
}

TEST_CASE("AssetPreflightResolver — multiple missing assets") {
    SceneBuilder s(preflight_ctx(Frame{0}));
    s.layer("title", [](LayerBuilder& l) {
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Nope.ttf";
        p.text.content.value = "Hello";
        (void)l.text_run("label", std::move(p));
    });
    s.layer("bg", [](LayerBuilder& l) {
        l.image("photo", {.path = "assets/nope.png", .size = {100, 100}});
    });
    Scene scene = s.build();

    auto resolver = make_empty_resolver();
    auto result = AssetPreflightResolver::check(scene, resolver,
        PreflightMode::FullComposition);
    CHECK_FALSE(result.ok());
    CHECK(result.size() >= 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// FrameOnly mode
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AssetPreflightResolver — FrameOnly: skips inactive layers") {
    SceneBuilder s(preflight_ctx(Frame{0}));
    // Layer active at frame 0-30 with font
    s.layer("early", [](LayerBuilder& l) {
        l.from(Frame{0}).duration(Frame{30});
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Early.ttf";
        p.text.content.value = "Early";
        (void)l.text_run("label", std::move(p));
    });
    // Layer active at frame 60-90 with font
    s.layer("late", [](LayerBuilder& l) {
        l.from(Frame{60}).duration(Frame{30});
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Late.ttf";
        p.text.content.value = "Late";
        (void)l.text_run("label", std::move(p));
    });
    Scene scene = s.build();

    auto resolver = make_empty_resolver();

    SUBCASE("frame 0: only early layer checked") {
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{0});
        // Only early layer is active, so only its font is checked
        CHECK_FALSE(result.ok());
        CHECK(result.size() >= 1);
        // Should only have the early font issue
        bool found_early = false;
        bool found_late = false;
        for (const auto& issue : result.issues) {
            if (issue.path == "assets/fonts/Early.ttf") found_early = true;
            if (issue.path == "assets/fonts/Late.ttf") found_late = true;
        }
        CHECK(found_early);
        CHECK_FALSE(found_late);
    }

    SUBCASE("frame 70: only late layer checked") {
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{70});
        bool found_early = false;
        bool found_late = false;
        for (const auto& issue : result.issues) {
            if (issue.path == "assets/fonts/Early.ttf") found_early = true;
            if (issue.path == "assets/fonts/Late.ttf") found_late = true;
        }
        CHECK_FALSE(found_early);
        CHECK(found_late);
    }

    SUBCASE("frame 40: no active layers, ok") {
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{40});
        CHECK(result.ok());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// check_manifest convenience
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AssetPreflightResolver — check_manifest: missing asset") {
    AssetManifest manifest;
    manifest.add_font("assets/fonts/Missing.ttf", "test/layer");

    auto resolver = make_empty_resolver();
    auto result = AssetPreflightResolver::check_manifest(manifest, resolver);
    CHECK_FALSE(result.ok());
    CHECK(result.issues[0].type == PreflightAssetType::Font);
    CHECK(result.issues[0].path == "assets/fonts/Missing.ttf");
}

TEST_CASE("AssetPreflightResolver — check_manifest: empty manifest ok") {
    AssetManifest manifest;
    auto resolver = make_empty_resolver();
    auto result = AssetPreflightResolver::check_manifest(manifest, resolver);
    CHECK(result.ok());
}
