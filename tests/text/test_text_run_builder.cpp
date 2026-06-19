// ═══════════════════════════════════════════════════════════════════════════
// test_text_run_builder.cpp — TextRunBuilder smoke tests
//
// Covers:
//   1. TextRunBuilder accumulates .position / .opacity / .animator / .selector
//      into the underlying TextRunSpec.params.animators vector uniformly.
//   2. LayerBuilder::text_run produces a stable reference that survives
//      multiple successive calls.
//   3. TextRunBuilder fluent chain (after commit()) can re-enter the
//      LayerBuilder chain without losing state.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

namespace {

/// Tiny helper: build a TextRunSpec with the canonical composable fields.
/// Uses `.text.*` paths exclusively — internal code must NOT touch the
/// deprecated `TextRunParams` alias.
TextRunSpec make_text_run_spec(std::string value, f32 font_size = 72.0f) {
    TextRunSpec spec;
    spec.text.content.value = std::move(value);
    spec.text.font.font_size = font_size;
    spec.text.font.font_path = "assets/fonts/Inter-Bold.ttf";
    spec.text.font.font_family = "Inter";
    spec.text.font.font_weight = 800;
    spec.text.font.font_style = "normal";
    spec.text.appearance.color = {1.0f, 1.0f, 1.0f, 1.0f};
    spec.text.position = {0.0f, 0.0f, 0.0f};
    spec.text.layout.anchor = TextAnchor::Center;
    spec.text.layout.align = TextAlign::Center;
    spec.text.layout.vertical_align = VerticalAlign::Middle;
    spec.text.layout.wrap = TextWrap::None;
    spec.direction = TextDirection::Auto;
    return spec;
}

GlyphSelectorSpec make_selector(std::string id, int n = 1) {
    GlyphSelectorSpec s;
    s.id = std::move(id);
    s.unit = TextSelectorUnit::Glyph;
    s.shape = TextSelectorShape::Square;
    s.start = {0.0f};
    s.end = {100.0f};
    s.amount = {100.0f};
    s.exclude_spaces = false;
    (void)n;
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. TextRunBuilder accumulates implicit per-mutator animators + explicit
//    .animator() / .selector() adds uniformly.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: position/opacity injects implicit animators") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunSpec params = make_text_run_spec("Hi");
    TextRunBuilder& trb = lb.text_run("implicit_run", std::move(params));

    trb.position({1.0f, 2.0f, 3.0f})
       .opacity(0.5f);

    // Two implicit animators (one per call) are appended to pending.animators.
    CHECK(trb.spec().animators.size() == 2);
    // First injected animator carries a PositionProperty.
    CHECK(std::holds_alternative<PositionProperty>(trb.spec().animators[0].properties[0]));
    // Second injected animator carries an OpacityProperty.
    CHECK(std::holds_alternative<OpacityProperty>(trb.spec().animators[1].properties[0]));
    // Both use the always-on (0..100) global glyph selector.
    CHECK(trb.spec().animators[0].selectors.size() == 1);
    CHECK(trb.spec().animators[1].selectors.size() == 1);
}

// PR-3-SENSITIVE: revisits needed when .selector().animator() binding changes.
// This test asserts `animators.size() == 3` + last.id == "user_slide_in" +
// last.selectors.size() == 2 — derived from the CURRENT PR 3 semantics
// (selector-after-animator appends to the LAST animator's selector list).
// If PR 3 is revisited, these counts/IDs MUST be re-derived.
TEST_CASE("TextRunBuilder: chain via LayerBuilder accumulates all entries") {
    LayerBuilder lb("test_layer", Frame{0});

    TextRunSpec params = make_text_run_spec("Hello", 64.0f);

    // First text_run.
    TextRunBuilder& trb_a = lb.text_run("run_a", params);
    trb_a.position({10.0f, 20.0f, 0.0f})
         .opacity(0.7f)
         .animator(TextAnimatorSpec{
             .id = "user_slide_in",
             .enabled = true,
             .selectors = { make_selector("user_slide_in_sel") },
             .properties = { PositionProperty{{0.0f, 40.0f, 0.0f}} }
         })
         .selector(make_selector("user_stagger_sel"));

    // Second text_run — must NOT invalidate the first reference.
    TextRunSpec params_b = make_text_run_spec("World", 64.0f);
    TextRunBuilder& trb_b = lb.text_run("run_b", params_b);
    trb_b.opacity(0.5f).rotate({0.0f, 30.0f, 0.0f});

    // Verify the first reference is still alive.
    CHECK_FALSE(trb_a.spec().text.content.value.empty());
    CHECK(trb_a.spec().text.content.value == "Hello");

    // Both runs landed.
    const auto& runs_a_after = trb_a.spec();
    const auto& runs_b_after = trb_b.spec();
    CHECK(runs_a_after.text.content.value == "Hello");
    CHECK(runs_b_after.text.content.value == "World");

    // PR 3 — .selector(user_stagger_sel) AFTER .animator(user_a)
    // now attaches the selector to user_a (the LAST animator) instead of
    // appending a separate phantom entry.  So:
    //   .position/.opacity/.animator  → 3 entries (animator_id = "user_slide_in")
    //   .selector(user_stagger_sel)   → becomes a 2nd selector ON user_a
    // Total animators.size() == 3.
    CHECK(runs_a_after.animators.size() == 3);
    // The LAST animator retains its id and now carries 2 selectors:
    //   [0] user_slide_in_sel (set in the explicit .animator(...) spec)
    //   [1] user_stagger_sel  (attached by the follow-up .selector() call)
    REQUIRE(runs_a_after.animators.back().id == "user_slide_in");
    REQUIRE(runs_a_after.animators.back().selectors.size() == 2);
    CHECK(runs_a_after.animators.back().selectors[0].id == "user_slide_in_sel");
    CHECK(runs_a_after.animators.back().selectors[1].id == "user_stagger_sel");

    // trb_b chain `.opacity(0.5f).rotate(...)`: 2 implicit animators, no selectors.
    // PR 3 does not change implicit-mutator flow.
    CHECK(runs_b_after.animators.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// PR 3 — Selector binding semantics (`.selector(s).animator(a)` + `.animator(a).selector(s)`)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: .selector(s).animator(a) queues s and prepends it to a") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("pat_a", make_text_run_spec("P"));

    // `.selector(s).animator(a)` is the documented "selector-first" pattern:
    //   s queues, then `.animator(a)` drains the queue by PREPENDING s to a's
    //   own selector list.  Order = [s, a's original].
    auto sel_s = make_selector("sel_s");
    auto sel_a = make_selector("sel_a");
    trb.selector(sel_s)
       .animator(TextAnimatorSpec{
           .id = "a1",
           .enabled = true,
           .selectors = { sel_a },
           .properties = { PositionProperty{{1.0f, 2.0f, 3.0f}} }
       });

    CHECK(trb.spec().animators.size() == 1);   // no phantom entry
    const auto& anim = trb.spec().animators.front();
    REQUIRE(anim.selectors.size() == 2);
    CHECK(anim.selectors[0].id == "sel_s");    // prepended
    CHECK(anim.selectors[1].id == "sel_a");    // original
}

TEST_CASE("TextRunBuilder: .animator(a).selector(s) appends s to a (no new animator)") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("pat_b", make_text_run_spec("P"));

    // `.animator(a).selector(s)` is the documented "selector-after" pattern:
    //   s is APPENDED to a's selector list; no new phantom animator is created.
    auto sel_s = make_selector("sel_s");
    auto sel_a = make_selector("sel_a");
    trb.animator(TextAnimatorSpec{
           .id = "a1",
           .enabled = true,
           .selectors = { sel_a },
           .properties = { PositionProperty{{1.0f, 2.0f, 3.0f}} }
       })
       .selector(sel_s);

    CHECK(trb.spec().animators.size() == 1);   // selector did NOT spawn a phantom
    const auto& anim = trb.spec().animators.front();
    REQUIRE(anim.selectors.size() == 2);
    CHECK(anim.selectors[0].id == "sel_a");    // original
    CHECK(anim.selectors[1].id == "sel_s");    // appended
}

TEST_CASE("TextRunBuilder: chained queued selectors all prepend to next animator") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("pat_c", make_text_run_spec("P"));

