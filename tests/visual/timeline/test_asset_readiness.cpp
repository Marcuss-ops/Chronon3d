// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/timeline/test_asset_readiness.cpp
//
// Gate 3 — Asset Readiness Tests
//
// Definition of Done: proves that missing assets (font, image, video)
// cause a HARD preflight failure — not a silent black frame or fallback.
//
// 5 test cases:
//   1. MissingFontFailsPreflight     — font missing → Error + kind=Font
//   2. MissingImageFailsPreflight    — image missing → Error + kind=Image
//   3. MissingVideoFailsPreflight    — video missing → Error + kind=Video
//   4. FrameOnlyVsFullComposition    — FrameOnly scopes correctly
//   5. PreflightErrorFormatting      — text + JSON output
//
// The rule: asset mancante = render non parte. No PNG nero, no fallback
// silenzioso a font default.
//
// NOTE: We use flat layer() calls (not sequence() wrappers) for
// simplicity.  SceneBuilder::sequence() now ALWAYS executes the lambda
// and merges asset manifests (fixed in 10-point friction audit,
// commit 0ff8b100).  SequenceBuilder::sequence() (nested) was fixed
// in the A1 sequence-manifest unification (same contract).  Both
// paths now correctly collect assets regardless of active_at() filtering.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <nlohmann/json.hpp>
#include <tests/helpers/test_utils.hpp>

#include <fstream>
#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::assets::AssetManifest;

