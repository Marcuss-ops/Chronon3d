// ── tests/core/timeline/test_sequence_integration.cpp
//
// Integration tests for Sequence V2: end-to-end pipeline verification.
//
// Coverage (genuinely NEW — no overlap with test_timeline_resolver,
// test_sequence_animation, or test_asset_preflight_resolver):
//   1. Mixed active/inactive sequences — only active layers produce output
//   2. Scene manifest aggregation across sequence boundaries
//   3. Preflight FrameOnly with nested sequences — inner sequence asset
//      skipped when outer sequence is inactive
//
// These tests exercise cross-cutting concerns that span multiple
// subsystems (TimelineResolver + SceneBuilder + AssetManifest +
// AssetPreflightResolver) in ways not covered by the unit tests.

#include <doctest/doctest.h>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

using namespace chronon3d;

// ── Helpers ───────────────────────────────────────────────────────────

static FrameContext seq_ctx(Frame frame) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = {30, 1};
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

// Unique name avoids unity-build collision with test_asset_preflight_resolver.cpp
namespace {
assets::AssetResolver make_no_mount_resolver() {
    assets::AssetResolver resolver;
    return resolver;
}
} // anon

// ═══════════════════════════════════════════════════════════════════════════
// 1. Mixed active/inactive sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence integration — mixed active/inactive: only active layers render") {
    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(seq_ctx(f));
        s.sequence("early", {.from = Frame{0}, .duration = Frame{30}},
            [](SceneBuilder& s) {
                s.layer("a", [](LayerBuilder& l) {
                    l.rect("r", {.size = {100, 100}, .color = Color::white()});
                });
            });
        s.sequence("late", {.from = Frame{60}, .duration = Frame{30}},
            [](SceneBuilder& s) {
                s.layer("b", [](LayerBuilder& l) {
                    l.rect("r", {.size = {100, 100}, .color = Color::white()});
                });
            });
        return s.build();
    };

    SUBCASE("frame 10: only early active") {
        Scene scene = build_at(Frame{10});
        CHECK(scene.layers().size() == 1);
    }

    SUBCASE("frame 45: gap — no active sequences") {
        Scene scene = build_at(Frame{45});
        CHECK(scene.layers().size() == 0);
    }

    SUBCASE("frame 70: only late active") {
        Scene scene = build_at(Frame{70});
        CHECK(scene.layers().size() == 1);
    }

    SUBCASE("frame 0: early at boundary start") {
        Scene scene = build_at(Frame{0});
        CHECK(scene.layers().size() == 1);
    }

    SUBCASE("frame 30: early ended, late not started") {
        Scene scene = build_at(Frame{30});
        CHECK(scene.layers().size() == 0);
    }

    SUBCASE("frame 90: late ended") {
        Scene scene = build_at(Frame{90});
        CHECK(scene.layers().size() == 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Scene manifest aggregation across sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence integration — manifest aggregates across sequence boundaries") {
    SceneBuilder s(seq_ctx(Frame{0}));

    s.sequence("intro", {.from = Frame{0}, .duration = Frame{30}},
        [](SceneBuilder& s) {
            s.layer("title", [](LayerBuilder& l) {
                l.image("photo", {.path = "assets/images/intro.png", .size = {100, 100}});
            });
        });

    s.sequence("body", {.from = Frame{30}, .duration = Frame{60}},
        [](SceneBuilder& s) {
            s.layer("content", [](LayerBuilder& l) {
                l.image("photo", {.path = "assets/images/body.png", .size = {100, 100}});
            });
        });

    Scene scene = s.build();
    const auto& manifest = scene.asset_manifest();

    // Both sequences' assets should be in the scene manifest
    auto images = manifest.filter(assets::AssetKind::Image);
    CHECK(images.size() >= 2);

    bool found_intro = false;
    bool found_body = false;
    for (const auto& i : images) {
        if (i.path == "assets/images/intro.png") found_intro = true;
        if (i.path == "assets/images/body.png") found_body = true;
    }
    CHECK(found_intro);
    CHECK(found_body);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Preflight FrameOnly with nested sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence integration — preflight FrameOnly with nested sequences") {
    // Outer sequence from=0..300, inner sequence from=100..200 (inside outer).
    // The inner sequence has an image asset. SceneBuilder evaluates layers at
    // the build frame, so we must build at a frame where the inner sequence is
    // active for its asset to appear in the manifest.

    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(seq_ctx(f));
        s.sequence("outer", {.from = Frame{0}, .duration = Frame{300}},
            [](SequenceBuilder& outer) {
                outer.sequence("inner", {.from = Frame{100}, .duration = Frame{100}},
                    [](SequenceBuilder& inner) {
                        inner.layer("title", [](LayerBuilder& l) {
                            l.image("photo", {.path = "assets/images/nested.png", .size = {100, 100}});
                        });
                    });
            });
        return s.build();
    };

    auto resolver = make_no_mount_resolver();

    SUBCASE("FullComposition: always detects nested asset") {
        // Build at frame 150 so the inner sequence is active and its asset is
        // added to the manifest.
        Scene scene = build_at(Frame{150});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FullComposition);
        CHECK_FALSE(result.ok());
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.path == "assets/images/nested.png") found = true;
        }
        CHECK(found);
    }

    SUBCASE("FrameOnly at frame 150: outer+inner active, asset checked") {
        Scene scene = build_at(Frame{150});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{150});
        CHECK_FALSE(result.ok());
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.path == "assets/images/nested.png") found = true;
        }
        CHECK(found);
    }

    SUBCASE("FrameOnly at frame 50: outer active but inner not yet, asset skipped") {
        // At frame 50, outer is active (0..300) but inner (100..200) is not.
        // The layer inside inner is not produced, so its manifest is not included.
        Scene scene = build_at(Frame{50});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{50});
        // No active layers with assets → ok
        CHECK(result.ok());
    }

    SUBCASE("FrameOnly at frame 400: outer inactive, everything skipped") {
        Scene scene = build_at(Frame{400});
        auto result = AssetPreflightResolver::check(scene, resolver,
            PreflightMode::FrameOnly, Frame{400});
        CHECK(result.ok());
    }
}
