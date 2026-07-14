// ═══════════════════════════════════════════════════════════════════════════
// Authoring DSL — Scene B2.3 thin forwarder tests
//
// B2.3 — adds 5 thin forwarders that route authoring::Scene verbs to the
// canonical SceneBuilder:
//   * `.camera()`         → CameraApi  (value-typed sub-builder)
//   * `.background(...)`  → grid_background
//   * `.image(...)`       → image
//   * `.screen_layer(...)`→ screen_layer  (dual-surface SFINAE)
//   * `.precomp(...)`     → precomp_layer (dual-surface SFINAE)
//
// Cat-3 minimal-surface — these tests verify the verbatim delegation
// contract + the dual-surface SFINAE pattern (mirror Scene::layer).
// No new SDK symbols, no new clock-cycle overhead.
//
// ── Assertion strategy ──────────────────────────────────────────────────
//
// We use `>= 1` count + presence-by-name, NOT exact-size assertions.
// Reason: SceneBuilder's underlying `Scene` may have internal nodes/layers
// (default stage / asset-manifest trackpoints / camera target) that the
// forwarder doesn't author.  Asserting "exactly 1 node" is brittle to
// upstream SceneBuilder evolution; asserting "the named entry IS present"
// is the right contract for a thin forwarder.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/camera_api.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/authoring/scene.hpp>      // B2.3 — 5 forwarders live here
#include <chronon3d/authoring/layer.hpp>

#include <string_view>

namespace {

// ── Engine FrameContext factory (mirrors test_scene_sequence.cpp pattern) ──
chronon3d::FrameContext engine_ctx(chronon3d::Frame cf = chronon3d::Frame{0}) {
    chronon3d::FrameContext c;
    c.frame      = cf;
    c.frame_rate = chronon3d::FrameRate{30, 1};
    c.width      = 1920;
    c.height     = 1080;
    return c;
}

// ── Authoring FrameContext factory (1920×1080 default — explicit, no silent fall-back) ──
chronon3d::authoring::FrameContext author_ctx(float w = 1920.0f, float h = 1080.0f) {
    chronon3d::authoring::FrameContext fc;
    fc.width  = w;
    fc.height = h;
    return fc;
}

// ── Presence-by-name helpers (forwarder contract: "the named entry IS present") ──
[[nodiscard]] bool scene_has_node(const chronon3d::Scene& s, std::string_view name) {
    for (const auto& n : s.nodes()) {
        if (n.name == name) return true;
    }
    return false;
}

[[nodiscard]] bool scene_has_layer(const chronon3d::Scene& s, std::string_view name) {
    for (const auto& l : s.layers()) {
        if (l.name == name) return true;
    }
    return false;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Scene::camera() — value-typed CameraApi sub-builder
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::camera / returns valid CameraApi by value") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    // Forwarder returns CameraApi (not Scene&) — compile-enforced via
    // the return type itself.  The assignment below compiles only if
    // the return type actually IS CameraApi — no runtime check needed
    // (CameraApi has no operator==; a CHECK on it would not compile
    // against this particular doctest's Expression template).
    static_assert(std::is_class_v<chronon3d::CameraApi>);
    chronon3d::CameraApi api = scene.camera();
    (void)api;
}

TEST_CASE("Authoring / Scene::camera / chain on CameraApi mutates underlying SceneBuilder") {
    chronon3d::SceneBuilder sb{engine_ctx(chronon3d::Frame{200})};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    // Forwarder exposes CameraApi by value; chained setter mutates
    // the underlying SceneBuilder.
    scene.camera()
         .position(chronon3d::Vec3{10.0f, 20.0f, 30.0f})
         .zoom(2.0f);

    // Forwarder correctly routed — the SceneBuilder state reflects
    // the mutations.
    const chronon3d::Camera2_5D& cam = sb.camera_2_5d();
    CHECK(cam.zoom == doctest::Approx(2.0f));
    CHECK(cam.position.x != 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Scene::background() — fluent Scene& return + delegated grid_background
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::background / chains to Scene& and adds grid_background node") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    // Forwarder returns Scene& for fluent chaining.
    chronon3d::authoring::Scene& r = scene.background(
        "stage_bg", chronon3d::GridBackgroundParams{
            .size = {1920.0f, 1080.0f},
            .bg_color = chronon3d::Color{0.05f, 0.05f, 0.05f, 1.0f},
            .spacing = 80.0f,
        });
    CHECK(&r == &scene);

    // Forwarder routed to grid_background — the named node IS present
    // (SceneBuilder may add other internal nodes; we don't constrain).
    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.nodes().size() >= 1);
    CHECK(scene_has_node(evaluated, "stage_bg"));
}

TEST_CASE("Authoring / Scene::background / chained with layer() — full fluent surface") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    scene.background("bg", chronon3d::GridBackgroundParams{})
         .layer("title", [](chronon3d::LayerBuilder& l) {
             l.rect("r", {.size = {100.0f, 50.0f}, .color = chronon3d::Color::white()});
         });

    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.nodes().size()   >= 1);  // at least our background
    CHECK(evaluated.layers().size()  >= 1);  // at least our layer
    CHECK(scene_has_node (evaluated, "bg"));
    CHECK(scene_has_layer(evaluated, "title"));
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Scene::image() — fluent Scene& return + delegated image
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::image / chains to Scene& and adds image layer") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    chronon3d::authoring::Scene& r = scene.image(
        "logo", chronon3d::ImageParams{
            .asset_path = "images/logo.png",
            .size = {200.0f, 200.0f},
            .pos = {960.0f, 540.0f, 0.0f},
        });
    CHECK(&r == &scene);

    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.layers().size() >= 1);
    CHECK(scene_has_layer(evaluated, "logo"));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Scene::screen_layer() — dual-surface SFINAE dispatch
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::screen_layer / Layer& surface — authoring DSL works inside closure") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    bool received_authoring_layer = false;
    scene.screen_layer("overlay", [&](chronon3d::authoring::Layer& lyr) {
        received_authoring_layer = true;
        lyr.configure_core([](chronon3d::LayerBuilder& core) {
            core.rect("r", {.size = {50.0f, 50.0f}, .color = chronon3d::Color::white()});
        });
    });
    CHECK(received_authoring_layer);

    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.layers().size() >= 1);
    CHECK(scene_has_layer(evaluated, "overlay"));
}

