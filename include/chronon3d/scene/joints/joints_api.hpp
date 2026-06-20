// joints_api.hpp — Public joints API for layer-to-layer attachment.
//
// Joints describe a parent -> child relationship between two named layers.
// Pure additive (incompatible with no change to Scene ABI). Apply via the
// free function `chronon3d::joints::apply_joints(scene, registry)`.
//
// Conventions:
//   * Joints are 2D-joints: rotation only about Z (degrees). Apply: child
//     pose = parent pose rotated by parent.rotation.z, then translated by
//     joint.offset_px, then rotated by joint.rotation_deg.
//   * Layer names match Scene::layers()[i].name (a std::pmr::string).
//     Lookup uses std::string comparisons against the names.
//   * Cycles (parent==child OR longer cycles) are reported in the result,
//     NOT applied.
//
// Scope: this is the public surface only. Stores (e.g. a Scene::joints()
// field) and integration into the LayoutSolver pipeline are out of scope
// for the v1.0 freeze; apply_joints() can be called explicitly when the
// caller needs joint behaviour, before or after LayoutSolver::solve().

#pragma once

#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::joints {

// ── JointId: opaque, value-typed handle for joints ────────────────────
struct JointId {
    std::string value;

    JointId() = default;
    explicit JointId(std::string v) : value(std::move(v)) {}

    // Default std::equal_to<JointId> relies on operator==; define it here so
    // the default hashmap / unordered_set usage in joints_api.cpp compiles.
    [[nodiscard]] bool operator==(const JointId& other) const noexcept {
        return value == other.value;
    }
    [[nodiscard]] bool operator!=(const JointId& other) const noexcept {
        return value != other.value;
    }
};

struct JointIdHash {
    size_t operator()(const JointId& j) const noexcept {
        return std::hash<std::string>{}(j.value);
    }
};

struct JointIdEq {
    bool operator()(const JointId& a, const JointId& b) const noexcept {
        return a.value == b.value;
    }
};

// ── Joint: parent -> child with 2D offset and Z-rotation delta ─────────
struct Joint {
    JointId  id;
    std::string parent_layer_id;
    std::string child_layer_id;
    Vec2       offset_px{0.0f, 0.0f};
    f32        rotation_deg{0.0f};
};

// ── JointRegistry: bag of uniquely-id'd joints ────────────────────────
class JointRegistry {
public:
    /// Add a joint; returns false if a joint with the same id already exists.
    bool add(Joint j);

    /// Remove a joint by id; returns true if removed.
    bool remove(const JointId& id);

    /// Look up a joint by id.
    [[nodiscard]] std::optional<Joint> get(const JointId& id) const;

    /// All joints (unordered, do not mutate through this view).
    [[nodiscard]] const std::unordered_map<JointId, Joint, JointIdHash>&
    all() const noexcept { return joints_; }

    [[nodiscard]] size_t size()  const noexcept { return joints_.size();  }
    [[nodiscard]] bool   empty() const noexcept { return joints_.empty(); }

    void clear() noexcept { joints_.clear(); }

private:
    std::unordered_map<JointId, Joint, JointIdHash> joints_;
};

// ── JointApplyResult: declared BEFORE apply_joints so the free function
//    below has a complete return type at its declaration site.
struct JointApplyResult {
    size_t applied{0};
    size_t skipped_missing{0};
    std::vector<JointId> cycle_ids;
};

// ── apply_joints: idempotent. Mutates `scene.layers()` in place. ──────
//
// Returns a result describing the application:
//   * applied:           # of joints whose math was applied to a child.
//   * skipped_missing:   # of joints referencing a missing layer id.
//   * cycle_ids:         self-loops and longer cycles are reported here;
//                        apply_joints() does NOT apply cycle joints.
//
// Math:
//   child.transform.position = parent.position + R(parent.rotation.z) * offset_px
//   child.transform.rotation.z = parent.rotation.z + joint.rotation_deg
//   child.transform.position.z / rotation.x / rotation.y are NOT touched.
//   (Joints are intentionally 2-D; depth-stable for the freeze.)
JointApplyResult apply_joints(Scene& scene, const JointRegistry& reg);

} // namespace chronon3d::joints
