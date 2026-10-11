#pragma once

// ============================================================================
// morph_normalize.hpp — automatic topology normalization for path morphing.
//
// AE parity: After Effects can morph between arbitrary SVG shapes. Chronon's
// existing MorphPathParams engine requires identical command topology
// (see validate_morph_path_topology). This header does NOT rewrite that
// engine: it converts any two PathShapes (circle, rect, triangle, arbitrary
// SVG lowered to PathCommands) into a compatible pair that passes
// validation, by uniform arc-length resampling + cyclic-shift / winding
// alignment.
//
// Headless / deterministic contract (matches path_operators.cpp):
//   - pure functions of inputs only; no clock, no RNG, no global state.
//   - finite-coordinate validation; throws std::invalid_argument on abuse.
//   - geometry cap: samples in [3, 4096], total commands bounded.
//   - random-access: normalized pair can be interpolated at any
//     progress in [0,1] via existing morph_path / MorphPathParams.
//
// Usage:
//   auto pair = make_morph_compatible_pair(shapeA, shapeB, 128);
//   // pair.first.commands and pair.second.commands now share topology.
//   MorphPathParams params{pair.second.commands, 0.5f};
//   auto mid = apply_path_operators(pair.first, {{PathOperatorKind::Morph, params, 0}});
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chronon3d::morph {

// ── Limits ────────────────────────────────────────────────────────────────
inline constexpr std::size_t kMinMorphSamples = 3;
inline constexpr std::size_t kMaxMorphSamples = 4096;
inline constexpr std::size_t kMaxFlattenSegments = 32;

namespace detail {

[[nodiscard]] inline bool finite_point(Vec2 p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

// Concatenate all flattened subpaths into a single outline. Multi-contour
// inputs (e.g. SVG with holes) are joined in command order; callers needing
// per-contour correspondence should normalize contour-by-contour.
[[nodiscard]] inline std::vector<Vec2> outline_points(const PathShape& path) {
    const auto subpaths = math::flatten_path(path, 24);
    std::vector<Vec2> out;
    for (const auto& sub : subpaths) {
        if (sub.size() < 2)
            continue;
        if (!out.empty()) {
            // Avoid a zero-length stitch when contours touch.
            if (glm::length(sub.front() - out.back()) <= 1e-6f) {
                out.insert(out.end(), sub.begin() + 1, sub.end());
                continue;
            }
        }
        out.insert(out.end(), sub.begin(), sub.end());
    }
    // Drop a duplicated closing point; closed-ness is tracked separately.
    if (out.size() > 1 && glm::length(out.front() - out.back()) <= 1e-6f)
        out.pop_back();
    return out;
}

[[nodiscard]] inline bool path_is_closed(const PathShape& path) {
    if (path.closed)
        return true;
    for (const auto& c : path.commands)
        if (c.type == PathCommandType::Close)
            return true;
    return false;
}

// Uniform arc-length resample of a point loop / polyline to N samples.
[[nodiscard]] inline std::vector<Vec2> resample_uniform(
    const std::vector<Vec2>& points, std::size_t samples, bool closed) {
    std::vector<Vec2> out;
    out.reserve(samples);
    if (points.empty() || samples == 0)
        return out;
    if (points.size() == 1) {
        out.assign(samples, points.front());
        return out;
    }
    // Cumulative lengths.
    std::vector<double> cum(points.size(), 0.0);
    double total = 0.0;
    const std::size_t segs = points.size() - 1 + (closed ? 1 : 0);
    for (std::size_t i = 0; i < segs; ++i) {
        const Vec2 a = points[i % points.size()];
        const Vec2 b = points[(i + 1) % points.size()];
        total += static_cast<double>(glm::length(b - a));
        if (i + 1 < cum.size())
            cum[i + 1] = total;
    }
    if (!(total > 1e-9)) {
        out.assign(samples, points.front());
        return out;
    }
    auto point_at = [&](double d) {
        d = std::clamp(d, 0.0, total);
        // Locate segment.
        std::size_t lo = 0, hi = points.size() - 1;
        // Linear scan is fine for N <= 4096; keeps float behavior obvious.
        std::size_t seg = 0;
        for (std::size_t i = 0; i + 1 < cum.size(); ++i) {
            if (cum[i + 1] >= d) {
                seg = i;
                break;
            }
            seg = i;
        }
        (void)lo;
        (void)hi;
        const Vec2 a = points[seg % points.size()];
        const Vec2 b = points[(seg + 1) % points.size()];
        const double seg_start = cum[seg];
        const double seg_end =
            (seg + 1 < cum.size()) ? cum[seg + 1] : total;
        const double span = seg_end - seg_start;
        const double t = span > 1e-12 ? (d - seg_start) / span : 0.0;
        const float ft = static_cast<float>(std::clamp(t, 0.0, 1.0));
        return Vec2{a.x + (b.x - a.x) * ft, a.y + (b.y - a.y) * ft};
    };
    for (std::size_t i = 0; i < samples; ++i) {
        const double d =
            closed ? total * static_cast<double>(i) / static_cast<double>(samples)
                   : total * static_cast<double>(i) /
                         static_cast<double>(samples - 1);
        out.push_back(point_at(d));
    }
    return out;
}

[[nodiscard]] inline double alignment_cost(
    const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
    double cost = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double dx = static_cast<double>(a[i].x) - static_cast<double>(b[i].x);
        const double dy = static_cast<double>(a[i].y) - static_cast<double>(b[i].y);
        cost += dx * dx + dy * dy;
    }
    return cost;
}

// Best cyclic shift (+ optional reversal) of `b` against fixed `a`.
// O(N^2) with N <= 4096 is acceptable for an offline authoring bake
// (128 samples default => 16k distance evals).
inline void align_samples(std::vector<Vec2>& b, const std::vector<Vec2>& a) {
    if (a.size() != b.size() || a.empty())
        return;
    const std::size_t n = a.size();
    double best = std::numeric_limits<double>::infinity();
    std::size_t best_shift = 0;
    bool best_reversed = false;
    std::vector<Vec2> trial(n), rev(n);
    for (std::size_t s = 0; s < n; ++s) {
        for (std::size_t i = 0; i < n; ++i)
            trial[i] = b[(i + s) % n];
        const double c = alignment_cost(a, trial);
        if (c < best) {
            best = c;
            best_shift = s;
            best_reversed = false;
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        rev[i] = b[(n - i) % n];
    for (std::size_t s = 0; s < n; ++s) {
        for (std::size_t i = 0; i < n; ++i)
            trial[i] = rev[(i + s) % n];
        const double c = alignment_cost(a, trial);
        if (c < best) {
            best = c;
            best_shift = s;
            best_reversed = true;
        }
    }
    if (!best_reversed) {
        for (std::size_t i = 0; i < n; ++i)
            trial[i] = b[(i + best_shift) % n];
    } else {
        for (std::size_t i = 0; i < n; ++i)
            trial[i] = rev[(i + best_shift) % n];
    }
    b = std::move(trial);
}

[[nodiscard]] inline std::vector<PathCommand> samples_to_commands(
    const std::vector<Vec2>& samples, bool closed) {
    std::vector<PathCommand> cmds;
    cmds.reserve(samples.size() + (closed ? 1 : 0));
    if (samples.empty())
        return cmds;
    cmds.push_back(PathCommand::move_to(samples.front()));
    for (std::size_t i = 1; i < samples.size(); ++i)
        cmds.push_back(PathCommand::line_to(samples[i]));
    if (closed)
        cmds.push_back(PathCommand::close());
    return cmds;
}

inline void validate_inputs(const PathShape& a, const PathShape& b,
                            std::size_t samples) {
    if (samples < kMinMorphSamples || samples > kMaxMorphSamples)
        throw std::invalid_argument(
            "morph normalization sample count must be in [3, 4096]");
    for (const auto& cmd : a.commands) {
        if (cmd.type == PathCommandType::Close)
            continue;
        if (!finite_point(cmd.p0) || !finite_point(cmd.p1) || !finite_point(cmd.p2))
            throw std::invalid_argument("morph source coordinates must be finite");
    }
    for (const auto& cmd : b.commands) {
        if (cmd.type == PathCommandType::Close)
            continue;
        if (!finite_point(cmd.p0) || !finite_point(cmd.p1) || !finite_point(cmd.p2))
            throw std::invalid_argument("morph target coordinates must be finite");
    }
    if (a.commands.empty() || b.commands.empty())
        throw std::invalid_argument("morph normalization requires non-empty paths");
}

} // namespace detail

/// Normalized, topologically-identical command pair for the Morph engine.
struct NormalizedMorphPair {
    std::vector<PathCommand> source;
    std::vector<PathCommand> target;
    bool closed{false};
    std::size_t samples{0};
};

/// Resample one path to N uniformly-spaced polyline commands.
/// Closed-ness follows the input (Close op or PathShape::closed).
[[nodiscard]] inline std::vector<PathCommand> normalize_path_for_morph(
    const PathShape& path, std::size_t samples = 128) {
    if (samples < kMinMorphSamples || samples > kMaxMorphSamples)
        throw std::invalid_argument(
            "morph normalization sample count must be in [3, 4096]");
    if (path.commands.empty())
        throw std::invalid_argument("morph normalization requires a non-empty path");
    auto outline = detail::outline_points(path);
    if (outline.size() < 2)
        throw std::invalid_argument("morph normalization needs at least 2 distinct points");
    const bool closed = detail::path_is_closed(path);
    auto resampled = detail::resample_uniform(outline, samples, closed);
    return detail::samples_to_commands(resampled, closed);
}

/// Convert two arbitrary shapes into a compatible pair.
///
/// Both outputs share the exact command sequence
/// (MoveTo + (N-1) LineTo [+ Close]) so validate_morph_path_topology
/// succeeds. Target samples are cyclically shifted / optionally reversed
/// to minimize vertex-travel distance (no twist when morphing
/// circle -> rect started at different corners).
[[nodiscard]] inline NormalizedMorphPair make_morph_compatible_pair(
    const PathShape& a, const PathShape& b, std::size_t samples = 128) {
    detail::validate_inputs(a, b, samples);
    auto outline_a = detail::outline_points(a);
    auto outline_b = detail::outline_points(b);
    if (outline_a.size() < 2 || outline_b.size() < 2)
        throw std::invalid_argument(
            "morph normalization needs at least 2 distinct points per shape");
    // Closed if either side is closed: a closed loop resampled open would
    // collapse the seam vertex and visibly pop. Open+closed still morphs
    // cleanly because both sides share the emitted Close.
    const bool closed = detail::path_is_closed(a) || detail::path_is_closed(b);
    auto ra = detail::resample_uniform(outline_a, samples, closed);
    auto rb = detail::resample_uniform(outline_b, samples, closed);
    if (closed)
        detail::align_samples(rb, ra);
    NormalizedMorphPair pair;
    pair.source = detail::samples_to_commands(ra, closed);
    pair.target = detail::samples_to_commands(rb, closed);
    pair.closed = closed;
    pair.samples = samples;
    // Self-check: must satisfy the engine's topology gate.
    if (validate_morph_path_topology(pair.source, pair.target))
        throw std::runtime_error(
            "internal error: normalized morph pair failed topology validation");
    return pair;
}

/// Convenience: normalized PathShapes (style preserved from inputs).
[[nodiscard]] inline std::pair<PathShape, PathShape>
build_normalized_morph_shapes(const PathShape& a, const PathShape& b,
                              std::size_t samples = 128) {
    auto pair = make_morph_compatible_pair(a, b, samples);
    PathShape na = a, nb = b;
    na.commands = std::move(pair.source);
    nb.commands = std::move(pair.target);
    na.closed = nb.closed = pair.closed;
    na.operators.clear();
    nb.operators.clear();
    return {std::move(na), std::move(nb)};
}

// ── Primitive helpers (circle / rect / triangle -> compatible paths) ──────

[[nodiscard]] inline PathShape circle_to_path(Vec2 center, f32 radius,
                                             std::size_t samples = 128) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(radius) || radius <= 0.0f)
        throw std::invalid_argument("circle morph source must be finite with radius > 0");
    if (samples < kMinMorphSamples || samples > kMaxMorphSamples)
        throw std::invalid_argument("sample count must be in [3, 4096]");
    PathShape out;
    out.closed = true;
    out.commands.reserve(samples + 1);
    constexpr float kTau = 6.2831853071795864769f;
    for (std::size_t i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        const Vec2 p{center.x + radius * std::cos(kTau * t),
                     center.y + radius * std::sin(kTau * t)};
        if (i == 0)
            out.commands.push_back(PathCommand::move_to(p));
        else
            out.commands.push_back(PathCommand::line_to(p));
    }
    out.commands.push_back(PathCommand::close());
    return out;
}

[[nodiscard]] inline PathShape rect_to_path(Vec2 min, Vec2 max,
                                           std::size_t samples = 128) {
    if (!std::isfinite(min.x) || !std::isfinite(min.y) ||
        !std::isfinite(max.x) || !std::isfinite(max.y) || max.x <= min.x ||
        max.y <= min.y)
        throw std::invalid_argument("rect morph source requires finite min < max");
    if (samples < kMinMorphSamples || samples > kMaxMorphSamples)
        throw std::invalid_argument("sample count must be in [3, 4096]");
    const std::vector<Vec2> corners{min, {max.x, min.y}, max, {min.x, max.y}};
    std::vector<Vec2> loop;
    for (std::size_t e = 0; e < 4; ++e) {
        const Vec2 p0 = corners[e], p1 = corners[(e + 1) % 4];
        const std::size_t per = samples / 4;
        const std::size_t extra = (e < samples % 4) ? 1 : 0;
        const std::size_t count = per + extra;
        for (std::size_t i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count);
            loop.push_back(Vec2{p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t});
        }
    }
    PathShape out;
    out.closed = true;
    out.commands.reserve(loop.size() + 1);
    out.commands.push_back(PathCommand::move_to(loop.front()));
    for (std::size_t i = 1; i < loop.size(); ++i)
        out.commands.push_back(PathCommand::line_to(loop[i]));
    out.commands.push_back(PathCommand::close());
    return out;
}

