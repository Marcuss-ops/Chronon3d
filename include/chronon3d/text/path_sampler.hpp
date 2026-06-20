#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// path_sampler.hpp — Arc-length parameterised path sampler
//
// PR 4 of the text pipeline.  Compiles a PathShape into a table of
// arc-length-indexed points so that consumers (text-on-path, trim-path,
// motion-path) can efficiently query position, tangent, and normal at any
// distance along the curve without re-evaluating the Bézier segments.
//
// Curve flattening uses recursive De Casteljau subdivision with a
// configurable flatness tolerance (default 0.25 px).
//
// Usage:
//   auto sampler = PathSampler::compile(my_path_shape, 0.25f);
//   float total = sampler.total_length();
//   auto s = sampler.sample_distance(42.0f);
//   // s.position = Vec2(…), s.tangent = Vec2(…), s.normal = Vec2(…)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

#include <cstddef>
#include <vector>

namespace chronon3d {

// ── PathSample — result of sampling at a given distance ─────────────────

struct PathSample {
    /// World-space position on the path.
    Vec2 position{0.0f, 0.0f};

    /// Unit tangent vector (direction of travel).
    Vec2 tangent{1.0f, 0.0f};

    /// Unit normal vector.  Computed as a 90° clockwise rotation of the
    /// tangent: `(tangent.y, -tangent.x)`.  In screen coordinates (y-down)
    /// this points downward for a left-to-right path.  Negate for an
    /// upward-pointing normal (useful for baseline offset in text-on-path).
    Vec2 normal{0.0f, -1.0f};

    /// Distance from the start of the path (pixels).
    float distance{0.0f};

    /// Normalised parameter 0..1 (distance / total_length).
    /// Undefined when total_length == 0 (set to 0).
    float normalized_t{0.0f};
};

// ── PathSampler — compiled arc-length table ─────────────────────────────

class PathSampler {
public:
    /// Compile a PathShape into an arc-length sampler.
    ///
    /// Flattens all Bézier curves into line segments using recursive
    /// De Casteljau subdivision, builds an arc-length lookup table,
    /// and makes it available for O(log n) distance-based queries.
    ///
    /// @param path               The path shape to compile.
    /// @param flatness_tolerance Maximum allowed deviation between the chord
    ///                           and the true curve (pixels).  0.25 px is a
    ///                           good default for screen-space paths.
    /// @return A sampler ready for distance queries.
    [[nodiscard]] static PathSampler compile(
        const PathShape& path,
        float flatness_tolerance = 0.25f
    );

    /// Total arc length of the path in pixels.
    [[nodiscard]] float total_length() const noexcept { return m_total_length; }

    /// Sample the path at a given distance from the start.
    ///
    /// @param distance  Arc-length distance in pixels.  Clamped to [0, total_length].
    /// @return PathSample with position, tangent, normal, and normalised t.
    [[nodiscard]] PathSample sample_distance(float distance) const;

    /// Sample the path at a normalised parameter t ∈ [0, 1].
    [[nodiscard]] PathSample sample_normalized(float t) const;

    /// Number of sampling points in the arc-length table.
    [[nodiscard]] size_t point_count() const noexcept { return m_samples.size(); }

private:
    PathSampler() = default;

    struct ArcLengthPoint {
        float cumulative_distance{0.0f};
        Vec2 position{0.0f, 0.0f};
        Vec2 tangent{1.0f, 0.0f};
    };

    std::vector<ArcLengthPoint> m_samples;
    float m_total_length{0.0f};
};

} // namespace chronon3d