    // `.selector(s).selector(t).animator(a)` — s and t both queue, then both
    // are prepended (in declared order) to a's selector list.
    auto sel_s = make_selector("sel_s");
    auto sel_t = make_selector("sel_t");
    auto sel_a = make_selector("sel_a");
    trb.selector(sel_s)
       .selector(sel_t)
       .animator(TextAnimatorSpec{
           .id = "a1",
           .enabled = true,
           .selectors = { sel_a },
           .properties = { PositionProperty{{1.0f, 2.0f, 3.0f}} }
       });

    CHECK(trb.spec().animators.size() == 1);
    const auto& anim = trb.spec().animators.front();
    REQUIRE(anim.selectors.size() == 3);
    CHECK(anim.selectors[0].id == "sel_s");    // queued first
    CHECK(anim.selectors[1].id == "sel_t");    // queued second
    CHECK(anim.selectors[2].id == "sel_a");    // original
}

TEST_CASE("TextRunBuilder: standalone .selector(s) queues until next .animator()") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("pat_d", make_text_run_spec("P"));

    // A lone `.selector(s)` (no animator in the chain) must NOT spawn a
    // phantom animator entry (PR-3 fix) — it sits in `m_pending_selectors`
    // and is consumed by the next explicit `.animator(...)`.
    auto sel_s = make_selector("sel_s");
    trb.selector(sel_s);

    CHECK(trb.spec().animators.empty());
    trb.animator(TextAnimatorSpec{
        .id = "a1",
        .enabled = true,
        .properties = { OpacityProperty{0.8f} }
    });

    CHECK(trb.spec().animators.size() == 1);
    REQUIRE(trb.spec().animators.front().selectors.size() == 1);
    CHECK(trb.spec().animators.front().selectors[0].id == "sel_s");
}