[[nodiscard]] inline PathShape polygon_to_path(const std::vector<Vec2>& vertices,
                                              std::size_t samples = 128) {
    if (vertices.size() < 3)
        throw std::invalid_argument("polygon morph source needs >= 3 vertices");
    for (const auto v : vertices)
        if (!detail::finite_point(v))
            throw std::invalid_argument("polygon vertices must be finite");
    if (samples < kMinMorphSamples || samples > kMaxMorphSamples)
        throw std::invalid_argument("sample count must be in [3, 4096]");
    // Perimeter-uniform resample so long edges get proportionally more verts.
    std::vector<double> cum(vertices.size() + 1, 0.0);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Vec2 d = vertices[(i + 1) % vertices.size()] - vertices[i];
        cum[i + 1] = cum[i] + static_cast<double>(glm::length(d));
    }
    const double total = cum.back();
    if (!(total > 1e-9))
        throw std::invalid_argument("polygon morph source is degenerate");
    PathShape out;
    out.closed = true;
    for (std::size_t i = 0; i < samples; ++i) {
        const double target = total * static_cast<double>(i) / static_cast<double>(samples);
        std::size_t seg = 0;
        while (seg + 1 < cum.size() && cum[seg + 1] < target)
            ++seg;
        const Vec2 p0 = vertices[seg % vertices.size()];
        const Vec2 p1 = vertices[(seg + 1) % vertices.size()];
        const double span = cum[seg + 1] - cum[seg];
        const double t = span > 1e-12 ? (target - cum[seg]) / span : 0.0;
        const float ft = static_cast<float>(std::clamp(t, 0.0, 1.0));
        const Vec2 p{p0.x + (p1.x - p0.x) * ft, p0.y + (p1.y - p0.y) * ft};
        if (i == 0)
            out.commands.push_back(PathCommand::move_to(p));
        else
            out.commands.push_back(PathCommand::line_to(p));
    }
    out.commands.push_back(PathCommand::close());
    return out;
}

} // namespace chronon3d::morph
