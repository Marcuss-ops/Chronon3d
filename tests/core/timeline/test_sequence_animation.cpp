// ── tests/core/timeline/test_sequence_animation.cpp
//
// Tests for Sequence V2: animations inside sequences use local frame.
//
// Verifies that:
//   - AnimatedTransform inside a sequence evaluates at local frame 0
//   - Opacity animation starts from local frame 0 (not global)
//   - Position animation uses local frame
//   - Nested sequence animation uses innermost local frame
//   - Backward compatibility: animations outside sequences use global frame
//
// Key mechanism: SceneBuilder::sequence() sets local_ctx.frame = local,
// so LayerBuilder::build() evaluates anim_transform at the local frame.

#include <doctest/doctest.h>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
using namespace chronon3d;

// ── Helper: build a FrameContext at a given frame ─────────────────────
static FrameContext anim_ctx(Frame frame) {
    FrameContext ctx;
    ctx = ctx.with_frame(frame);
    ctx = ctx.with_frame_rate({30, 1});
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

// ═══════════════════════════════════════════════════════════════════════════
// Opacity animation inside sequence
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence anim — opacity starts from local frame 0") {
    // Sequence from=100, duration=60
    // Layer has opacity animation: key(0, 0.0f) → key(30, 1.0f)
    // At global frame 100 (local 0): opacity = 0
    // At global frame 115 (local 15): opacity = 0.5
    // At global frame 130 (local 30): opacity = 1.0

    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(anim_ctx(f));
        s.sequence("test", {.from = Frame{100}, .duration = Frame{60}},
            [](SceneBuilder& s) {
                s.layer("animated", [](LayerBuilder& l) {
                    l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    l.opacity_anim()
                        .key(Frame{0}, 0.0f)
                        .key(Frame{30}, 1.0f);
                });
            });
        return s.build();
    };

    SUBCASE("at sequence start (local 0): opacity = 0") {
        Scene scene = build_at(Frame{100});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.0f));
    }

    SUBCASE("midway (local 15): opacity = 0.5") {
        Scene scene = build_at(Frame{115});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    }

    SUBCASE("at local 30: opacity = 1.0") {
        Scene scene = build_at(Frame{130});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(1.0f));
    }

    SUBCASE("before sequence (global 50): no layers") {
        Scene scene = build_at(Frame{50});
        CHECK(scene.layers().size() == 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Position animation inside sequence
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence anim — position uses local frame") {
    // Sequence from=50, duration=40
    // Position animation: key(0, {0,0,0}) → key(20, {100,0,0})
    // At global frame 50 (local 0): x = 0
    // At global frame 60 (local 10): x = 50
    // At global frame 70 (local 20): x = 100

    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(anim_ctx(f));
        s.sequence("move", {.from = Frame{50}, .duration = Frame{40}},
            [](SceneBuilder& s) {
                s.layer("box", [](LayerBuilder& l) {
                    l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    l.position_anim()
                        .key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f})
                        .key(Frame{20}, Vec3{100.0f, 0.0f, 0.0f});
                });
            });
        return s.build();
    };

    SUBCASE("at sequence start (local 0): x = 0") {
        Scene scene = build_at(Frame{50});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.position.x == doctest::Approx(0.0f));
    }

    SUBCASE("midway (local 10): x = 50") {
        Scene scene = build_at(Frame{60});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.position.x == doctest::Approx(50.0f));
    }

    SUBCASE("at local 20: x = 100") {
        Scene scene = build_at(Frame{70});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.position.x == doctest::Approx(100.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Nested sequence animation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence anim — nested sequence uses innermost local frame") {
    // project from=0, duration=300
    //   chapter from=100, duration=200
    //     layer with opacity: key(0, 0.0f) → key(30, 1.0f)
    //
    // At global frame 100: project.local=100, chapter.local=0 → opacity=0
    // At global frame 115: project.local=115, chapter.local=15 → opacity=0.5
    // At global frame 130: project.local=130, chapter.local=30 → opacity=1.0

    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(anim_ctx(f));
        s.sequence("project", {.from = Frame{0}, .duration = Frame{300}},
            [](SequenceBuilder& project) {
                project.sequence("chapter", {.from = Frame{100}, .duration = Frame{200}},
                    [](SequenceBuilder& chapter) {
                        chapter.layer("fading", [](LayerBuilder& l) {
                            l.rect("r", {.size = {100, 100}, .color = Color::white()});
                            l.opacity_anim()
                                .key(Frame{0}, 0.0f)
                                .key(Frame{30}, 1.0f);
                        });
                    });
            });
        return s.build();
    };

    SUBCASE("global 100 → chapter.local=0 → opacity=0") {
        Scene scene = build_at(Frame{100});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.0f));
    }

    SUBCASE("global 115 → chapter.local=15 → opacity=0.5") {
        Scene scene = build_at(Frame{115});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    }

    SUBCASE("global 130 → chapter.local=30 → opacity=1.0") {
        Scene scene = build_at(Frame{130});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(1.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Backward compat: animation outside sequence uses global frame
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence anim — backward compat: no sequence uses global frame") {
    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(anim_ctx(f));
        s.layer("direct", [](LayerBuilder& l) {
            l.rect("r", {.size = {100, 100}, .color = Color::white()});
            l.opacity_anim()
                .key(Frame{0}, 0.0f)
                .key(Frame{30}, 1.0f);
        });
        return s.build();
    };

    SUBCASE("frame 0: opacity = 0") {
        Scene scene = build_at(Frame{0});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.0f));
    }

    SUBCASE("frame 15: opacity = 0.5") {
        Scene scene = build_at(Frame{15});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    }

    SUBCASE("frame 30: opacity = 1.0") {
        Scene scene = build_at(Frame{30});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(1.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Scale animation inside sequence
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence anim — scale uses local frame") {
    // Sequence from=200, duration=60
    // Scale: key(0, {1,1,1}) → key(30, {2,2,2})

    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(anim_ctx(f));
        s.sequence("zoom", {.from = Frame{200}, .duration = Frame{60}},
            [](SceneBuilder& s) {
                s.layer("box", [](LayerBuilder& l) {
                    l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    l.scale_anim()
                        .key(Frame{0}, Vec3{1.0f, 1.0f, 1.0f})
                        .key(Frame{30}, Vec3{2.0f, 2.0f, 2.0f});
                });
            });
        return s.build();
    };

    SUBCASE("at sequence start (local 0): scale = 1") {
        Scene scene = build_at(Frame{200});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.scale.x == doctest::Approx(1.0f));
    }

    SUBCASE("midway (local 15): scale = 1.5") {
        Scene scene = build_at(Frame{215});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.scale.x == doctest::Approx(1.5f));
    }

    SUBCASE("at local 30: scale = 2") {
        Scene scene = build_at(Frame{230});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.scale.x == doctest::Approx(2.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Animation with SequenceBuilder facade
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Sequence anim — SequenceBuilder: animation uses local frame") {
    auto build_at = [](Frame f) -> Scene {
        SceneBuilder s(anim_ctx(f));
        s.sequence("intro", {.from = Frame{0}, .duration = Frame{60}},
            [](SequenceBuilder& seq) {
                seq.layer("fade_in", [](LayerBuilder& l) {
                    l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    l.opacity_anim()
                        .key(Frame{0}, 0.0f)
                        .key(Frame{30}, 1.0f);
                });
            });
        return s.build();
    };

    SUBCASE("frame 0 (local 0): opacity = 0") {
        Scene scene = build_at(Frame{0});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.0f));
    }

    SUBCASE("frame 15 (local 15): opacity = 0.5") {
        Scene scene = build_at(Frame{15});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    }

    SUBCASE("frame 45 (local 45): opacity = 1.0 (past keyframe, held)") {
        Scene scene = build_at(Frame{45});
        REQUIRE(scene.layers().size() == 1);
        CHECK(scene.layers()[0].transform.opacity == doctest::Approx(1.0f));
    }
}
