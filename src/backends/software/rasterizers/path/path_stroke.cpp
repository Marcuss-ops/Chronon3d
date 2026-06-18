#include "path_stroke.hpp"
#include "path_utils.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <utility>

namespace chronon3d::renderer {

// ── PR1: prepare_stroke_contours ────────────────────────────────────
// Apply trim + dash once for the whole draw_path call, so the per-pixel
// hot loop can iterate flat prepared data without re-allocating or
// re-running any geometry preprocessing.  This MUST keep observable
// behaviour identical to the legacy code that called trim/dash inside
// the per-pixel loop:
//   1. if `pts.size() < 2` the contour contributes an empty entry,
//   2. when trim returns empty we fall back to the original points
//      (legacy `pts = trimmed.empty() ? contour.points : trimmed`),
//   3. only subpaths with `size() >= 2` survive into visible_subpaths,
//   4. repeated_start / unique_count are pre-computed per subpath
//      using the same closed gate as the legacy pixel loop
//      (`no_dash && contour.closed`).

f32 segment_coverage(Vec2 p, Vec2 a, Vec2 b, f32 radius) {
    const Vec2 ab = b - a;
    const f32 len_sq = glm::dot(ab, ab);
    if (len_sq < kPathEpsilon) {
        const f32 dist = glm::length(p - a);
        const f32 aa = 1.0f;
        if (dist <= radius - aa) return 1.0f;
        if (dist >= radius + aa) return 0.0f;
        return 1.0f - ((dist - (radius - aa)) / (2.0f * aa));
    }

    const f32 raw_t = glm::dot(p - a, ab) / len_sq;
    if (raw_t < 0.0f || raw_t > 1.0f) return 0.0f;

    const Vec2 closest = a + ab * raw_t;
    const f32 dist = glm::length(closest - p);
    const f32 aa = 1.0f;
    if (dist <= radius - aa) return 1.0f;
    if (dist >= radius + aa) return 0.0f;
    return 1.0f - ((dist - (radius - aa)) / (2.0f * aa));
}

std::vector<Vec2> trim_polyline_points(const std::vector<Vec2>& points, bool closed, f32 start_t, f32 end_t) {
    if (points.size() < 2) return {};
    if (end_t <= start_t) return {};

    std::vector<std::pair<Vec2, Vec2>> segments;
    segments.reserve(points.size());
    for (usize i = 0; i + 1 < points.size(); ++i) {
        segments.push_back({points[i], points[i + 1]});
    }
    if (closed && glm::length(points.front() - points.back()) > kPathEpsilon) {
        segments.push_back({points.back(), points.front()});
    }

    std::vector<f32> lengths;
    lengths.reserve(segments.size());
    f32 total = 0.0f;
    for (const auto& seg : segments) {
        const f32 len = glm::length(seg.second - seg.first);
        lengths.push_back(len);
        total += len;
    }
    if (total < kPathEpsilon) return {};

    const f32 start_d = std::clamp(start_t, 0.0f, 1.0f) * total;
    const f32 end_d = std::clamp(end_t, 0.0f, 1.0f) * total;
    if (end_d <= start_d) return {};

    std::vector<Vec2> out;
    f32 cursor = 0.0f;
    for (usize i = 0; i < segments.size(); ++i) {
        const auto& [a, b] = segments[i];
        const f32 len = lengths[i];
        if (len < kPathEpsilon) {
            cursor += len;
            continue;
        }

        const f32 seg_start = cursor;
        const f32 seg_end = cursor + len;
        cursor = seg_end;

        if (seg_end < start_d || seg_start > end_d) {
            continue;
        }

        const Vec2 dir = (b - a) / len;
        if (out.empty()) {
            const f32 t0 = std::clamp((start_d - seg_start) / len, 0.0f, 1.0f);
            out.push_back(a + dir * (t0 * len));
        }

        const f32 t1 = std::clamp((end_d - seg_start) / len, 0.0f, 1.0f);
        out.push_back(a + dir * (t1 * len));

        if (seg_end >= end_d) {
            break;
        }
    }

    return out;
}

std::vector<std::vector<Vec2>> dash_polyline_points(const std::vector<Vec2>& points, bool closed, const std::vector<f32>& dash_array, f32 dash_offset) {
    if (dash_array.empty() || points.size() < 2) {
        return {points};
    }

    std::vector<std::pair<Vec2, Vec2>> segments;
    segments.reserve(points.size());
    for (usize i = 0; i + 1 < points.size(); ++i) {
        segments.push_back({points[i], points[i + 1]});
    }
    if (closed && glm::length(points.front() - points.back()) > kPathEpsilon) {
        segments.push_back({points.back(), points.front()});
    }

    std::vector<f32> lengths;
    lengths.reserve(segments.size());
    f32 total_length = 0.0f;
    for (const auto& seg : segments) {
        f32 len = glm::length(seg.second - seg.first);
        lengths.push_back(len);
        total_length += len;
    }
    if (total_length < kPathEpsilon) {
        return {points};
    }

    std::vector<f32> pattern = dash_array;
    if (pattern.size() % 2 != 0) {
        pattern.insert(pattern.end(), dash_array.begin(), dash_array.end());
    }
    f32 pattern_len = 0.0f;
    for (f32 len : pattern) {
        pattern_len += len;
    }
    if (pattern_len < kPathEpsilon) {
        return {points};
    }

    f32 offset = std::fmod(dash_offset, pattern_len);
    if (offset < 0.0f) {
        offset += pattern_len;
    }

    std::vector<std::vector<Vec2>> result;
    f32 d = -offset;
    usize pattern_idx = 0;
    bool is_on = true;

    while (d < total_length) {
        f32 next_d = d + pattern[pattern_idx];
        if (is_on) {
            f32 t_start = std::max(0.0f, d);
            f32 t_end = std::min(total_length, next_d);
            if (t_end > t_start + kPathEpsilon) {
                auto trimmed = trim_polyline_points(points, closed, t_start / total_length, t_end / total_length);
                if (trimmed.size() >= 2) {
                    result.push_back(std::move(trimmed));
                }
            }
        }
        d = next_d;
        is_on = !is_on;
        pattern_idx = (pattern_idx + 1) % pattern.size();
    }
    return result;
}

std::vector<detail::PreparedStrokeContour> prepare_stroke_contours(
    const std::vector<PathContour>& screen_contours,
    const PathStroke& stroke) {
    std::vector<detail::PreparedStrokeContour> out;
    out.reserve(screen_contours.size());

    const bool no_dash = stroke.dash_array.empty();

    for (const auto& contour : screen_contours) {
        detail::PreparedStrokeContour prep;
        prep.source_closed = contour.closed;

        // Mirror the legacy pixel-loop early-out: contours with fewer
        // than 2 vertices contribute an empty PreparedStrokeContour that
        // the rasteriser skips with one cheap emptiness check.
        if (contour.points.size() < 2) {
            out.push_back(std::move(prep));
            continue;
        }

        // 1. trim (preserving the legacy fallback: empty trimmed vector
        //    falls back to the original contour points).
        auto trimmed = trim_polyline_points(
            contour.points,
            contour.closed,
            stroke.trim_start,
            stroke.trim_end);
        const auto& pts = trimmed.empty() ? contour.points : trimmed;
        if (pts.size() < 2) {
            out.push_back(std::move(prep));
            continue;
        }

        // 2. dash (only when configured).  When not dashed the legacy
        //    produced a single subpath equal to `pts`.
        std::vector<std::vector<Vec2>> sub_polylines;
        if (no_dash) {
            sub_polylines.push_back(pts);
        } else {
            sub_polylines = dash_polyline_points(
                pts, false, stroke.dash_array, stroke.dash_offset);
        }

        // 3. closed gate for repeated_start / unique_count — identical
        //    to the per-pixel computation but evaluated once per subpath.
        const bool sub_closed = no_dash && contour.closed;

        for (auto& sub_pts : sub_polylines) {
            if (sub_pts.size() < 2) continue;

            const std::vector<Vec2> sub_points = std::move(sub_pts);
            const bool repeated_start =
                sub_closed && sub_points.size() > 2 &&
                glm::length(sub_points.front() - sub_points.back()) < 1e-4f;
            const std::size_t unique_count =
                repeated_start ? sub_points.size() - 1 : sub_points.size();

            prep.visible_subpaths.push_back(
                detail::PreparedSubPath{std::move(sub_points), repeated_start, unique_count});
        }

        out.push_back(std::move(prep));
    }

    return out;
}

} // namespace chronon3d::renderer
