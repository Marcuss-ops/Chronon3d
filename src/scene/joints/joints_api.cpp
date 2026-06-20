// joints_api.cpp — Implementation of JointRegistry + apply_joints.
//
// Topological sort over the joint DAG (Kahn-ish): joints whose parent is
// not the child of any other joint are processed first; once applied, the
// child becomes a "fresh" parent for downstream joints.
//
// Self-loops (parent_layer_id == child_layer_id) and cycles (joints that
// remain with non-zero in-degree after the sort) are reported via
// JointApplyResult::cycle_ids and NOT applied.
//
// Lookup: std::string_view over std::pmr::string is well-defined and
// copy-free (vs std::string(c_str())).

#include <chronon3d/scene/joints/joints_api.hpp>

#include <chronon3d/scene/model/layer/layer.hpp>

#include <algorithm>
#include <cmath>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::joints {

// ── JointRegistry primitives ──────────────────────────────────────────
bool JointRegistry::add(Joint j) {
    if (j.id.value.empty()) return false;
    auto [it, inserted] = joints_.try_emplace(j.id, std::move(j));
    (void)it;
    return inserted;
}

bool JointRegistry::remove(const JointId& id) {
    return joints_.erase(id) > 0;
}

std::optional<Joint> JointRegistry::get(const JointId& id) const {
    auto it = joints_.find(id);
    if (it == joints_.end()) return std::nullopt;
    return it->second;
}

// ── apply_joints: linear-time over joints, BFS over the DAG ───────────
JointApplyResult apply_joints(Scene& scene, const JointRegistry& reg) {
    JointApplyResult result;

    if (reg.empty()) return result;

    auto& layers = scene.layers();
    if (layers.empty()) {
        // Joints referenced non-existent layers; nothing to do.
        result.skipped_missing = static_cast<size_t>(reg.size());
        return result;
    }

    // ── (1) layer name -> index lookup (string_view, copy-free)
    std::unordered_map<std::string_view, size_t> layer_by_name;
    layer_by_name.reserve(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        // std::string_view compares transparently against std::string and
        // views std::pmr::string without a copy. Faster than
        // std::string(c_str()) per layer; correctness identical (no NUL-
        // embedded allocation behaviour for std::pmr::string).
        layer_by_name.emplace(
            std::string_view(layers[i].name.c_str(), layers[i].name.size()), i);
    }

    // ── (2) Filter out self-loops, missing layers, and capture valid joints
    struct ValidJoint {
        size_t    valid_index;  // index in `valid`
        size_t    parent_lidx;
        size_t    child_lidx;
        JointId   id;
        Vec2      offset_px;
        f32       rotation_deg;
    };
    std::vector<ValidJoint> valid;
    valid.reserve(reg.size());

    for (const auto& [jid, j] : reg.all()) {
        if (j.parent_layer_id == j.child_layer_id) {
            result.cycle_ids.push_back(jid);
            continue;
        }
        const auto p_it = layer_by_name.find(j.parent_layer_id);
        const auto c_it = layer_by_name.find(j.child_layer_id);
        if (p_it == layer_by_name.end() || c_it == layer_by_name.end()) {
            ++result.skipped_missing;
            continue;
        }
        ValidJoint vj;
        vj.valid_index  = valid.size();
        vj.parent_lidx  = p_it->second;
        vj.child_lidx   = c_it->second;
        vj.id           = jid;
        vj.offset_px    = j.offset_px;
        vj.rotation_deg = j.rotation_deg;
        valid.push_back(vj);
    }

    if (valid.empty()) return result;

    // ── (3) Prerequisite edges: a joint j is "ready" once all upstream
    //         joints whose child is j's parent have been applied.
    //         Length-2 and longer cycles: a joint whose parent is also
    //         a child of another joint depends on that joint being
    //         processed first — and if the dependency chain closes back,
    //         the in_degree never reaches 0.
    std::vector<int> in_degree(valid.size(), 0);
    for (size_t a = 0; a < valid.size(); ++a) {
        const size_t parent_of_a = valid[a].parent_lidx;
        for (size_t b = 0; b < valid.size(); ++b) {
            if (a == b) continue;
            if (valid[b].child_lidx == parent_of_a) {
                ++in_degree[a];
            }
        }
    }

    // ── (4) Topological order: process joints with in_degree 0 first;
    //         decrement dependents after each apply.
    std::queue<size_t> ready;
    for (size_t i = 0; i < valid.size(); ++i) {
        if (in_degree[i] == 0) ready.push(i);
    }

    std::vector<size_t> ordered;
    ordered.reserve(valid.size());
    while (!ready.empty()) {
        const size_t ji = ready.front();
        ready.pop();
        ordered.push_back(ji);
        const size_t applied_child_lidx = valid[ji].child_lidx;
        for (size_t k = 0; k < valid.size(); ++k) {
            if (k == ji) continue;
            if (valid[k].parent_lidx == applied_child_lidx) {
                if (--in_degree[k] == 0) ready.push(k);
            }
        }
    }

    // ── (5) Any joint still with non-zero in-degree lives in a cycle.
    //         Self-loops were already pushed in step (2) and never reach
    //         `valid`, so cycle_ids stays free of duplicates.
    for (size_t i = 0; i < valid.size(); ++i) {
        if (in_degree[i] > 0) {
            result.cycle_ids.push_back(valid[i].id);
        }
    }

    // ── (6) Apply the joints in topological order.
    std::unordered_set<size_t> cycle_lidx_mask;
    for (size_t i = 0; i < valid.size(); ++i) {
        if (in_degree[i] > 0) cycle_lidx_mask.insert(i);
    }
    for (size_t ji : ordered) {
        if (cycle_lidx_mask.count(ji) > 0) continue;
        const ValidJoint& vj = valid[ji];
        Layer& parent = layers[vj.parent_lidx];
        Layer& child  = layers[vj.child_lidx];

        // 2D rotation about Z, parent pose drives child pose.
        const f32 a_deg = parent.transform.rotation.z;
        const f32 a = a_deg * 3.14159265358979f / 180.0f;
        const f32 cos_a = std::cos(a);
        const f32 sin_a = std::sin(a);

        const Vec2 rotated{
            vj.offset_px.x * cos_a - vj.offset_px.y * sin_a,
            vj.offset_px.x * sin_a + vj.offset_px.y * cos_a
        };

        child.transform.position.x = parent.transform.position.x + rotated.x;
        child.transform.position.y = parent.transform.position.y + rotated.y;
        // z untouched (depth-stable 2-D joint).
        child.transform.rotation.z = a_deg + vj.rotation_deg;

        ++result.applied;
    }

    return result;
}

} // namespace chronon3d::joints
