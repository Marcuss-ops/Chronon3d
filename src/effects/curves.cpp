// ── curves.cpp — Curve LUT implementation ─────────────────────────────────

#include <chronon3d/effects/curves.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <bit>

namespace chronon3d {

// =============================================================================
// Hash helper
// =============================================================================

// Simple hash combiner (splitmix64-inspired)
static uint64_t hash_combine(uint64_t seed, uint64_t v) {
    seed ^= v + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

uint64_t hash_curve_points(const std::vector<CurvePoint>& points) {
    uint64_t h = 0;
    for (const auto& pt : points) {
        // bit_cast<float→uint32_t> requires same size; promote to uint64_t for combining
        const uint64_t xi = static_cast<uint64_t>(std::bit_cast<uint32_t>(pt.x));
        const uint64_t yi = static_cast<uint64_t>(std::bit_cast<uint32_t>(pt.y));
        h = hash_combine(h, xi);
        h = hash_combine(h, yi);
    }
    return h;
}

// =============================================================================
// Reference evaluation — piecewise linear with HDR extrapolation
// =============================================================================

/// Evaluate control points at x (direct, no LUT). Used as reference.
[[nodiscard]] static float evaluate_curve_reference(float x,
    const std::vector<CurvePoint>& points)
{
    if (points.empty()) return x;  // identity

    // Clamp to first/last point for extrapolation
    if (x <= points.front().x) {
        if (points.size() == 1) return points[0].y;
        // Extrapolate using slope from first segment
        const float dx = points[1].x - points[0].x;
        if (std::abs(dx) < 1.0e-10f) return points[0].y;
        const float slope = (points[1].y - points[0].y) / dx;
        return points[0].y + (x - points[0].x) * slope;
    }

    if (x >= points.back().x) {
        if (points.size() == 1) return points[0].y;
        const float dx = points.back().x - points[points.size() - 2].x;
        if (std::abs(dx) < 1.0e-10f) return points.back().y;
        const float slope = (points.back().y - points[points.size() - 2].y) / dx;
        return points.back().y + (x - points.back().x) * slope;
    }

    // Binary search for the segment containing x
    std::size_t lo = 0, hi = points.size() - 1;
    while (hi - lo > 1) {
        const std::size_t mid = (lo + hi) / 2;
        if (x < points[mid].x)
            hi = mid;
        else
            lo = mid;
    }

    // Linear interpolation within the segment
    const float dx = points[hi].x - points[lo].x;
    if (std::abs(dx) < 1.0e-10f) return points[lo].y;
    const float t = (x - points[lo].x) / dx;
    return points[lo].y * (1.0f - t) + points[hi].y * t;
}

// =============================================================================
// CompiledCurve
// =============================================================================

CompiledCurve::CompiledCurve(const std::vector<CurvePoint>& points)
    : m_is_identity(true)
{
    // Copy and sort by x to ensure robustness
    std::vector<CurvePoint> sorted = points;
    std::sort(sorted.begin(), sorted.end(),
        [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

    m_hash = hash_curve_points(sorted);

    // Build LUT: for each entry i, evaluate at x = i / 255.0f
    // For HDR values outside [0, 1], the evaluate function extrapolates.
    for (int i = 0; i < 256; ++i) {
        const float x = static_cast<float>(i) / 255.0f;
        m_lut[i] = evaluate_curve_reference(x, sorted);
        if (std::abs(m_lut[i] - x) > 1.0e-7f)
            m_is_identity = false;
    }
}

float CompiledCurve::evaluate(float x) const {
    // Clamp to LUT range for interpolation; extrapolate for HDR
    if (x <= 0.0f) {
        if (m_lut[0] == 0.0f) return x;  // identity-like at low end
        const float slope = (m_lut[1] - m_lut[0]) * 255.0f;
        return m_lut[0] + x * slope;
    }
    if (x >= 1.0f) {
        if (m_lut[255] == 1.0f && m_is_identity) return x;
        const float slope = (m_lut[255] - m_lut[254]) * 255.0f;
        return m_lut[255] + (x - 1.0f) * slope;
    }

    // Linear interpolation between LUT entries
    const float fi = x * 255.0f;
    const int i0 = static_cast<int>(fi);
    const int i1 = std::min(i0 + 1, 255);
    const float t = fi - static_cast<float>(i0);

    return m_lut[i0] * (1.0f - t) + m_lut[i1] * t;
}

// =============================================================================
// CurveCache
// =============================================================================

std::shared_ptr<const CompiledCurve> CurveCache::get_or_compile(
    const std::vector<CurvePoint>& points)
{
    const uint64_t h = hash_curve_points(points);

    // Linear search through cache (small cache, single-threaded)
    for (const auto& entry : m_cache) {
        if (entry.hash == h) {
            return entry.curve;
        }
    }

    // Compile new curve
    auto curve = std::make_shared<const CompiledCurve>(points);
    m_cache.push_back({h, curve});
    return curve;
}

// =============================================================================
// Global curve cache
// =============================================================================

CurveCache& global_curve_cache() {
    static CurveCache cache;
    return cache;
}

} // namespace chronon3d
