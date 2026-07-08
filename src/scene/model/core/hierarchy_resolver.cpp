// ============================================================================
// hierarchy_resolver.cpp — Unified HierarchyResolver implementation
//
// Replaces three previously separate hierarchy resolvers:
//   1. `TransformResolver3D::resolve()`        — DFS with selective inheritance
//   2. `LayerHierarchyResolver`                 — depth-grouped parallel via TBB
//   3. `Scene::resolve_hierarchy()` / `SceneTransformRegistry`
// ============================================================================

#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include "../internal/scene_ids.hpp"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <algorithm>
#include <stdexcept>

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

namespace chronon3d {

// ── Constructor ────────────────────────────────────────────────────────────

HierarchyResolver::HierarchyResolver(std::vector<HierarchyNodeView> nodes)
    : m_nodes(std::move(nodes))
    , m_results(m_nodes.size())
    , m_state(m_nodes.size(), VisitState::Unvisited)
    , m_depths(m_nodes.size(), -1)
{}

// ── resolve() ──────────────────────────────────────────────────────────────

std::vector<ResolvedNode> HierarchyResolver::resolve() {
    if (m_nodes.empty()) return m_results;

    compute_depths();
    resolve_levels();
    return m_results;
}

const ResolvedNode& HierarchyResolver::resolved(std::size_t index) const {
    return m_results.at(index);
}

// ── compute_depths() — cycle-safe DFS to determine max depth per node ──────

void HierarchyResolver::compute_depths() {
    std::vector<bool> visiting(m_nodes.size(), false);

    auto get_depth = [&](auto& self, std::size_t idx) -> int {
        if (m_depths[idx] != -1) return m_depths[idx];
        if (visiting[idx]) return 0;  // cycle fallback

        visiting[idx] = true;
        int d = 0;
        const auto& node = m_nodes[idx];
        if (node.parent.has_value()) {
            std::size_t p = *node.parent;
            if (p < m_nodes.size() && p != idx) {
                d = self(self, p) + 1;
            }
        }
        visiting[idx] = false;
        m_depths[idx] = d;
        return d;
    };

    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        get_depth(get_depth, i);
    }
}

// ── resolve_levels() — resolve level-by-level, parallel within each level ──

void HierarchyResolver::resolve_levels() {
    // Find max depth
    int max_d = 0;
    for (int d : m_depths) {
        if (d > max_d) max_d = d;
    }

    // Group by depth
    std::vector<std::vector<std::size_t>> by_depth(static_cast<std::size_t>(max_d + 1));
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_depths[i] >= 0) {
            by_depth[static_cast<std::size_t>(m_depths[i])].push_back(i);
        }
    }

    // Resolve level by level (depth 0 first = roots; depth 1 = children, etc.)
    for (int d = 0; d <= max_d; ++d) {
        const auto& level = by_depth[static_cast<std::size_t>(d)];
        if (level.empty()) continue;

        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, level.size()),
            [&](const tbb::blocked_range<std::size_t>& range) {
                for (std::size_t i = range.begin(); i < range.end(); ++i) {
                    resolve_one(level[i]);
                }
            }
        );
    }
}

// ── resolve_one() — core resolution logic for a single node ────────────────

