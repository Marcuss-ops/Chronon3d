// ── tests/core/timeline/test_sequence_builder.cpp
//
// Tests for Sequence V2: SequenceBuilder facade with nested sequence support.
//
// Coverage:
//   - SequenceBuilder as lambda argument to sequence()
//   - Nested sequence: local_frame propagation
//   - 3-level nesting: project → chapter → title
//   - trim_before in nested context
//   - Backward compatibility: SceneBuilder& lambda still works
//   - SequenceBuilder context accessors (local_frame, progress, duration)
//   - Overlapping sibling sequences in SequenceBuilder
//   - Nested sequence with trim_before

#include <doctest/doctest.h>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
using namespace chronon3d;

// ── Helper: build a FrameContext at a given frame ─────────────────────
static FrameContext make_seq_ctx(Frame frame, i32 w = 1920, i32 h = 1080) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = {30, 1};
    ctx.width = w;
    ctx.height = h;
    return ctx;
}

// ═══════════════════════════════════════════════════════════════════════════
// SequenceBuilder as lambda argument
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceBuilder — basic: layer inside sequence") {
    FrameContext ctx = make_seq_ctx(Frame{10});
    SceneBuilder s(ctx);

    s.sequence("intro", {.from = Frame{0}, .duration = Frame{30}},
        [](SequenceBuilder& seq) {
            CHECK(seq.local_frame() == Frame{10});
            CHECK(seq.duration() == Frame{30});
            CHECK(seq.progress() == doctest::Approx(10.0f / 30.0f));
            seq.layer("bg", [](LayerBuilder& l) {
                l.rect("r", {.size = {100, 100}, .color = Color::white()});
            });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
    CHECK(std::string(scene.layers()[0].name.c_str()) == "bg");
}

TEST_CASE("SequenceBuilder — inactive sequence: no layers produced") {
    FrameContext ctx = make_seq_ctx(Frame{50});
    SceneBuilder s(ctx);

    s.sequence("intro", {.from = Frame{0}, .duration = Frame{30}},
        [](SequenceBuilder& seq) {
            seq.layer("bg", [](LayerBuilder& l) {
                l.rect("r", {.size = {100, 100}, .color = Color::white()});
            });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Nested sequence with SequenceBuilder
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceBuilder — nested: chapter contains title") {
    // Global frame 120, outer from=100 dur=200, inner from=0 dur=30
    // chapter.local = 120 - 100 = 20, title.local = 20 - 0 = 20
    FrameContext ctx = make_seq_ctx(Frame{120});
    SceneBuilder s(ctx);

    s.sequence("chapter", {.from = Frame{100}, .duration = Frame{200}},
        [](SequenceBuilder& chapter) {
            CHECK(chapter.local_frame() == Frame{20});

            chapter.sequence("title", {.from = Frame{0}, .duration = Frame{30}},
                [](SequenceBuilder& title) {
                    CHECK(title.local_frame() == Frame{20});
                    title.layer("title_layer", [](LayerBuilder& l) {
                        l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    });
                });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
    // Layer from = 0 (inner) + 0 (inner spec.from) + 100 (outer spec.from) = 100
    CHECK(scene.layers()[0].from == Frame{100});
}

TEST_CASE("SequenceBuilder — nested: inner sequence at local frame 0") {
    // Global frame 100, outer from=100 dur=200, inner from=0 dur=30
    // chapter.local = 0, title.local = 0
    FrameContext ctx = make_seq_ctx(Frame{100});
    SceneBuilder s(ctx);

    s.sequence("chapter", {.from = Frame{100}, .duration = Frame{200}},
        [](SequenceBuilder& chapter) {
            CHECK(chapter.local_frame() == Frame{0});
            CHECK(chapter.progress() == doctest::Approx(0.0f));

            chapter.sequence("title", {.from = Frame{0}, .duration = Frame{30}},
                [](SequenceBuilder& title) {
                    CHECK(title.local_frame() == Frame{0});
                    CHECK(title.progress() == doctest::Approx(0.0f));
                    title.layer("title_layer", [](LayerBuilder& l) {
                        l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    });
                });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
}

TEST_CASE("SequenceBuilder — nested: inner sequence inactive") {
    // Global frame 130, outer from=100 dur=200, inner from=0 dur=30
    // chapter.local = 30, title.local = 30 → 30 >= 0+30 → inactive
    FrameContext ctx = make_seq_ctx(Frame{130});
    SceneBuilder s(ctx);

    s.sequence("chapter", {.from = Frame{100}, .duration = Frame{200}},
        [](SequenceBuilder& chapter) {
            CHECK(chapter.local_frame() == Frame{30});

            chapter.sequence("title", {.from = Frame{0}, .duration = Frame{30}},
                [](SequenceBuilder& title) {
                    // Should NOT be called — title is inactive at local frame 30
                    title.layer("title_layer", [](LayerBuilder& l) {
                        l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    });
                });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3-level nesting
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceBuilder — 3-level nesting: project → chapter → title") {
    // Global frame 120
    // project from=0 dur=300 → local=120
    //   chapter from=100 dur=200 → local=120-100=20
    //     title from=0 dur=30 → local=20-0=20
    FrameContext ctx = make_seq_ctx(Frame{120});
    SceneBuilder s(ctx);

    s.sequence("project", {.from = Frame{0}, .duration = Frame{300}},
        [](SequenceBuilder& project) {
            CHECK(project.local_frame() == Frame{120});

            project.sequence("chapter", {.from = Frame{100}, .duration = Frame{200}},
                [](SequenceBuilder& chapter) {
                    CHECK(chapter.local_frame() == Frame{20});

                    chapter.sequence("title", {.from = Frame{0}, .duration = Frame{30}},
                        [](SequenceBuilder& title) {
                            CHECK(title.local_frame() == Frame{20});
                            title.layer("title_layer", [](LayerBuilder& l) {
                                l.rect("r", {.size = {100, 100}, .color = Color::white()});
                            });
                        });
                });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
    // Layer from = 0 + 0 + 100 + 0 = 100
    CHECK(scene.layers()[0].from == Frame{100});
}

TEST_CASE("SequenceBuilder — 3-level nesting: inner at frame 0") {
    // Global frame 100
    // project from=0 dur=300 → local=100
    //   chapter from=100 dur=200 → local=0
    //     title from=0 dur=30 → local=0
    FrameContext ctx = make_seq_ctx(Frame{100});
    SceneBuilder s(ctx);

    s.sequence("project", {.from = Frame{0}, .duration = Frame{300}},
        [](SequenceBuilder& project) {
            CHECK(project.local_frame() == Frame{100});

            project.sequence("chapter", {.from = Frame{100}, .duration = Frame{200}},
                [](SequenceBuilder& chapter) {
                    CHECK(chapter.local_frame() == Frame{0});

                    chapter.sequence("title", {.from = Frame{0}, .duration = Frame{30}},
                        [](SequenceBuilder& title) {
                            CHECK(title.local_frame() == Frame{0});
                            title.layer("title_layer", [](LayerBuilder& l) {
                                l.rect("r", {.size = {100, 100}, .color = Color::white()});
                            });
                        });
                });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// trim_before in nested context
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceBuilder — nested with trim_before") {
    // Global frame 120, outer from=100 dur=200
    // chapter.local = 20
    // inner from=10 dur=30 trim_before=5
    // title local = 20 - 10 + 5 = 15
    FrameContext ctx = make_seq_ctx(Frame{120});
    SceneBuilder s(ctx);

    s.sequence("chapter", {.from = Frame{100}, .duration = Frame{200}},
        [](SequenceBuilder& chapter) {
            chapter.sequence("title", {.from = Frame{10}, .duration = Frame{30}, .trim_before = Frame{5}},
                [](SequenceBuilder& title) {
                    CHECK(title.local_frame() == Frame{15});
                    title.layer("title_layer", [](LayerBuilder& l) {
                        l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    });
                });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Overlapping siblings in SequenceBuilder
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceBuilder — overlapping sibling sequences") {
    // Global frame 50, parent from=0 dur=100
    // overlay_a from=0 dur=60 → active at local 50
    // overlay_b from=40 dur=60 → active at local 10
    FrameContext ctx = make_seq_ctx(Frame{50});
    SceneBuilder s(ctx);

    s.sequence("parent", {.from = Frame{0}, .duration = Frame{100}},
        [](SequenceBuilder& parent) {
            parent.sequence("overlay_a", {.from = Frame{0}, .duration = Frame{60}},
                [](SequenceBuilder& a) {
                    CHECK(a.local_frame() == Frame{50});
                    a.layer("layer_a", [](LayerBuilder& l) {
                        l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    });
                });
            parent.sequence("overlay_b", {.from = Frame{40}, .duration = Frame{60}},
                [](SequenceBuilder& b) {
                    CHECK(b.local_frame() == Frame{10});
                    b.layer("layer_b", [](LayerBuilder& l) {
                        l.rect("r", {.size = {100, 100}, .color = Color::white()});
                    });
                });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// Backward compatibility: SceneBuilder& lambda still works
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceBuilder — backward compat: SceneBuilder& lambda") {
    FrameContext ctx = make_seq_ctx(Frame{10});
    SceneBuilder s(ctx);

    // Old-style lambda taking SceneBuilder&
    s.sequence("intro", {.from = Frame{0}, .duration = Frame{30}},
        [](SceneBuilder& s) {
            s.layer("bg", [](LayerBuilder& l) {
                l.rect("r", {.size = {100, 100}, .color = Color::white()});
            });
        });

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
    CHECK(std::string(scene.layers()[0].name.c_str()) == "bg");
}

// ═══════════════════════════════════════════════════════════════════════════
// SequenceBuilder: context propagation (width, height, frame_rate)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceBuilder — context propagation") {
    FrameContext ctx = make_seq_ctx(Frame{10});
    ctx.width = 1280;
    ctx.height = 720;
    ctx.frame_rate = {24, 1};
    SceneBuilder s(ctx);

    s.sequence("test", {.from = Frame{0}, .duration = Frame{60}},
        [](SequenceBuilder& seq) {
            const auto& c = seq.context();
            CHECK(c.width == 1280);
            CHECK(c.height == 720);
            CHECK(c.frame_rate.numerator == 24);
            CHECK(c.frame == Frame{10});
            CHECK(c.local_frame == Frame{10});
            CHECK(c.duration == Frame{60});
        });
}