TEST_CASE("TextRunBuilder: queued selectors survive across implicit mutators until explicit .animator()") {
    // PR 3 — `m_pending_selectors` is preserved across implicit mutator
    // calls (`.position()`, `.opacity()`, `.rotate()`, ..., `.baseline_shift()`).
    // Only the EXPLICIT `.animator(...)` drains the queue and prepends
    // the selectors to the new animator's selector list.  This is what
    // makes `.selector(s).position(p).animator(a)` bind s to a (not
    // to the implicit `position` animator) without spawning a phantom.
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("pat_e", make_text_run_spec("P"));

    auto sel_s = make_selector("sel_s");

    // `.selector(s).position(p)`:
    //   - selector(s): no animator yet → queue m_pending = [s]
    //   - position(p): appends implicit animator; does NOT drain queue.
    //     animators = [pos_implicit{selectors=[global]}]; m_pending = [s]
    trb.selector(sel_s).position({1.0f, 2.0f, 0.0f});

    CHECK(trb.spec().animators.size() == 1);
    // The implicit position animator has only its own global selector; s
    // is still queued, not attached here.
    REQUIRE(trb.spec().animators.front().selectors.size() == 1);

    // Now an explicit `.animator(...)` drains the queue and prepends s.
    trb.animator(TextAnimatorSpec{
        .id = "explicit_a",
        .enabled = true,
        .properties = { OpacityProperty{0.9f} }
    });
    CHECK(trb.spec().animators.size() == 2);
    // The just-appended (second) animator received the queued s prepended.
    const auto& anim_a = trb.spec().animators.back();
    REQUIRE(anim_a.id == "explicit_a");
    REQUIRE(anim_a.selectors.size() == 1);
    CHECK(anim_a.selectors.front().id == "sel_s");
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Multiple text_run calls on a single LayerBuilder produce a stable
//    reference into the builder pool.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: chained references stay valid across push_back") {
    LayerBuilder lb("test_layer", Frame{0});

    TextRunBuilder* first_ref = nullptr;
    for (int i = 0; i < 5; ++i) {
        TextRunSpec params = make_text_run_spec(
            "frame_" + std::to_string(i), 32.0f);
        TextRunBuilder& trb = lb.text_run(
            "name_" + std::to_string(i), std::move(params));
        if (i == 0) {
            first_ref = &trb;
        } else {
            // Verify the prior reference still works.
            // NOTE: this alone doesn't catch the bug (compiler might
            // re-allocate).  We rely on the live spec inspection below.
            CHECK(first_ref != nullptr);
        }
        trb.opacity(0.1f * static_cast<f32>(i + 1));
    }
    // First reference should still be alive and read correctly.
    REQUIRE(first_ref != nullptr);
    CHECK(first_ref->spec().text.content.value == "frame_0");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. commit() returns the parent LayerBuilder for re-entry.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: commit hands control back to parent") {
    LayerBuilder lb("test_layer", Frame{0});

    TextRunSpec params = make_text_run_spec("Commit");
    TextRunBuilder& trb = lb.text_run("commit_run", std::move(params));
    LayerBuilder& back = trb.opacity(0.3f).commit();
    // Set a layer-level field; verify the returned reference is
    // the same LayerBuilder.
    back.opacity(0.9f);

    // Just a smoke check — re-using the same chain returned a valid
    // LayerBuilder (no segfault, valid parent association).
    CHECK(&back == &lb);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. PR 2 — State propagation on auto-build (no explicit commit needed)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: cache_layout(false) propagates to pending without commit") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("cache_false", make_text_run_spec("A"));
    trb.cache_layout(false);
    // PR 2: previously this only updated the discarded `m_cache_layout`
    // and required explicit `.commit()` to sync; auto-build now reads
    // pending directly so the value is observable immediately.
    CHECK_FALSE(trb.spec().cache_layout);
}

