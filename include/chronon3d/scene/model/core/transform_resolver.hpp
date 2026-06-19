#pragma once

#include <chronon3d/scene/model/shape/transform_3d.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

namespace chronon3d {

struct ResolvedTransform3D {
    Transform3D local;
    Mat4 world_matrix{1.0f};
    bool cycle_detected{false};
    bool parent_missing{false};
};

/// Result of hierarchical transform resolution, storing resolved matrices.
struct TransformResolverResult {
    std::unordered_map<std::string, ResolvedTransform3D> resolved;

    [[nodiscard]] std::optional<Mat4> world_matrix(const std::string& name) const {
        auto it = resolved.find(name);
        if (it != resolved.end()) {
            return it->second.world_matrix;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Vec3> world_position(const std::string& name) const {
        auto it = resolved.find(name);
        if (it != resolved.end()) {
            const Mat4& m = it->second.world_matrix;
            return Vec3(m[3]); // Translation is the 4th column
        }
        return std::nullopt;
    }

    /// Conservative: returns true if `name` could resolve to a layer whose
    /// world transform changes across frames (animated parent or self).
    /// Conservative fallback is acceptable when the resolver snapshot does
    /// not track per-layer animation metadata.
    [[nodiscard]] bool is_time_dependent(const std::string& name) const noexcept {
        return resolved.find(name) != resolved.end();
    }

    /// Returns a stable fingerprint of the layer's world transform that
    /// callers can include in a cache key.  Uses the upper-left 3x3 (basis)
    /// and the translation column; equivalent transforms (translation + 90°
    /// rotation) produce distinct fingerprints so consumers can detect
    /// changes between snapshots.
    [[nodiscard]] std::uint64_t world_transform_fingerprint(const std::string& name) const noexcept {
        auto it = resolved.find(name);
        if (it == resolved.end()) return 0;
        const Mat4& m = it->second.world_matrix;
        // Mix the 4x4 entries into a 64-bit hash.  Bad-quality but
        // deterministic and adequate for cache invalidation.
        std::uint64_t fp = 0x9E3779B97F4A7C15ULL;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                const std::uint32_t bits = static_cast<std::uint32_t>(
                    std::bit_cast<std::uint32_t>(m[col][row]));
                fp ^= bits + (fp << 6) + (fp >> 2);
            }
        }
        return fp;
    }
};

/// Resolves a hierarchy of 3D transforms.
/// Handles cycle detection and selective inheritance (position, rotation, scale).
class TransformResolver3D {
public:
    struct InputNode {
        std::string name;
        Transform3D transform;
    };

    /// Resolves the list of input nodes, returning resolved transforms mapping from name to ResolvedTransform3D.
    [[nodiscard]] static std::unordered_map<std::string, ResolvedTransform3D> resolve(
        const std::vector<InputNode>& nodes
    );
};

struct SceneTransformNode {
    std::string name;
    Transform3D local;
    bool renderable{false};
    bool diagnostic_only{false};
};

class SceneTransformRegistry {
public:
    void add_node(std::string name, Transform3D transform, bool renderable, bool diagnostic_only = false) {
        nodes.push_back({std::move(name), std::move(transform), renderable, diagnostic_only});
    }

    [[nodiscard]] const SceneTransformNode* find(std::string_view name) const {
        for (const auto& n : nodes) {
            if (n.name == name) {
                return &n;
            }
        }
        return nullptr;
    }

    [[nodiscard]] TransformResolverResult resolve_all() const {
        std::vector<TransformResolver3D::InputNode> inputs;
        inputs.reserve(nodes.size());
        for (const auto& n : nodes) {
            inputs.push_back({n.name, n.local});
        }
        TransformResolverResult res;
        res.resolved = TransformResolver3D::resolve(inputs);
        return res;
    }

    [[nodiscard]] const std::vector<SceneTransformNode>& get_nodes() const {
        return nodes;
    }

private:
    std::vector<SceneTransformNode> nodes;
};

} // namespace chronon3d