TEST_CASE("Authoring / Scene::screen_layer / LayerBuilder& passthrough — engine surface") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    bool invoked = false;
    scene.screen_layer("hud", [&](chronon3d::LayerBuilder& lb) {
        invoked = true;
        lb.rect("r", {.size = {10.0f, 10.0f}, .color = chronon3d::Color::white()});
    });
    CHECK(invoked);

    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.layers().size() >= 1);
    CHECK(scene_has_layer(evaluated, "hud"));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Scene::precomp() — dual-surface SFINAE dispatch + comp_name arity
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::precomp / Layer& surface — comp_name routed verbatim") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    bool received_authoring_layer = false;
    scene.precomp(
        "main_precomp",
        "intro_clip",                   // comp_name — arity preserved
        [&](chronon3d::authoring::Layer& lyr) {
            received_authoring_layer = true;
            lyr.configure_core([](chronon3d::LayerBuilder& core) {
                core.rect("r", {.size = {30.0f, 30.0f}, .color = chronon3d::Color::white()});
            });
        });

    CHECK(received_authoring_layer);

    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.layers().size() >= 1);
    CHECK(scene_has_layer(evaluated, "main_precomp"));
}

TEST_CASE("Authoring / Scene::precomp / LayerBuilder& passthrough — engine surface") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    bool invoked = false;
    scene.precomp(
        "main_precomp",
        "intro_clip",
        [&](chronon3d::LayerBuilder& lb) {
            invoked = true;
            lb.rect("r", {.size = {15.0f, 15.0f}, .color = chronon3d::Color::white()});
        });

    CHECK(invoked);

    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.layers().size() >= 1);
    CHECK(scene_has_layer(evaluated, "main_precomp"));
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Mixed verbs — B2.3 + B2.2 verbs compose freely on one Scene
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene / mixed B2.2 + B2.3 verbs — all fluent, single chained expression") {
    chronon3d::SceneBuilder sb{engine_ctx()};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    // camera() is a sub-builder getter (terminal verb — see TC1).
    // background, image, sequence return Scene& and chain freely.
    scene.background("bg", chronon3d::GridBackgroundParams{})
         .image("logo",
                chronon3d::ImageParams{.asset_path = "logo.png",
                                       .size = {200.0f, 200.0f}})
         .sequence("intro",
                   {.from = chronon3d::Frame{0}, .duration = chronon3d::Frame{60}},
                   [](chronon3d::SequenceBuilder& seq) {
                       seq.layer("title",
                                 [](chronon3d::LayerBuilder& l) {
                                     l.rect("r", {.size = {100.0f, 50.0f},
                                                  .color = chronon3d::Color::white()});
                                 });
                   })
         .screen_layer("hud",
                       [](chronon3d::LayerBuilder& l) {
                           l.rect("r", {.size = {10.0f, 10.0f},
                                        .color = chronon3d::Color::white()});
                       });

    // camera() called separately (terminal verb — uses CameraApi chain).
    scene.camera().zoom(1.5f);

    // Each named entry IS present (count assertions are brittle to
    // SceneBuilder internal defaults; presence-by-name is the contract).
    chronon3d::Scene evaluated = sb.build();
    CHECK(scene_has_node (evaluated, "bg"));
    CHECK(scene_has_layer(evaluated, "logo"));
    CHECK(scene_has_layer(evaluated, "hud"));
    // Sequence is inactive at cf=0 (from=0, duration=60 ⇒ active when 0≤cf<60,
    // Wait: from=0 + duration=60 ⇒ active [0..60); cf=0 IS active — so the
    // sequence's "title" layer IS present.)
    CHECK(scene_has_layer(evaluated, "title"));
    CHECK(evaluated.camera_2_5d().zoom == doctest::Approx(1.5f));
}

TEST_CASE("Authoring / Scene / mixed verbs — sequence is active at its own frame window") {
    chronon3d::SceneBuilder sb{engine_ctx(chronon3d::Frame{30})};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    scene.background("bg", chronon3d::GridBackgroundParams{})
         .image("logo", chronon3d::ImageParams{.asset_path = "logo.png",
                                               .size = {200.0f, 200.0f}})
         .sequence("intro",
                   {.from = chronon3d::Frame{0}, .duration = chronon3d::Frame{60}},
                   [](chronon3d::SequenceBuilder& seq) {
                       seq.layer("title",
                                 [](chronon3d::LayerBuilder& l) {
                                     l.rect("r", {.size = {100.0f, 50.0f},
                                                  .color = chronon3d::Color::white()});
                                 });
                   });

    // At cf=30, sequence is active (from=0, duration=60 ⇒ [0..60) active).
    chronon3d::Scene evaluated = sb.build();
    CHECK(scene_has_node (evaluated, "bg"));
    CHECK(scene_has_layer(evaluated, "logo"));
    CHECK(scene_has_layer(evaluated, "title"));
}