TEST_CASE("TextRunBuilder: cache_layout(true) is the default") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("cache_default", make_text_run_spec("B"));
    CHECK(trb.spec().cache_layout);
}

TEST_CASE("TextRunBuilder: font_size() updates pending.font_size AND invalidates cache") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("font_size_test", make_text_run_spec("C", 64.0f));
    trb.font_size(48.0f);
    CHECK(trb.spec().text.font.font_size == doctest::Approx(48.0f));
    CHECK_FALSE(trb.spec().cache_layout);
}

TEST_CASE("TextRunBuilder: font_engine() per-spec override persists to build_spec") {
    LayerBuilder lb("test_layer", Frame{0});
    TextRunBuilder& trb = lb.text_run("engine_override", make_text_run_spec("D"));
    // Sentinel pointer: tests verify pointer propagation without ever
    // dereferencing it (no real FontEngine required).
    FontEngine* sentinel = reinterpret_cast<FontEngine*>(0xC0FFEE);
    trb.font_engine(sentinel);
    CHECK(trb.build_spec().font_engine == sentinel);
    CHECK(trb.build_spec().name == "engine_override");
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Compat: TextRunParams alias identity — the SOLE sanctioned use of the
//    deprecated alias name.  All other production code and tests must
//    reference `TextRunSpec` directly.  The pragma-suppression below silences
//    `-Wdeprecated-declarations` ONLY around the static_assert body, per
//    migration policy (no global ignores).
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunParams: deprecated alias preserves type identity with TextRunSpec") {
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    static_assert(std::is_same_v<chronon3d::TextRunParams, chronon3d::TextRunSpec>,
                  "TextRunParams must remain a type alias of TextRunSpec");
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Registry integration: shape_ids::TextRun is registered, the variant
//    accepts TextRunSpec, ShapeType::TextRun is in the enum, and
//    RenderNodeFactory::text_run produces a flagged RenderNode.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/registry/shape_params.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp>

TEST_CASE("TextRun registry: shape_ids::TextRun exposed + variant accepts TextRunSpec") {
    auto& reg = chronon3d::registry::ShapeRegistry::instance();
    CHECK(reg.contains(chronon3d::registry::shape_ids::TextRun));

    // Variant rejects unrecognized payloads, so verify TextRunSpec
    // (canonical composable) is a valid ShapeParams alternative.
    chronon3d::registry::ShapeParams p = make_text_run_spec("Hello");
    CHECK(std::holds_alternative<chronon3d::TextRunSpec>(p));
}

TEST_CASE("TextRun registry: RenderNodeFactory::text_run produces flagged RenderNode") {
    std::pmr::monotonic_buffer_resource pool;
    auto node = chronon3d::RenderNodeFactory::text_run(
        &pool, "registry_run", make_text_run_spec("World"));

    CHECK(node.is_text_run_shape);
    CHECK(node.shape.type == chronon3d::ShapeType::TextRun);
    // Identity smoke check — the named node must round-trip.
    CHECK(node.name == std::pmr::string{"registry_run", &pool});
}

TEST_CASE("TextRun registry: ShapeRegistry::create_node(\"shape.text_run\", ...) routes to factory") {
    auto& reg = chronon3d::registry::ShapeRegistry::instance();
    std::pmr::monotonic_buffer_resource pool;
    chronon3d::registry::ShapeParams params = make_text_run_spec("FromRegistry");
    auto node = reg.create_node(
        chronon3d::registry::shape_ids::TextRun,
        &pool, "registry_route",
        std::move(params));

    CHECK(node.is_text_run_shape);
    CHECK(node.shape.type == chronon3d::ShapeType::TextRun);
    CHECK(node.name == std::pmr::string{"registry_route", &pool});
}