void HierarchyResolver::resolve_one(std::size_t index) {
    if (m_state[index] == VisitState::Visited) return;

    if (m_state[index] == VisitState::Visiting) {
        // Cycle detected — fall back to local matrix
        m_results[index].cycle_detected = true;
        m_results[index].world_matrix = m_nodes[index].local_matrix;
        m_results[index].world_opacity = m_nodes[index].local_opacity;
        m_state[index] = VisitState::Visited;
        return;
    }

    m_state[index] = VisitState::Visiting;

    const auto& node = m_nodes[index];
    Mat4 world = node.local_matrix;
    f32 opacity = node.local_opacity;

    if (node.parent.has_value()) {
        std::size_t parent_idx = *node.parent;

        if (parent_idx >= m_nodes.size()) {
            m_results[index].parent_missing = true;
        } else if (parent_idx == index) {
            m_results[index].cycle_detected = true;
        } else {
            // Ensure parent is resolved first (should already be resolved
            // since we process level-by-level, but DFS fallback for safety)
            if (m_state[parent_idx] != VisitState::Visited) {
                resolve_one(parent_idx);
            }

            const auto& parent_result = m_results[parent_idx];
            const Mat4& parent_world = parent_result.world_matrix;

            // Selective inheritance via matrix decomposition
            if (!node.inherits_position || !node.inherits_rotation || !node.inherits_scale) {
                Vec3 parent_scale{1.0f};
                Quat parent_rot{1.0f, 0.0f, 0.0f, 0.0f};
                Vec3 parent_trans{0.0f};
                Vec3 parent_skew{0.0f};
                glm::vec4 parent_persp{0.0f};

                if (glm::decompose(parent_world, parent_scale, parent_rot,
                                   parent_trans, parent_skew, parent_persp)) {
                    Vec3 filtered_trans = node.inherits_position ? parent_trans : Vec3(0.0f);
                    Quat filtered_rot  = node.inherits_rotation  ? parent_rot  : Quat(1.0f, 0.0f, 0.0f, 0.0f);
                    Vec3 filtered_scale = node.inherits_scale     ? parent_scale : Vec3(1.0f);

                    Mat4 parent_filtered = glm::translate(Mat4(1.0f), filtered_trans) *
                                           glm::toMat4(filtered_rot) *
                                           glm::scale(Mat4(1.0f), filtered_scale);
                    world = parent_filtered * node.local_matrix;
                } else {
                    world = parent_world * node.local_matrix;
                }
            } else {
                // Full inheritance — simple matrix multiply
                world = parent_world * node.local_matrix;
            }

            // Accumulate opacity up the chain
            opacity = parent_result.world_opacity * node.local_opacity;
        }
    } else if (node.parent_declared) {
        // Parent name was specified but not found during name→index lookup.
        m_results[index].parent_missing = true;
    }

    m_results[index].world_matrix = world;
    m_results[index].world_opacity = opacity;
    m_state[index] = VisitState::Visited;
}

// ── build_name_index helper ────────────────────────────────────────────────

std::unordered_map<std::string_view, LayerID> build_name_index(
    const std::pmr::vector<Layer>& layers
) {
    std::unordered_map<std::string_view, LayerID> map;
    map.reserve(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        std::string_view name(layers[i].name);
        auto [it, inserted] = map.emplace(name, LayerID{i});
        if (!inserted) {
            throw std::runtime_error(
                "Duplicate layer name '" + std::string(name)
                + "' at index " + std::to_string(i)
                + " (previously at index " + std::to_string(it->second.value) + ")");
        }
    }
    return map;
}

// ── ResolvedSceneTransforms ────────────────────────────────────────────────

ResolvedSceneTransforms::ResolvedSceneTransforms(
    std::vector<ResolvedNode> resolved,
    std::vector<std::string> names,
    std::vector<std::string> parent_names
) : m_resolved(std::move(resolved))
  , m_names(std::move(names))
  , m_parent_names(std::move(parent_names))
{
    m_name_to_index.reserve(m_names.size());
    for (std::size_t i = 0; i < m_names.size(); ++i) {
        m_name_to_index.emplace(m_names[i], i);
    }
}

std::size_t ResolvedSceneTransforms::size() const noexcept {
    return m_resolved.size();
}

bool ResolvedSceneTransforms::empty() const noexcept {
    return m_resolved.empty();
}

const ResolvedNode& ResolvedSceneTransforms::resolved(std::size_t index) const {
    return m_resolved.at(index);
}

const std::string& ResolvedSceneTransforms::name_at(std::size_t index) const {
    return m_names.at(index);
}

const std::string& ResolvedSceneTransforms::parent_name_at(std::size_t index) const {
    return m_parent_names.at(index);
}

std::optional<std::string> ResolvedSceneTransforms::parent_name(
    const std::string& name
) const {
    auto it = m_name_to_index.find(name);
    if (it == m_name_to_index.end()) return std::nullopt;
    if (m_parent_names.empty()) return std::nullopt;
    return m_parent_names[it->second];
}