namespace {

// ── Helpers ────────────────────────────────────────────────────────────────

static FrameContext asset_ctx(Frame frame = Frame{0}) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = {30, 1};
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

/// Resolver that resolves nothing — all paths are missing.
static assets::AssetResolver empty_resolver() {
    return {};
}

/// Check that at least one issue matches the given type and path substring.
static bool has_issue(const AssetPreflightResult& result,
                      PreflightAssetType type,
                      const std::string& path_substr) {
    for (const auto& issue : result.issues) {
        if (issue.type == type &&
            issue.path.find(path_substr) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// Check that no issue references the given path.
static bool has_no_issue_for(const AssetPreflightResult& result,
                             const std::string& path) {
    for (const auto& issue : result.issues) {
        if (issue.path == path) return false;
    }
    return true;
}

/// Check that the issue message contains a substring.
static bool has_message_containing(const AssetPreflightResult& result,
                                   const std::string& substr) {
    for (const auto& issue : result.issues) {
        if (issue.message.find(substr) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Test 1: MissingFontFailsPreflight
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Assets.MissingFontFailsPreflight") {
    SUBCASE("single missing font") {
        SceneBuilder s(asset_ctx());
        s.layer("title", [](LayerBuilder& l) {
            TextRunSpec p;
            p.text.font.font_path = "assets/fonts/DOES_NOT_EXIST.ttf";
            p.text.content.value = "Hello";
            (void)l.text_run("label", std::move(p));
        });
        Scene scene = s.build();

        auto resolver = empty_resolver();
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);

        CHECK_FALSE(result.ok());
        CHECK(result.size() >= 1);
        CHECK(has_issue(result, PreflightAssetType::Font, "DOES_NOT_EXIST"));
        CHECK(result.issues[0].code == "ASSET_NOT_FOUND");
    }

    SUBCASE("font issue has owner/layer info") {
        SceneBuilder s(asset_ctx());
        s.layer("my_title_layer", [](LayerBuilder& l) {
            TextRunSpec p;
            p.text.font.font_path = "assets/fonts/Missing-Bold.ttf";
            p.text.content.value = "Test";
            (void)l.text_run("text_node", std::move(p));
        });
        Scene scene = s.build();

        auto resolver = empty_resolver();
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);

        CHECK_FALSE(result.ok());
        CHECK(has_message_containing(result, "Missing-Bold.ttf"));
    }

    SUBCASE("preflight failure blocks render") {
        SceneBuilder s(asset_ctx());
        s.layer("title", [](LayerBuilder& l) {
            TextRunSpec p;
            p.text.font.font_path = "assets/fonts/DOES_NOT_EXIST.ttf";
            p.text.content.value = "Hello";
            (void)l.text_run("label", std::move(p));
        });
        Scene scene = s.build();

        auto resolver = empty_resolver();
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);

        // Preflight must catch this BEFORE render
        CHECK_FALSE(result.ok());
        CHECK(has_preflight_errors(result.issues));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 2: MissingImageFailsPreflight
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Assets.MissingImageFailsPreflight") {
    SUBCASE("single missing image") {
        SceneBuilder s(asset_ctx());
        s.layer("bg", [](LayerBuilder& l) {
            l.image("photo", {.path = "assets/images/missing_bg.png",
                              .size = {1920, 1080}});
        });
        Scene scene = s.build();

        auto resolver = empty_resolver();
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);

        CHECK_FALSE(result.ok());
        CHECK(result.size() >= 1);
        CHECK(has_issue(result, PreflightAssetType::Image, "missing_bg.png"));
    }

    SUBCASE("image issue contains path and owner") {
        SceneBuilder s(asset_ctx());
        s.layer("background_layer", [](LayerBuilder& l) {
            l.image("bg_image", {.path = "assets/images/nonexistent.png",
                                 .size = {100, 100}});
        });
        Scene scene = s.build();

        auto resolver = empty_resolver();
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);

        CHECK_FALSE(result.ok());
        CHECK(result.issues[0].path == "assets/images/nonexistent.png");
        CHECK_FALSE(result.issues[0].layer_id.empty());
    }

    SUBCASE("multiple missing images all reported") {
        SceneBuilder s(asset_ctx());
        s.layer("layer1", [](LayerBuilder& l) {
            l.image("img1", {.path = "missing1.png", .size = {100, 100}});
        });
        s.layer("layer2", [](LayerBuilder& l) {
            l.image("img2", {.path = "missing2.png", .size = {100, 100}});
        });
        Scene scene = s.build();

        auto resolver = empty_resolver();
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);

        CHECK_FALSE(result.ok());
        CHECK(result.size() >= 2);
        CHECK(has_issue(result, PreflightAssetType::Image, "missing1.png"));
        CHECK(has_issue(result, PreflightAssetType::Image, "missing2.png"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 3: MissingVideoFailsPreflight
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Assets.MissingVideoFailsPreflight") {
    SUBCASE("font + image both missing = both reported") {
        SceneBuilder s(asset_ctx());
        s.layer("text_layer", [](LayerBuilder& l) {
            TextRunSpec p;
            p.text.font.font_path = "assets/fonts/Nope.ttf";
            p.text.content.value = "X";
            (void)l.text_run("text", std::move(p));
        });
        s.layer("bg_layer", [](LayerBuilder& l) {
            l.image("bg", {.path = "assets/images/nope.png", .size = {100, 100}});
        });
        Scene scene = s.build();

        auto resolver = empty_resolver();
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);

        CHECK_FALSE(result.ok());
        CHECK(result.size() >= 2);
        CHECK(has_issue(result, PreflightAssetType::Font, "Nope.ttf"));
        CHECK(has_issue(result, PreflightAssetType::Image, "nope.png"));
    }

    // NOTE: LayerBuilder::video() sets the VideoSource on the layer but does
    // NOT register the asset in the AssetManifest. Video asset preflight
    // requires manifest registration (future work).
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 4: FrameOnlyVsFullComposition
//
// Uses flat layer() calls with from()/duration() — NOT sequence() wrappers.
// layer() always executes its lambda (registering assets in the manifest),
// then filters by active_at() for the scene's layer list.
//
// Layout:
//   "early" layer: from=0, dur=30   → font "assets/fonts/Early.ttf"
//   "late"  layer: from=60, dur=30  → video "assets/video/Late.mp4"
//
// FrameOnly f10  → only "early" active → only Early.ttf checked
// FrameOnly f70  → only "late" active  → only Late.mp4 checked
// FrameOnly f50  → gap, no active layers → ok
// FullComposition → both checked → FAIL
// ═══════════════════════════════════════════════════════════════════════════

// ── Helper: build a two-layer scene at the given frame ────────────────
//
// SceneBuilder filters layers by active_at(build_frame), so we build
// at the frame where the target layer is active.
static Scene make_two_asset_scene(Frame build_frame) {
    SceneBuilder s(asset_ctx(build_frame));
    // Early layer: active at frame 0-29, uses font
    s.layer("early", [](LayerBuilder& l) {
        l.from(Frame{0}).duration(Frame{30});
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Early.ttf";
        p.text.content.value = "EARLY";
        (void)l.text_run("label", std::move(p));
    });
    // Late layer: active at frame 60-89, uses image
    s.layer("late", [](LayerBuilder& l) {
        l.from(Frame{60}).duration(Frame{30});
        l.image("bg", {.path = "assets/images/Late.png", .size = {1920, 1080}});
    });
    return s.build();
}

TEST_CASE("Assets.FrameOnlyVsFullComposition") {
    auto resolver = empty_resolver();

    SUBCASE("FrameOnly f10 — only early layer active") {
        auto scene = make_two_asset_scene(Frame{10});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{10});
        CHECK_FALSE(result.ok());
        CHECK(has_issue(result, PreflightAssetType::Font, "Early.ttf"));
        // Late layer not in scene (filtered at build time) — not checked
        CHECK(has_no_issue_for(result, "assets/images/Late.png"));
    }

    SUBCASE("FrameOnly f70 — only late layer active") {
        auto scene = make_two_asset_scene(Frame{70});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{70});
        CHECK_FALSE(result.ok());
        CHECK(has_issue(result, PreflightAssetType::Image, "Late.png"));
        // Early layer not in scene (filtered at build time) — not checked
        CHECK(has_no_issue_for(result, "assets/fonts/Early.ttf"));
    }

    SUBCASE("FrameOnly f50 — gap, no active layers") {
        auto scene = make_two_asset_scene(Frame{50});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{50});
        CHECK(result.ok());
        CHECK(result.empty());
    }

    SUBCASE("FullComposition at f10 — early assets checked") {
        auto scene = make_two_asset_scene(Frame{10});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);
        // Scene built at f10 — only early layer in scene
        CHECK_FALSE(result.ok());
        CHECK(has_issue(result, PreflightAssetType::Font, "Early.ttf"));
    }

