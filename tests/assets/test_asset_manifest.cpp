// ── tests/assets/test_asset_manifest.cpp
//
// Tests for Sequence V2: AssetRef + AssetManifest.
//
// Coverage:
//   - AssetManifest: add, filter, merge, empty, size
//   - AssetRef: fields, typed convenience methods
//   - Manifest collection: text_run → font, image → image, video → video
//   - Propagation: Layer manifest → Scene manifest
//   - Multiple layers: manifests aggregate correctly

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// AssetManifest: basic operations
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AssetManifest — empty by default") {
    AssetManifest m;
    CHECK(m.empty());
    CHECK(m.size() == 0);
    CHECK(m.assets().empty());
}

TEST_CASE("AssetManifest — add_font") {
    AssetManifest m;
    m.add_font("assets/fonts/Inter-Bold.ttf", "title/text");
    CHECK(m.size() == 1);
    CHECK(m.assets()[0].kind == assets::AssetKind::Font);
    CHECK(m.assets()[0].path == "assets/fonts/Inter-Bold.ttf");
    CHECK(m.assets()[0].owner == "title/text");
    CHECK(m.assets()[0].required == true);
}

TEST_CASE("AssetManifest — add_image") {
    AssetManifest m;
    m.add_image("assets/bg.png", "background/rect", false);
    CHECK(m.size() == 1);
    CHECK(m.assets()[0].kind == assets::AssetKind::Image);
    CHECK(m.assets()[0].path == "assets/bg.png");
    CHECK(m.assets()[0].required == false);
}

TEST_CASE("AssetManifest — add_video") {
    AssetManifest m;
    m.add_video("assets/intro.mp4", "intro/video");
    CHECK(m.size() == 1);
    CHECK(m.assets()[0].kind == assets::AssetKind::Video);
}

TEST_CASE("AssetManifest — add_audio") {
    AssetManifest m;
    m.add_audio("assets/music.mp3", "bgm/audio");
    CHECK(m.size() == 1);
    CHECK(m.assets()[0].kind == assets::AssetKind::Audio);
}

TEST_CASE("AssetManifest — filter by type") {
    AssetManifest m;
    m.add_font("a.ttf", "x");
    m.add_image("b.png", "y");
    m.add_font("c.ttf", "z");
    m.add_video("d.mp4", "w");

    auto fonts = m.filter(assets::AssetKind::Font);
    CHECK(fonts.size() == 2);
    CHECK(fonts[0].path == "a.ttf");
    CHECK(fonts[1].path == "c.ttf");

    auto images = m.filter(assets::AssetKind::Image);
    CHECK(images.size() == 1);

    auto videos = m.filter(assets::AssetKind::Video);
    CHECK(videos.size() == 1);

    auto audio = m.filter(assets::AssetKind::Audio);
    CHECK(audio.empty());
}

TEST_CASE("AssetManifest — merge") {
    AssetManifest a;
    a.add_font("a.ttf", "x");

    AssetManifest b;
    b.add_image("b.png", "y");
    b.add_video("c.mp4", "z");

    a.merge(b);
    CHECK(a.size() == 3);
    CHECK(a.assets()[1].kind == assets::AssetKind::Image);
    CHECK(a.assets()[2].kind == assets::AssetKind::Video);
}

TEST_CASE("AssetManifest — clear") {
    AssetManifest m;
    m.add_font("a.ttf", "x");
    CHECK(m.size() == 1);
    m.clear();
    CHECK(m.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Manifest collection from LayerBuilder
// ═══════════════════════════════════════════════════════════════════════════

static FrameContext manifest_ctx() {
    FrameContext ctx;
    ctx.frame = Frame{0};
    ctx.frame_rate = {30, 1};
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

TEST_CASE("AssetManifest — text_run collects font asset") {
    SceneBuilder s(manifest_ctx());
    s.layer("title", [](LayerBuilder& l) {
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Inter-Bold.ttf";
        p.text.content.value = "Hello";
        (void)l.text_run("label", std::move(p));
    });

    Scene scene = s.build();
    const auto& manifest = scene.asset_manifest();
    REQUIRE(manifest.size() >= 1);

    auto fonts = manifest.filter(assets::AssetKind::Font);
    REQUIRE(fonts.size() >= 1);
    bool found = false;
    for (const auto& f : fonts) {
        if (f.path == "assets/fonts/Inter-Bold.ttf") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("AssetManifest — image collects image asset") {
    SceneBuilder s(manifest_ctx());
    s.layer("bg", [](LayerBuilder& l) {
        l.image("photo", {.path = "assets/bg.png", .size = {100, 100}});
    });

    Scene scene = s.build();
    const auto& manifest = scene.asset_manifest();
    auto images = manifest.filter(assets::AssetKind::Image);
    REQUIRE(images.size() >= 1);
    bool found = false;
    for (const auto& img : images) {
        if (img.path == "assets/bg.png") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("AssetManifest — empty path not collected") {
    SceneBuilder s(manifest_ctx());
    s.layer("bg", [](LayerBuilder& l) {
        l.image("photo", {.path = "", .size = {100, 100}});
    });

    Scene scene = s.build();
    auto images = scene.asset_manifest().filter(assets::AssetKind::Image);
    CHECK(images.empty());
}

TEST_CASE("AssetManifest — multiple layers aggregate") {
    SceneBuilder s(manifest_ctx());
    s.layer("title", [](LayerBuilder& l) {
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Inter-Bold.ttf";
        p.text.content.value = "Hello";
        (void)l.text_run("label", std::move(p));
    });
    s.layer("bg", [](LayerBuilder& l) {
        l.image("photo", {.path = "assets/bg.png", .size = {100, 100}});
    });

    Scene scene = s.build();
    const auto& manifest = scene.asset_manifest();
    CHECK(manifest.filter(assets::AssetKind::Font).size() >= 1);
    CHECK(manifest.filter(assets::AssetKind::Image).size() >= 1);
}

TEST_CASE("AssetManifest — sequence layers propagate to scene") {
    FrameContext ctx = manifest_ctx();
    SceneBuilder s(ctx);
    s.sequence("intro", {.from = Frame{0}, .duration = Frame{30}},
        [](SceneBuilder& s) {
            s.layer("title", [](LayerBuilder& l) {
                TextRunSpec p;
                p.text.font.font_path = "assets/fonts/Poppins-Bold.ttf";
                p.text.content.value = "INTRO";
                (void)l.text_run("label", std::move(p));
            });
        });

    Scene scene = s.build();
    auto fonts = scene.asset_manifest().filter(assets::AssetKind::Font);
    bool found = false;
    for (const auto& f : fonts) {
        if (f.path == "assets/fonts/Poppins-Bold.ttf") {
            found = true;
            break;
        }
    }
    CHECK(found);
}
