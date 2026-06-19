#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/transform.hpp>
#include <vector>
#include <optional>
#include <unordered_map>
#include <string>
#include <string_view>
#include <span>
#include <cstddef>
#include <cstdint>

namespace chronon3d {

// Forward declaration — Layer is defined in scene/model/layer/layer.hpp.
// This canonical header is the single source of truth for hierarchical
// transform resolution; no other transform-resolution headers are needed.
class Layer;

// ── Input: flat array of nodes with parent references ──────────────────────

struct HierarchyNodeView {
    std::size_t id{0};                   // index in the input array
    std::optional<std::size_t> parent;   // parent index (nullopt = root)
    Mat4 local_matrix{1.0f};             // local-space transform

    // Selective inheritance (default: inherit all)
    bool inherits_position{true};
    bool inherits_rotation{true};
    bool inherits_scale{true};

    f32 local_opacity{1.0f};             // local opacity (multiplied up the chain)
};

// ── Output: index-aligned results ──────────────────────────────────────────

struct ResolvedNode {
    Mat4 world_matrix{1.0f};             // accumulated world transform
    f32  world_opacity{1.0f};            // accumulated opacity (product of chain)
    bool cycle_detected{false};          // self-referencing or cyclic chain
    bool parent_missing{false};          // parent name not found
};

// ── Unified HierarchyResolver ──────────────────────────────────────────────
//
// Replaces three previously separate implementations:
//   1. `TransformResolver3D::resolve()`        — DFS with selective inheritance
//   2. `LayerHierarchyResolver`                — depth-grouped parallel via TBB
//   3. `Scene::resolve_hierarchy()` / `SceneTransformRegistry`
//
// Algorithm: computes depths first (cycle-safe), groups nodes by depth,
// resolves level-by-level (parallel when TBB is available).  Supports
// selective inheritance via glm::decompose on the parent world matrix.

class HierarchyResolver {
public:
    /// Construct with ownership of the node views.
    explicit HierarchyResolver(std::vector<HierarchyNodeView> nodes);

    /// Resolve all nodes.  Returns index-aligned results.
    [[nodiscard]] std::vector<ResolvedNode> resolve();

    /// Access the resolved node for a given input index.
    /// Must call resolve() first (or resolve_one() for that index).
    [[nodiscard]] const ResolvedNode& resolved(std::size_t index) const;

private:
    enum class VisitState : u8 { Unvisited, Visiting, Visited };

    void compute_depths();
    void resolve_levels();

    /// Resolve a single node (used within resolve_levels and for ad-hoc queries).
    void resolve_one(std::size_t index);

    std::vector<HierarchyNodeView> m_nodes;
    std::vector<ResolvedNode> m_results;
    std::vector<VisitState> m_state;
    std::vector<int> m_depths;
};

// ── Helpers ────────────────────────────────────────────────────────────────

/// Build a name→index map from a layer vector.
/// Used to convert string-based parent references to index-based ones.
[[nodiscard]] std::unordered_map<std::string_view, std::size_t> build_name_index(
    const std::pmr::vector<Layer>& layers
);

// ── Scene-transform convenience API ────────────────────────────────────────
//
// All callers should construct a `ResolvedSceneTransforms` (either by calling
// `resolve_scene_transforms` on a flat input list, or by hand-building via
// `insert(name, world_matrix)` for fixture/test code) and pass it directly to
// the camera / framing / validator APIs that accept
// `const ResolvedSceneTransforms&`.

/// Input row for `resolve_scene_transforms`: a named Transform3D plus a few
/// diagnostic flags carried for backward compatibility with SceneTransformRegistry.
struct SceneTransformInput {
    std::string name;
    Transform3D local;
    bool renderable{false};
    bool diagnostic_only{false};
};

/// Name-keyed wrapper around `HierarchyResolver::ResolvedNode[]`.  Includes
/// parent-name metadata so callers can iterate by index without round-
/// tripping a name lookup.
class ResolvedSceneTransforms {
public:
    ResolvedSceneTransforms() = default;
    /// Three-arg constructor: results + names + parent names (parallel arrays).
    ResolvedSceneTransforms(std::vector<ResolvedNode> resolved,
                            std::vector<std::string> names,
                            std::vector<std::string> parent_names);

    /// Legacy 2-arg constructor for hand-built fixtures with no parent
    /// metadata.  `parent_names` defaults to an empty parallel array.
    explicit ResolvedSceneTransforms(std::pair<
        std::vector<ResolvedNode>,
        std::vector<std::string>> named_results);

    /// Test-fixture builder: append a name/world-matrix pair.
    /// `parent_name` is optional because hand-built fixtures often don't
    /// model parent-child wiring; pass `""` to skip.  When present, the
    /// `m_parent_names` parallel array stays in lock-step with `m_resolved`
    /// so `parent_name_at(i)` returns a real value or `""` — never an
    /// out-of-range throw.
    ResolvedSceneTransforms& insert(std::string name, Mat4 world_matrix,
                                    bool cycle_detected = false,
                                    bool parent_missing = false,
                                    std::string parent_name = std::string{});

    // ── Iteration primitives ────────────────────────────────────────────
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const ResolvedNode& resolved(std::size_t index) const;
    /// Name of the resolved node at `index`.  Useful for iteration /
    /// diagnostics.  Throws on out-of-range, matching `resolved(index)`.
    [[nodiscard]] const std::string& name_at(std::size_t index) const;
    /// Parent name of the node at `index` (empty string when the node was
    /// a root or hand-built without parent metadata).  Parallel to
    /// `name_at(index)` so callers can iterate by index.
    [[nodiscard]] const std::string& parent_name_at(std::size_t index) const;

    // ── Name-based lookups ──────────────────────────────────────────────
    [[nodiscard]] std::optional<Mat4> world_matrix(const std::string& name) const;
    [[nodiscard]] std::optional<Vec3> world_position(const std::string& name) const;
    [[nodiscard]] bool is_time_dependent(const std::string& name) const noexcept;
    [[nodiscard]] std::optional<std::string> parent_name(
        const std::string& name) const;

    /// Stable fingerprint of the matrix at `name`.  0 if not found.
    [[nodiscard]] std::uint64_t world_transform_fingerprint(
        const std::string& name) const noexcept;

private:
    std::vector<ResolvedNode> m_resolved;
    std::vector<std::string> m_names;
    std::vector<std::string> m_parent_names;
    std::unordered_map<std::string, std::size_t> m_name_to_index;
};

/// One-shot helper: builds HierarchyNodeView rows, runs HierarchyResolver,
/// returns a name-keyed wrapper.
[[nodiscard]] ResolvedSceneTransforms resolve_scene_transforms(
    std::span<const SceneTransformInput> inputs
);

} // namespace chronon3d
