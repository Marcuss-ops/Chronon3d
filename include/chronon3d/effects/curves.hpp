#pragma once

// ── curves.hpp — Curve LUT, evaluation, hashing and cache ─────────────────
//
// Curve: piecewise-linear interpolation through control points with
//        HDR extrapolation beyond the first/last point via slope extension.
//
// CompiledCurve: 256-entry LUT pre-computed from a set of control points
//                for O(1) evaluation with interpolation.

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace chronon3d {

// ── CurvePoint ──────────────────────────────────────────────────────────────
// Defined in effect_params.hpp — included for convenience.
using CurvePoint = ::chronon3d::CurvePoint;

// ── CompiledCurve ───────────────────────────────────────────────────────────
// A 256-entry LUT pre-computed from control points.
// Uses linear interpolation between entries for sub-LUT precision.

class CompiledCurve {
public:
    CompiledCurve() = default;

    /// Compile control points into a 256-entry LUT.
    /// Points must be sorted by x (strictly increasing).
    /// Empty points → identity curve.
    explicit CompiledCurve(const std::vector<CurvePoint>& points);

    /// Evaluate the curve at x (HDR-safe, extrapolates at edges).
    [[nodiscard]] float evaluate(float x) const;

    /// Access the raw LUT entry at index i (0..255).
    [[nodiscard]] float lut_entry(std::size_t i) const { return m_lut[i]; }

    /// Check if this curve is identity (no-op).
    [[nodiscard]] bool is_identity() const { return m_is_identity; }

    /// Hash the control points for caching.
    [[nodiscard]] uint64_t hash() const { return m_hash; }

    bool operator==(const CompiledCurve& other) const { return m_hash == other.m_hash; }
    bool operator!=(const CompiledCurve& other) const { return m_hash != other.m_hash; }

private:
    float m_lut[256]{};
    bool  m_is_identity{true};
    uint64_t m_hash{0};
};

// ── CurveCache ──────────────────────────────────────────────────────────────
// Thread-safe LRU cache backed by the common LruCache primitive.
// Reuses CompiledCurve objects for identical control point sets.
// Capacity: 256 entries (Count mode).

class CurveCache {
public:
    CurveCache() : m_cache(256) {}

    /// Get or compile a curve from control points.
    /// Returns a shared pointer to the compiled curve (may be cached).
    [[nodiscard]] std::shared_ptr<const CompiledCurve> get_or_compile(
        const std::vector<CurvePoint>& points);

    /// Clear all cached curves.
    void clear() { m_cache.clear(); }

private:
    cache::LruCache<uint64_t, std::shared_ptr<const CompiledCurve>> m_cache;
};

// ── Hash helper ─────────────────────────────────────────────────────────────

/// Hash a set of control points for curve identity comparison.
[[nodiscard]] uint64_t hash_curve_points(const std::vector<CurvePoint>& points);

/// Globally accessible curve cache (thread-safe single-threaded assumption).
/// Reuses CompiledCurve objects across all dispatch paths (processor + stack).
[[nodiscard]] CurveCache& global_curve_cache();

} // namespace chronon3d