ResolvedSceneTransforms& ResolvedSceneTransforms::insert(
    std::string name,
    Mat4 world_matrix,
    bool cycle_detected,
    bool parent_missing,
    std::string parent_name
) {
    auto it = m_name_to_index.find(name);
    if (it != m_name_to_index.end()) {
        // Update existing entry — preserves insertion order so callers can
        // still rely on index = iteration order before insertion.
        const std::size_t idx = it->second;
        m_resolved[idx].world_matrix = world_matrix;
        m_resolved[idx].cycle_detected = cycle_detected;
        m_resolved[idx].parent_missing = parent_missing;
        // Keep `m_parent_names` parallel to `m_resolved` so `parent_name_at(i)`
        // never throws on out-of-range.  Lazily grow if hand-built fixtures
        // never called the parent-aware path.
        if (m_parent_names.size() <= idx) m_parent_names.resize(idx + 1);
        m_parent_names[idx] = std::move(parent_name);
        return *this;
    }
    const std::size_t idx = m_resolved.size();
    m_resolved.push_back(ResolvedNode{world_matrix, 1.0f, cycle_detected, parent_missing});
    m_names.push_back(std::move(name));
    m_parent_names.push_back(std::move(parent_name));
    m_name_to_index.emplace(m_names.back(), idx);
    return *this;
}

std::optional<Mat4> ResolvedSceneTransforms::world_matrix(const std::string& name) const {
    auto it = m_name_to_index.find(name);
    if (it == m_name_to_index.end()) return std::nullopt;
    return m_resolved[it->second].world_matrix;
}

std::optional<Vec3> ResolvedSceneTransforms::world_position(const std::string& name) const {
    auto it = m_name_to_index.find(name);
    if (it == m_name_to_index.end()) return std::nullopt;
    const Mat4& m = m_resolved[it->second].world_matrix;
    return Vec3(m[3]);  // Translation is the 4th column
}

bool ResolvedSceneTransforms::is_time_dependent(const std::string& name) const noexcept {
    return m_name_to_index.find(name) != m_name_to_index.end();
}

std::uint64_t ResolvedSceneTransforms::world_transform_fingerprint(
    const std::string& name
) const noexcept {
    auto it = m_name_to_index.find(name);
    if (it == m_name_to_index.end()) return 0;
    const Mat4& m = m_resolved[it->second].world_matrix;
    // Mix the 4x4 entries into a 64-bit hash.  Bad-quality but deterministic
    // and adequate for cache invalidation (matches the prior impl).
    std::uint64_t fp = 0x9E3779B97F4A7C15ULL;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(m[col][row]);
            fp ^= bits + (fp << 6) + (fp >> 2);
        }
    }
    return fp;
}

// ── resolve_scene_transforms helper ────────────────────────────────────────

ResolvedSceneTransforms resolve_scene_transforms(
    std::span<const SceneTransformInput> inputs
) {
    if (inputs.empty()) {
        return ResolvedSceneTransforms({}, {}, {});
    }

    // Two-pass build: first record names so a child's parent_name can be
    // resolved to an index.  Forward references to names not yet seen would
    // be a user error (cycles are handled by HierarchyResolver downstream).
    std::vector<std::string> names;
    names.reserve(inputs.size());
    for (const auto& i : inputs) {
        names.push_back(i.name);
    }

    std::unordered_map<std::string, LayerID> name_to_idx;
    name_to_idx.reserve(inputs.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        name_to_idx.emplace(names[i], LayerID{i});
    }

    std::vector<HierarchyNodeView> views;
    views.reserve(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& in = inputs[i];
        HierarchyNodeView v;
        v.id = i;
        if (!in.local.parent_name.empty()) {
            v.parent_declared = true;
            auto pit = name_to_idx.find(in.local.parent_name);
            if (pit != name_to_idx.end()) {
                v.parent = pit->second.value;
            }
            // Missing parent (parent_declared=true, parent=nullopt)
            // → HierarchyResolver marks `parent_missing` in resolve_one().
        }
        v.local_matrix = in.local.to_mat4();
        v.inherits_position = in.local.inherits_position;
        v.inherits_rotation = in.local.inherits_rotation;
        v.inherits_scale    = in.local.inherits_scale;
        // v.local_opacity defaults to 1.0f; matches previous behaviour where
        // the resolver didn't track per-row opacity.
        views.push_back(v);
    }

    std::vector<std::string> parent_names;
    parent_names.reserve(inputs.size());
    for (const auto& in : inputs) {
        parent_names.push_back(in.local.parent_name);
    }

    HierarchyResolver resolver(std::move(views));
    auto resolved = resolver.resolve();
    return ResolvedSceneTransforms(
        std::move(resolved),
        std::move(names),
        std::move(parent_names)
    );
}
} // namespace chronon3d
