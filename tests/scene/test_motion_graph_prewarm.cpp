// test_motion_graph_prewarm.cpp — P2 motion-graph pre-warm coverage.
//
// Covers:
//   * Empty scene returns ok=true with zero counts
//   * Mixed-kind Scene counts text layers correctly
//   * MotionPrewarmOptions include_* flags honoured
//   * Idempotency: same Scene+Options twice yields identical result

#include <doctest/doctest.h>

#include <chronon3d/runtime/motion_graph_prewarm.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

using chronon3d::Scene;
using chronon3d::Layer;
using chronon3d::LayerKind;
using chronon3d::prewarm_motion_graph;
using chronon3d::MotionPrewarmOptions;

TEST_CASE("prewarm_motion_graph: empty scene is a clean no-op") {
    Scene s;
    auto r = prewarm_motion_graph(s);
    CHECK(r.ok);
    CHECK(r.layers_touched == 0);
    CHECK(r.text_layers_touched == 0);
    CHECK(r.elapsed_us >= 0);
    // v1.0 honesty contract: stub warning ALWAYS emitted so callers see
    // that no TextLayoutCache is actually primed. The substring pin is on
    // the semantic load-bearing word, not the version marker, so wording
    // tweaks don't break the test.
    CHECK_FALSE(r.notes.empty());
    CHECK(r.notes.find("no TextLayoutCache primed") != std::string::npos);
}

TEST_CASE("prewarm_motion_graph: counts every layer in the scene") {
    Scene s;
    Layer a; a.name = "a"; a.kind = LayerKind::Shape;  s.add_layer(std::move(a));
    Layer b; b.name = "b"; b.kind = LayerKind::Shape;  s.add_layer(std::move(b));
    Layer c; c.name = "c"; s.add_layer(std::move(c));
    auto r = prewarm_motion_graph(s);
    CHECK(r.ok);
    CHECK(r.layers_touched == 3);
}

TEST_CASE("prewarm_motion_graph: text layers are counted only when "
          "include_text_run_layout is true") {
    Scene s;
    Layer t1; t1.name = "t1"; t1.kind = LayerKind::Text;  s.add_layer(std::move(t1));
    Layer t2; t2.name = "t2"; t2.kind = LayerKind::Text;  s.add_layer(std::move(t2));
    Layer s1; s1.name = "s1"; s1.kind = LayerKind::Shape; s.add_layer(std::move(s1));

    SUBCASE("text default-on") {
        auto r = prewarm_motion_graph(s);
        CHECK(r.text_layers_touched == 2);
        CHECK(r.layers_touched == 3);
    }
    SUBCASE("text explicitly off") {
        MotionPrewarmOptions opts;
        opts.include_text_run_layout = false;
        auto r = prewarm_motion_graph(s, opts);
        CHECK(r.text_layers_touched == 0);
        CHECK(r.layers_touched == 3);
    }
}

TEST_CASE("prewarm_motion_graph: idempotent — same Scene+Options twice") {
    Scene s;
    Layer t; t.name = "t"; t.kind = LayerKind::Text; s.add_layer(std::move(t));
    Layer sh; sh.name = "sh"; sh.kind = LayerKind::Shape; s.add_layer(std::move(sh));

    MotionPrewarmOptions opts;
    opts.verbose_log = true;

    auto r1 = prewarm_motion_graph(s, opts);
    auto r2 = prewarm_motion_graph(s, opts);

    CHECK(r1.ok);
    CHECK(r2.ok);
    CHECK(r1.layers_touched == r2.layers_touched);
    CHECK(r1.text_layers_touched == r2.text_layers_touched);
    CHECK_FALSE(r1.notes.empty());
    CHECK_FALSE(r2.notes.empty());
    // Notes are deterministic across idempotent calls.
    CHECK(r1.notes == r2.notes);
}
