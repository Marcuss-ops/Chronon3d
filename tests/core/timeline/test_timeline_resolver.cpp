// ── tests/core/timeline/test_timeline_resolver.cpp
//
// Tests for Sequence V2: SequenceNode + TimelineResolver.
//
// Coverage:
//   - Flat sequence: active window, local frame, progress
//   - Nested sequence: chained local frame computation
//   - trim_before offset
//   - Inactive frames (before, at end, after)
//   - Zero duration guard
//   - Multiple root sequences
//   - Multi-level nesting (3+ levels)
//   - Effective context propagation (frame_rate, width, height)
//   - resolve_one convenience

#include <doctest/doctest.h>
#include <chronon3d/timeline/timeline_resolver.hpp>
#include <chronon3d/core/types/frame_context.hpp>

using namespace chronon3d;

// ── Helper: build a FrameContext at a given frame ─────────────────────
static FrameContext make_ctx(Frame frame, i32 fps_num = 30, i32 fps_den = 1) {
    FrameContext ctx;
    ctx = ctx.with_frame(frame);
    ctx = ctx.with_frame_rate({fps_num, fps_den});
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

// ═══════════════════════════════════════════════════════════════════════════
// Flat sequence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — flat sequence: active window") {
    SequenceNode root{"intro", {Frame{30}, Frame{60}}, {}};

    SUBCASE("before sequence: no resolution") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{29}));
        CHECK(results.empty());
    }

    SUBCASE("at start frame: active, local=0") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{30}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path.size() == 1);
        CHECK(results[0].active_path[0].name == "intro");
        CHECK(results[0].active_path[0].local_frame == Frame{0});
        CHECK(results[0].effective_context.frame() == Frame{0});
        CHECK(results[0].effective_context.local_time().integral_frame() == Frame{0});
    }

    SUBCASE("mid-sequence: correct local frame") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{50}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].local_frame == Frame{20});
        CHECK(results[0].effective_context.frame() == Frame{20});
    }

    SUBCASE("at end frame (exclusive): no resolution") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{90}));
        CHECK(results.empty());
    }

    SUBCASE("after sequence: no resolution") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{100}));
        CHECK(results.empty());
    }
}

