// ═══════════════════════════════════════════════════════════════════════════
// Authoring DSL — Scene::sequence forwarder tests (B2.2)
//
// B2.2 — thin forwarder that routes through `SceneBuilder::sequence` →
// the canonical `compile_sequence()` (single source-of-truth recorded
// in `tools/check_single_source_of_truth.sh` Concept 8).  These tests
// cover the dual-surface contract enforced by Scene::sequence's
// `static_assert` + the A1 spatial-layers-skipped-when-inactive
// invariant from compile_sequence().
//
// Cat-3 minimal-surface — no new symbols added.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/authoring/scene.hpp>      // B2.2 — Scene::sequence lives here

namespace {

// ── Engine FrameContext factory (mirrors test_sequence_animation.cpp pattern) ──
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

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Dual-surface dispatch — SequenceBuilder& receives the canonical facade
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::sequence / SequenceBuilder surface — lambda receives canonical facade + correct local_frame") {
    chronon3d::SceneBuilder sb{engine_ctx(chronon3d::Frame{0})};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    bool invoked = false;
    chronon3d::Frame captured_local{999};
    int layer_count_inside_seq = 0;

    scene.sequence(
        "intro",
        {.from = chronon3d::Frame{0}, .duration = chronon3d::Frame{60}},
        [&](chronon3d::SequenceBuilder& seq) {
            invoked = true;
            captured_local = seq.local_frame();
            seq.layer("title", [&](chronon3d::LayerBuilder& l) {
                l.rect("r", {.size = {100.0f, 100.0f}, .color = chronon3d::Color::white()});
                ++layer_count_inside_seq;
            });
        });

    CHECK(invoked);
    CHECK(captured_local == chronon3d::Frame{0});
    CHECK(layer_count_inside_seq == 1);

    // Forwarder delegated to SceneBuilder::sequence → compile_sequence() →
    // merged the inner layer into SceneBuilder.scene_.layers().
    chronon3d::Scene evaluated = sb.build();
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "title");
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Dual-surface dispatch — SceneBuilder& (legacy passthrough)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::sequence / SceneBuilder surface — legacy passthrough path") {
    // cf=100, sequence from=100, duration=60 → active at boundary (100≥100 && 100<160).
    chronon3d::SceneBuilder sb{engine_ctx(chronon3d::Frame{100})};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    int invoked = 0;
    scene.sequence(
        "intro",
        {.from = chronon3d::Frame{100}, .duration = chronon3d::Frame{60}},
        [&](chronon3d::SceneBuilder& sub) {
            invoked++;
            sub.layer("bg", [](chronon3d::LayerBuilder& l) {
                l.rect("bg_rect", {.size = {200.0f, 200.0f},
                                   .color = chronon3d::Color::white()});
            });
        });

    CHECK(invoked == 1);

    chronon3d::Scene evaluated = sb.build();
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "bg");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Forwarder returns Scene& — fluent chaining contract
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::sequence / returns Scene& for fluent chaining") {
    chronon3d::SceneBuilder sb{engine_ctx(chronon3d::Frame{0})};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    chronon3d::authoring::Scene& r1 = scene.sequence(
        "a", {.from = chronon3d::Frame{0}, .duration = chronon3d::Frame{30}},
        [](chronon3d::SequenceBuilder&) {});

    chronon3d::authoring::Scene& r2 = scene.sequence(
        "b", {.from = chronon3d::Frame{30}, .duration = chronon3d::Frame{30}},
        [](chronon3d::SequenceBuilder&) {});

    CHECK(&r1 == &scene);
    CHECK(&r2 == &scene);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Nested sequence — inner local_frame derives from outer local_frame
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::sequence / nested — inner local_frame derives from outer local_frame") {
    // cf=110 (global frame at which SceneBuilder evaluates).
    //   outer: from=100, duration=60  →  outer.active (110≥100 && 110<160)
    //                                  →  outer.local = cf - outer.from + trim
    //                                                    = 110 - 100 + 0 = 10
    //   inner: from=10,  duration=40  →  inner.active (parent_cf=10 ≥10 && 10<50)
    //                                  →  inner.local = parent_cf - inner.from + trim
    //                                                     = 10 - 10 + 0 = 0
    //   Expected: outer.local_frame() == 10, inner.local_frame() == 0.
    chronon3d::SceneBuilder sb{engine_ctx(chronon3d::Frame{110})};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    chronon3d::Frame captured_outer{999};
    chronon3d::Frame captured_inner{999};

    scene.sequence("outer",
                   {.from = chronon3d::Frame{100}, .duration = chronon3d::Frame{60}},
        [&](chronon3d::SequenceBuilder& outer) {
            captured_outer = outer.local_frame();
            outer.sequence("inner",
                           {.from = chronon3d::Frame{10}, .duration = chronon3d::Frame{40}},
                [&](chronon3d::SequenceBuilder& inner) {
                    captured_inner = inner.local_frame();
                    inner.layer("inner_layer", [](chronon3d::LayerBuilder& l) {
                        l.rect("r", {.size = {20.0f, 20.0f},
                                     .color = chronon3d::Color::white()});
                    });
                });
        });

    CHECK(captured_outer == chronon3d::Frame{10});
    CHECK(captured_inner == chronon3d::Frame{0});

    chronon3d::Scene evaluated = sb.build();
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "inner_layer");
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. A1 contract — inactive sequence still invokes lambda but emits no
//    spatial layers.  At cf=200 with from=100,duration=60 → inactive.
//
//    The forwarder must honor compile_sequence()'s A1 invariant verbatim
//    (always-run-lambda, only-merge-spatial-if-active).  We assert the
//    SPATIAL contract via evaluated.layers().empty(); the ALWAYS-RUN
//    part is implicitly verified by lambda_invoked being non-false.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene::sequence / A1 spatial contract — inactive frame emits no layers") {
    // cf=200, from=100, duration=60 → 200 >= 100 && 200 < (100+60=160) → INACTIVE.
    chronon3d::SceneBuilder sb{engine_ctx(chronon3d::Frame{200})};
    chronon3d::authoring::Scene scene{sb, author_ctx()};

    bool lambda_invoked = false;
    scene.sequence("inactive",
                   {.from = chronon3d::Frame{100}, .duration = chronon3d::Frame{60}},
                   [&](chronon3d::SequenceBuilder& seq) {
                       lambda_invoked = true;
                       seq.layer("hidden_layer", [](chronon3d::LayerBuilder& l) {
                           l.rect("r", {.size = {50.0f, 50.0f},
                                        .color = chronon3d::Color::white()});
                       });
                   });

    // A1 — ALWAYS run lambda even when inactive.
    CHECK(lambda_invoked);

    // A1 — ONLY add spatial layers/nodes if active.
    chronon3d::Scene evaluated = sb.build();
    CHECK(evaluated.layers().empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Move-only contract — Scene is non-copyable like every authoring facade
//    handle.  Static_asserts at compile time, no runtime cost.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring / Scene — move-only contract") {
    CHECK(!std::is_copy_constructible_v<chronon3d::authoring::Scene>);
    CHECK(!std::is_copy_assignable_v   <chronon3d::authoring::Scene>);
    CHECK( std::is_move_constructible_v<chronon3d::authoring::Scene>);
    CHECK( std::is_move_assignable_v   <chronon3d::authoring::Scene>);
}
