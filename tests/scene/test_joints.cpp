// test_joints.cpp — P2 joints API coverage.
//
// Covers:
//   * JointRegistry primitives (add/remove/get)
//   * apply_joints chained math (parent->child position+rotation)
//   * Self-loop cycle detection
//   * Missing layer id -> skipped
//   * Idempotency (apply_joints twice yields identical layer state)

#include <doctest/doctest.h>

#include <chronon3d/scene/joints/joints_api.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

#include <cmath>
#include <string>

using chronon3d::Scene;
using chronon3d::Layer;
using chronon3d::LayerKind;
using chronon3d::Transform;
using chronon3d::Vec2;
using chronon3d::Vec3;
using chronon3d::joints::Joint;
using chronon3d::joints::JointId;
using chronon3d::joints::JointRegistry;
using chronon3d::joints::apply_joints;
using chronon3d::joints::JointApplyResult;

namespace {

// Helper: build a Scene with 4 named layers at known positions.
Scene make_chain_scene() {
    Scene s;
    Layer a; a.name = "A"; a.kind = LayerKind::Shape; a.transform.position = {0.0f, 0.0f, 0.0f};
    Layer b; b.name = "B"; b.kind = LayerKind::Shape; b.transform.position = {10.0f, 5.0f, 0.0f};
    Layer c; c.name = "C"; c.kind = LayerKind::Shape; c.transform.position = {20.0f, 10.0f, 0.0f};
    Layer d; d.name = "D"; d.kind = LayerKind::Shape; d.transform.position = {30.0f, 15.0f, 0.0f};
    s.add_layer(std::move(a));
    s.add_layer(std::move(b));
    s.add_layer(std::move(c));
    s.add_layer(std::move(d));
    return s;
}

bool near(f32 a, f32 b, f32 eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

// ── JointRegistry primitives ───────────────────────────────────────────
TEST_CASE("JointRegistry: add/get/remove round-trip") {
    JointRegistry r;
    Joint j;
    j.id = JointId{"j1"};
    j.parent_layer_id = "A";
    j.child_layer_id  = "B";
    j.offset_px = {1.0f, 2.0f};
    j.rotation_deg = 30.0f;

    CHECK(r.empty());
    CHECK(r.add(j));
    CHECK_FALSE(r.empty());
    CHECK(r.size() == 1);
    CHECK(r.get(JointId{"j1"})->parent_layer_id == "A");
    CHECK(r.remove(JointId{"j1"}));
    CHECK_FALSE(r.remove(JointId{"j1"}));
    CHECK(r.empty());
}

TEST_CASE("JointRegistry: adding the same id twice is rejected") {
    JointRegistry r;
    Joint j; j.id = JointId{"dup"}; j.parent_layer_id = "X"; j.child_layer_id = "Y";
    CHECK(r.add(j));
    Joint j2; j2.id = JointId{"dup"}; j2.parent_layer_id = "X"; j2.child_layer_id = "Z";
    CHECK_FALSE(r.add(j2));
    CHECK(r.size() == 1);
}

// ── apply_joints: math ─────────────────────────────────────────────────
TEST_CASE("apply_joints: A->B is offset-only (parent rotation 0)") {
    Scene s = make_chain_scene();
    JointRegistry r;
    Joint j; j.id = JointId{"AB"}; j.parent_layer_id = "A"; j.child_layer_id = "B";
    j.offset_px = {100.0f, 50.0f}; j.rotation_deg = 0.0f;
    r.add(j);
    auto res = apply_joints(s, r);
    CHECK(res.applied == 1);
    CHECK(res.skipped_missing == 0);
    CHECK(res.cycle_ids.empty());
    CHECK(near(s.layers()[1].transform.position.x, 100.0f));
    CHECK(near(s.layers()[1].transform.position.y, 50.0f));
}

TEST_CASE("apply_joints: parent rotation rotates the offset") {
    Scene s = make_chain_scene();
    // Parent A: rotate by 90deg. offset (1, 0) -> child lands at (0, 1)
    s.layers()[0].transform.rotation.z = 90.0f;

    JointRegistry r;
    Joint j; j.id = JointId{"AB"}; j.parent_layer_id = "A"; j.child_layer_id = "B";
    j.offset_px = {1.0f, 0.0f}; j.rotation_deg = 0.0f;
    r.add(j);
    auto res = apply_joints(s, r);
    CHECK(res.applied == 1);
    CHECK(near(s.layers()[1].transform.position.x, 0.0f));
    CHECK(near(s.layers()[1].transform.position.y, 1.0f));
}

TEST_CASE("apply_joints: chained A->B then B->C propagates") {
    Scene s = make_chain_scene();
    JointRegistry r;
    Joint j1; j1.id = JointId{"AB"}; j1.parent_layer_id = "A"; j1.child_layer_id = "B";
    j1.offset_px = {10.0f, 0.0f}; j1.rotation_deg = 0.0f;
    Joint j2; j2.id = JointId{"BC"}; j2.parent_layer_id = "B"; j2.child_layer_id = "C";
    j2.offset_px = {0.0f, 5.0f}; j2.rotation_deg = 0.0f;
    r.add(j1);
    r.add(j2);

    auto res = apply_joints(s, r);
    CHECK(res.applied == 2);
    CHECK(res.cycle_ids.empty());
    // B ends up at A(0,0) + (10, 0)  = (10, 0)
    CHECK(near(s.layers()[1].transform.position.x, 10.0f));
    CHECK(near(s.layers()[1].transform.position.y, 0.0f));
    // C ends up at B(10, 0) + (0, 5) = (10, 5)
    CHECK(near(s.layers()[2].transform.position.x, 10.0f));
    CHECK(near(s.layers()[2].transform.position.y, 5.0f));
}

TEST_CASE("apply_joints: rotation chains additively through parents") {
    Scene s = make_chain_scene();
    JointRegistry r;
    Joint j1; j1.id = JointId{"AB"}; j1.parent_layer_id = "A"; j1.child_layer_id = "B";
    j1.offset_px = {1.0f, 0.0f}; j1.rotation_deg = 90.0f;
    Joint j2; j2.id = JointId{"BC"}; j2.parent_layer_id = "B"; j2.child_layer_id = "C";
    j2.offset_px = {1.0f, 0.0f}; j2.rotation_deg = 0.0f;
    r.add(j1);
    r.add(j2);
    auto res = apply_joints(s, r);
    CHECK(res.applied == 2);
    // B's rotation = parent (0) + j1.rotation_deg (90) = 90
    CHECK(near(s.layers()[1].transform.rotation.z, 90.0f));
    // C's rotation = B's rotation (90) + j2.rotation_deg (0) = 90
    CHECK(near(s.layers()[2].transform.rotation.z, 90.0f));
}

// ── apply_joints: cycles ───────────────────────────────────────────────
TEST_CASE("apply_joints: self-loop is reported as cycle and not applied") {
    Scene s = make_chain_scene();
    JointRegistry r;
    Joint j; j.id = JointId{"AA"}; j.parent_layer_id = "A"; j.child_layer_id = "A";
    j.offset_px = {99.0f, 99.0f}; j.rotation_deg = 99.0f;
    r.add(j);
    auto res = apply_joints(s, r);
    CHECK(res.applied == 0);
    CHECK(res.cycle_ids.size() == 1);
    CHECK(res.cycle_ids[0].value == "AA");
    // A's original position must be unchanged.
    CHECK(near(s.layers()[0].transform.position.x, 0.0f));
}

TEST_CASE("apply_joints: longer cycle A->B->A only flags the cyclic joint") {
    Scene s = make_chain_scene();
    JointRegistry r;
    Joint j1; j1.id = JointId{"AB"}; j1.parent_layer_id = "A"; j1.child_layer_id = "B";
    j1.offset_px = {5.0f, 0.0f}; j1.rotation_deg = 0.0f;
    Joint j2; j2.id = JointId{"BA"}; j2.parent_layer_id = "B"; j2.child_layer_id = "A";
    j2.offset_px = {0.0f, 0.0f}; j2.rotation_deg = 0.0f;
    r.add(j1);
    r.add(j2);
    auto res = apply_joints(s, r);
    CHECK(res.applied == 0);
    CHECK(res.cycle_ids.size() == 2);
}

// ── apply_joints: missing ──────────────────────────────────────────────
TEST_CASE("apply_joints: unknown parent_layer_id is skipped silently") {
    Scene s = make_chain_scene();
    JointRegistry r;
    Joint j; j.id = JointId{"XX"}; j.parent_layer_id = "missing"; j.child_layer_id = "B";
    j.offset_px = {1.0f, 1.0f}; j.rotation_deg = 0.0f;
    r.add(j);
    auto res = apply_joints(s, r);
    CHECK(res.applied == 0);
    CHECK(res.skipped_missing == 1);
    CHECK(res.cycle_ids.empty());
}

// ── apply_joints: idempotency ──────────────────────────────────────────
TEST_CASE("apply_joints: applying twice yields the same end state") {
    Scene s = make_chain_scene();
    JointRegistry r;
    Joint j; j.id = JointId{"AB"}; j.parent_layer_id = "A"; j.child_layer_id = "B";
    j.offset_px = {7.0f, 3.0f}; j.rotation_deg = 45.0f;
    r.add(j);

    auto r1 = apply_joints(s, r);
    auto state_after_first = s.layers()[1].transform.position;

    auto r2 = apply_joints(s, r);
    auto state_after_second = s.layers()[1].transform.position;

    CHECK(r1.applied == 1);
    CHECK(r2.applied == 1);
    CHECK(near(state_after_first.x,  state_after_second.x));
    CHECK(near(state_after_first.y,  state_after_second.y));
    CHECK(near(state_after_first.z,  state_after_second.z));
}