TEST_CASE("TimelineResolver — flat sequence: progress") {
    SequenceNode root{"title", {Frame{0}, Frame{100}}, {}};

    SUBCASE("at start: progress=0") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{0}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].progress == doctest::Approx(0.0f));
    }

    SUBCASE("midway: progress=0.5") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{50}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].progress == doctest::Approx(0.5f));
    }

    SUBCASE("near end: progress=0.99") {
        auto results = TimelineResolver::resolve({root}, make_ctx(Frame{99}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].progress == doctest::Approx(0.99f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Nested sequence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — nested sequence: chained local frame") {
    // chapter starts at global 100, duration 200
    //   title starts at local 0, duration 30
    //   body starts at local 30, duration 150
    SequenceNode chapter{"chapter", {Frame{100}, Frame{200}}, {}};
    chapter.add_child("title", {Frame{0}, Frame{30}});
    chapter.add_child("body",  {Frame{30}, Frame{150}});

    SUBCASE("global 120 → chapter.local=20, title.local=20") {
        auto results = TimelineResolver::resolve({chapter}, make_ctx(Frame{120}));
        // chapter is active, title is active (frame 20 within 0..30), body is not (20 < 30)
        REQUIRE(results.size() == 2); // chapter + title

        // First: chapter
        CHECK(results[0].active_path.size() == 1);
        CHECK(results[0].active_path[0].name == "chapter");
        CHECK(results[0].active_path[0].local_frame == Frame{20});
        CHECK(results[0].effective_context.frame() == Frame{20});

        // Second: title (nested inside chapter)
        CHECK(results[1].active_path.size() == 2);
        CHECK(results[1].active_path[0].name == "chapter");
        CHECK(results[1].active_path[0].local_frame == Frame{20});
        CHECK(results[1].active_path[1].name == "title");
        CHECK(results[1].active_path[1].local_frame == Frame{20});
        CHECK(results[1].effective_context.frame() == Frame{20});
    }

    SUBCASE("global 130 → chapter.local=30, body.local=0") {
        auto results = TimelineResolver::resolve({chapter}, make_ctx(Frame{130}));
        // chapter active, title NOT active (30 >= 0+30, exclusive), body active (30 within 30..180)
        REQUIRE(results.size() == 2); // chapter + body

        // First: chapter
        CHECK(results[0].active_path[0].name == "chapter");
        CHECK(results[0].active_path[0].local_frame == Frame{30});

        // Second: body
        CHECK(results[1].active_path.size() == 2);
        CHECK(results[1].active_path[1].name == "body");
        CHECK(results[1].active_path[1].local_frame == Frame{0});
        CHECK(results[1].effective_context.frame() == Frame{0});
    }

    SUBCASE("global 99 → not active (before chapter)") {
        auto results = TimelineResolver::resolve({chapter}, make_ctx(Frame{99}));
        CHECK(results.empty());
    }

    SUBCASE("global 300 → not active (after chapter)") {
        auto results = TimelineResolver::resolve({chapter}, make_ctx(Frame{300}));
        CHECK(results.empty());
    }
}

TEST_CASE("TimelineResolver — 3-level nesting") {
    // project from=0, duration=300
    //   chapter from=100, duration=200
    //     title from=0, duration=30
    SequenceNode project{"project", {Frame{0}, Frame{300}}, {}};
    auto& chapter = project.add_child("chapter", {Frame{100}, Frame{200}});
    chapter.add_child("title", {Frame{0}, Frame{30}});

    SUBCASE("global 100 → project.local=100, chapter.local=0, title.local=0") {
        auto results = TimelineResolver::resolve({project}, make_ctx(Frame{100}));
        // project, chapter, title all active
        REQUIRE(results.size() == 3);

        CHECK(results[0].active_path.size() == 1);
        CHECK(results[0].active_path[0].name == "project");
        CHECK(results[0].active_path[0].local_frame == Frame{100});

        CHECK(results[1].active_path.size() == 2);
        CHECK(results[1].active_path[1].name == "chapter");
        CHECK(results[1].active_path[1].local_frame == Frame{0});

        CHECK(results[2].active_path.size() == 3);
        CHECK(results[2].active_path[2].name == "title");
        CHECK(results[2].active_path[2].local_frame == Frame{0});
        CHECK(results[2].effective_context.frame() == Frame{0});
    }

    SUBCASE("global 120 → project.local=120, chapter.local=20, title.local=20") {
        auto results = TimelineResolver::resolve({project}, make_ctx(Frame{120}));
        REQUIRE(results.size() == 3);

        CHECK(results[2].active_path[2].name == "title");
        CHECK(results[2].active_path[2].local_frame == Frame{20});
        CHECK(results[2].effective_context.frame() == Frame{20});
    }

    SUBCASE("global 130 → project.local=130, chapter.local=30, title inactive") {
        auto results = TimelineResolver::resolve({project}, make_ctx(Frame{130}));
        // project + chapter active, title NOT active (30 >= 0+30 exclusive)
        REQUIRE(results.size() == 2);
        CHECK(results[1].active_path[1].name == "chapter");
        CHECK(results[1].active_path[1].local_frame == Frame{30});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// trim_before tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — trim_before offset") {
    // Sequence starts at global 50, but content starts at local 10
    SequenceNode seq{"trimmed", {Frame{50}, Frame{40}, Frame{10}}, {}};

    SUBCASE("global 50 → local = 0 + trim_before = 10") {
        auto results = TimelineResolver::resolve({seq}, make_ctx(Frame{50}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].local_frame == Frame{10});
        CHECK(results[0].effective_context.frame() == Frame{10});
    }

    SUBCASE("global 70 → local = 20 + trim_before = 30") {
        auto results = TimelineResolver::resolve({seq}, make_ctx(Frame{70}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].local_frame == Frame{30});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Zero duration guard
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — zero duration: never active") {
    SequenceNode seq{"empty", {Frame{10}, Frame{0}}, {}};

    auto results = TimelineResolver::resolve({seq}, make_ctx(Frame{10}));
    CHECK(results.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Multiple root sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — multiple roots") {
    SequenceNode intro{"intro", {Frame{0}, Frame{30}}, {}};
    SequenceNode title{"title", {Frame{30}, Frame{60}}, {}};
    SequenceNode outro{"outro", {Frame{90}, Frame{30}}, {}};

    SUBCASE("frame 15: only intro active") {
        auto results = TimelineResolver::resolve({intro, title, outro}, make_ctx(Frame{15}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].name == "intro");
        CHECK(results[0].active_path[0].local_frame == Frame{15});
    }

    SUBCASE("frame 45: only title active") {
        auto results = TimelineResolver::resolve({intro, title, outro}, make_ctx(Frame{45}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].name == "title");
        CHECK(results[0].active_path[0].local_frame == Frame{15});
    }

    SUBCASE("frame 100: only outro active") {
        auto results = TimelineResolver::resolve({intro, title, outro}, make_ctx(Frame{100}));
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].name == "outro");
        CHECK(results[0].active_path[0].local_frame == Frame{10});
    }

    SUBCASE("frame 80: title active, others inactive") {
        auto results = TimelineResolver::resolve({intro, title, outro}, make_ctx(Frame{80}));
        // 80 is in title (30..90), not in intro (0..30), not in outro (90..120)
        REQUIRE(results.size() == 1);
        CHECK(results[0].active_path[0].name == "title");
        CHECK(results[0].active_path[0].local_frame == Frame{50});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// resolve_one convenience
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — resolve_one") {
    SequenceNode seq{"hero", {Frame{10}, Frame{50}}, {}};

    SUBCASE("active: returns resolution") {
        auto result = TimelineResolver::resolve_one(seq, make_ctx(Frame{30}));
        REQUIRE(result.has_value());
        CHECK(result->active_path[0].name == "hero");
        CHECK(result->active_path[0].local_frame == Frame{20});
    }

    SUBCASE("inactive: returns nullopt") {
        auto result = TimelineResolver::resolve_one(seq, make_ctx(Frame{5}));
        CHECK(!result.has_value());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// any_active convenience
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — any_active") {
    SequenceNode a{"a", {Frame{0}, Frame{30}}, {}};
    SequenceNode b{"b", {Frame{60}, Frame{30}}, {}};

    CHECK(TimelineResolver::any_active({a, b}, make_ctx(Frame{10})));
    CHECK_FALSE(TimelineResolver::any_active({a, b}, make_ctx(Frame{45})));
    CHECK(TimelineResolver::any_active({a, b}, make_ctx(Frame{70})));
}

// ═══════════════════════════════════════════════════════════════════════════
// Effective context propagation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — effective context preserves parent properties") {
    SequenceNode seq{"test", {Frame{0}, Frame{60}}, {}};

    FrameContext ctx = make_ctx(Frame{30});
    ctx.width = 1280;
    ctx.height = 720;
    ctx = ctx.with_frame_rate({24, 1});

    auto result = TimelineResolver::resolve_one(seq, ctx);
    REQUIRE(result.has_value());

    const auto& eff = result->effective_context;
    CHECK(eff.frame() == Frame{30});
    CHECK(eff.local_time().integral_frame() == Frame{30});
    CHECK(eff.width == 1280);
    CHECK(eff.height == 720);
    CHECK(eff.frame_rate().numerator == 24);
    CHECK(eff.duration() == Frame{60});
}

// ═══════════════════════════════════════════════════════════════════════════
// SequenceRange unit tests (direct)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceRange — contains") {
    SequenceRange r{Frame{10}, Frame{20}, Frame{0}};

    CHECK_FALSE(r.contains(Frame{9}));
    CHECK(r.contains(Frame{10}));
    CHECK(r.contains(Frame{29}));
    CHECK_FALSE(r.contains(Frame{30}));
}

TEST_CASE("SequenceRange — to_local") {
    SequenceRange r{Frame{10}, Frame{20}, Frame{0}};
    CHECK(r.to_local(Frame{10}) == Frame{0});
    CHECK(r.to_local(Frame{20}) == Frame{10});
    CHECK(r.to_local(Frame{29}) == Frame{19});
}

TEST_CASE("SequenceRange — to_local with trim_before") {
    SequenceRange r{Frame{10}, Frame{20}, Frame{5}};
    CHECK(r.to_local(Frame{10}) == Frame{5});
    CHECK(r.to_local(Frame{20}) == Frame{15});
}

TEST_CASE("SequenceRange — zero duration never contains") {
    SequenceRange r{Frame{10}, Frame{0}, Frame{0}};
    CHECK_FALSE(r.contains(Frame{10}));
    CHECK_FALSE(r.contains(Frame{0}));
}

TEST_CASE("SequenceRange — end()") {
    SequenceRange r{Frame{10}, Frame{20}, Frame{0}};
    CHECK(r.end() == Frame{30});
}

// ═══════════════════════════════════════════════════════════════════════════
// SequenceNode — add_child chaining
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceNode — add_child chaining") {
    SequenceNode root{"root", {Frame{0}, Frame{300}}, {}};
    root.add_child("a", {Frame{0}, Frame{100}})
        .add_child("a1", {Frame{0}, Frame{50}});

    CHECK(root.children.size() == 1);
    CHECK(root.children[0].name == "a");
    CHECK(root.children[0].children.size() == 1);
    CHECK(root.children[0].children[0].name == "a1");
    CHECK(root.is_leaf() == false);
    CHECK(root.children[0].children[0].is_leaf() == true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Overlapping sibling sequences (both active at same frame)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimelineResolver — overlapping siblings") {
    SequenceNode parent{"parent", {Frame{0}, Frame{100}}, {}};
    parent.add_child("overlay_a", {Frame{0}, Frame{60}});
    parent.add_child("overlay_b", {Frame{40}, Frame{60}});

    SUBCASE("frame 50: both children active") {
        auto results = TimelineResolver::resolve({parent}, make_ctx(Frame{50}));
        // parent + overlay_a + overlay_b = 3
        REQUIRE(results.size() == 3);
        CHECK(results[1].active_path[1].name == "overlay_a");
        CHECK(results[1].active_path[1].local_frame == Frame{50});
        CHECK(results[2].active_path[1].name == "overlay_b");
        CHECK(results[2].active_path[1].local_frame == Frame{10});
    }
}
