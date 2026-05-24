#include "path_stroke.hpp"
#include "path_utils.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <utility>

namespace chronon3d::renderer {

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

} // namespace chronon3d::renderer