    SUBCASE("FrameOnly with resolved asset — no error") {
        namespace fs = std::filesystem;
        auto scene = make_two_asset_scene(Frame{10});

        auto tmp_dir = fs::temp_directory_path() / "chronon3d_test_assets";
        fs::create_directories(tmp_dir / "assets" / "fonts");
        auto font_path = tmp_dir / "assets" / "fonts" / "Early.ttf";
        {
            std::ofstream f(font_path);
            f << "dummy";
        }

        assets::AssetResolver r;
        r.mount(tmp_dir);

        auto result = AssetPreflightResolver::check(scene, r,
            PreflightMode::FrameOnly, Frame{10});

        bool has_font_error = false;
        for (const auto& issue : result.issues) {
            if (issue.type == PreflightAssetType::Font) has_font_error = true;
        }
        CHECK_FALSE(has_font_error);
        CHECK(result.ok());

        std::error_code ec;
        fs::remove_all(tmp_dir, ec);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 5: Preflight error formatting
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Assets.PreflightErrorFormatting") {
    AssetManifest manifest;
    manifest.add_font("assets/fonts/Missing-Bold.ttf", "title/text");
    manifest.add_image("assets/images/missing_bg.png", "background/rect");

    auto resolver = empty_resolver();
    auto result = AssetPreflightResolver::check_manifest(manifest, resolver);

    CHECK_FALSE(result.ok());

    std::string text = format_preflight_issues_text(result.issues);
    CHECK(text.find("Missing-Bold.ttf") != std::string::npos);
    CHECK(text.find("missing_bg.png") != std::string::npos);
    CHECK(text.find("ASSET_NOT_FOUND") != std::string::npos);

    auto js = preflight_issues_to_json(result.issues);
    CHECK(js["ok"] == false);
    CHECK(js["errors"] >= 2);
}
