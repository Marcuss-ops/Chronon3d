#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <string>
#include <vector>
#include <optional>

namespace chronon3d {

/// A named binding from a camera property to a layer in the transform hierarchy.
///
/// Replaces the ad-hoc std::string fields (target_name, parent_name,
/// focus_target_name) with a unified type that supports:
///   - Single-target binding (name only).
///   - Weighted multi-target blending for target/focus/up.
///   - Empty sentinel (weight = 0 or name empty).
///
/// Usage:
///   CameraBinding target{"hero_card"};              // single target
///   std::vector<CameraBinding> targets{
///       {"hero_card", 0.7f}, {"sidekick", 0.3f}    // blended
///   };
struct CameraBinding {
    std::string name;
    f32 weight{1.0f};

    [[nodiscard]] bool empty() const noexcept { return name.empty(); }

    bool operator==(const CameraBinding&) const = default;
};

/// Resolves CameraBindings to world-space data via a TransformResolverResult.
///
/// Wraps the raw TransformResolverResult pointer (may be nullptr when no
/// resolver is available) and provides:
///   - Single-binding resolve → std::optional<Vec3 / Mat4>.
///   - Multi-binding weighted blend → std::optional<Vec3>.
///   - External-dependency check for camera cache invalidation.
class CameraBindingResolver {
public:
    explicit CameraBindingResolver(const TransformResolverResult* resolved) noexcept
        : m_resolved(resolved) {}

    /// ── Single-binding resolution ──────────────────────────────────────

    [[nodiscard]] std::optional<Vec3> resolve_position(const CameraBinding& binding) const {
        if (!m_resolved || binding.empty()) return std::nullopt;
        return m_resolved->world_position(binding.name);
    }

    [[nodiscard]] std::optional<Mat4> resolve_matrix(const CameraBinding& binding) const {
        if (!m_resolved || binding.empty()) return std::nullopt;
        return m_resolved->world_matrix(binding.name);
    }

    /// ── Weighted multi-target blend ────────────────────────────────────
    ///
    /// Computes (Σ weight_i * pos_i) / Σ weight_i for all resolved bindings.
    /// Returns std::nullopt when no binding resolves.

    [[nodiscard]] std::optional<Vec3> resolve_blend(
        const std::vector<CameraBinding>& bindings) const
    {
        if (!m_resolved || bindings.empty()) return std::nullopt;

        Vec3  blended{0.0f};
        f32   total_weight = 0.0f;
        bool  any_resolved = false;

        for (const auto& b : bindings) {
            if (b.weight <= 0.0f) continue;
            if (auto pos = m_resolved->world_position(b.name)) {
                blended += *pos * b.weight;
                total_weight += b.weight;
                any_resolved = true;
            }
        }

        if (!any_resolved)     return std::nullopt;
        if (total_weight > 0)  blended /= total_weight;
        return blended;
    }

    /// ── External dependency check ──────────────────────────────────────
    ///
    /// Conservative: returns true when the vector is non-empty, because
    /// any referenced layer may have an animated transform.  A future
    /// precise version will inspect per-reference time-dependency.

    [[nodiscard]] bool has_external_dependency(
        const std::vector<CameraBinding>& bindings) const noexcept
    {
        return !bindings.empty();
    }

    [[nodiscard]] bool has_external_dependency(
        const CameraBinding& binding) const noexcept
    {
        return !binding.empty();
    }

private:
    const TransformResolverResult* m_resolved;
};

} // namespace chronon3d
