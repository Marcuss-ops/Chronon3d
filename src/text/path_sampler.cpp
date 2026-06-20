// ═══════════════════════════════════════════════════════════════════════════
// path_sampler.cpp — Arc-length PathSampler implementation
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/path_sampler.hpp>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// ── Point-to-line distance (for flatness check) ──────────────────────────

[[nodiscard]] float point_line_distance(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float len_sq = glm::dot(ab, ab);
    if (len_sq < 1e-12f) return glm::distance(p, a);
    float t = glm::dot(p - a, ab) / len_sq;
    t = std::clamp(t, 0.0f, 1.0f);
    Vec2 projection = a + ab * t;
    return glm::distance(p, projection);
}

// ── Cubic Bézier De Casteljau subdivision ────────────────────────────────
//
// Recursively subdivides a cubic Bézier (p0, p1, p2, p3) until the curve
// is sufficiently flat, then emits the endpoint p3.
//
// IMPORTANT: The caller MUST have already emitted the start point p0
// before calling this function.  This function only emits intermediate
// subdivision points and the final endpoint.

void flatten_cubic(
    Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3,
    float tolerance,
    std::vector<Vec2>& out
) {
    // Flatness check: distance of control points from chord p0→p3.
    float err = point_line_distance(p1, p0, p3) + point_line_distance(p2, p0, p3);
    if (err <= tolerance) {
        out.push_back(p3);
        return;
    }

    // Subdivide at t = 0.5 using De Casteljau.
    Vec2 p01  = (p0 + p1) * 0.5f;
    Vec2 p12  = (p1 + p2) * 0.5f;
    Vec2 p23  = (p2 + p3) * 0.5f;
    Vec2 p012 = (p01 + p12) * 0.5f;
    Vec2 p123 = (p12 + p23) * 0.5f;
    Vec2 p0123= (p012 + p123) * 0.5f;

    flatten_cubic(p0, p01, p012, p0123, tolerance, out);
    flatten_cubic(p0123, p123, p23, p3, tolerance, out);
}

// ── Quadratic Bézier De Casteljau subdivision ────────────────────────────
//
// Same contract as flatten_cubic: caller must have emitted p0; this
// function emits intermediates + p2.

void flatten_quadratic(
    Vec2 p0, Vec2 p1, Vec2 p2,
    float tolerance,
    std::vector<Vec2>& out
) {
    float err = point_line_distance(p1, p0, p2);
    if (err <= tolerance) {
        out.push_back(p2);
        return;
    }

    Vec2 p01  = (p0 + p1) * 0.5f;
    Vec2 p12  = (p1 + p2) * 0.5f;
    Vec2 p012 = (p01 + p12) * 0.5f;

    flatten_quadratic(p0, p01, p012, tolerance, out);
    flatten_quadratic(p012, p12, p2, tolerance, out);
}

// ── Flatten a full PathShape into a vector of points ─────────────────────

[[nodiscard]] std::vector<Vec2> flatten_path(const PathShape& path, float tolerance) {
    std::vector<Vec2> points;

    if (path.commands.empty()) return points;

    // The first command MUST be MoveTo.
    Vec2 current = path.commands[0].p0;
    Vec2 path_start = current;
    points.push_back(current);

    for (size_t i = 1; i < path.commands.size(); ++i) {
        const auto& cmd = path.commands[i];
        switch (cmd.type) {
        case PathCommandType::MoveTo:
            current = cmd.p0;
            path_start = current;
            points.push_back(current);
            break;

        case PathCommandType::LineTo:
            current = cmd.p0;
            points.push_back(current);
            break;

        case PathCommandType::CubicTo:
            flatten_cubic(current, cmd.p1, cmd.p2, cmd.p0, tolerance, points);
            current = cmd.p0;
            break;

        case PathCommandType::QuadraticTo:
            flatten_quadratic(current, cmd.p1, cmd.p0, tolerance, points);
            current = cmd.p0;
            break;

        case PathCommandType::Close:
            if (glm::distance(current, path_start) > 1e-6f) {
                points.push_back(path_start);
            }
            current = path_start;
            break;
        }
    }

    // If the path is marked closed but no explicit Close command,
    // add the closing segment.
    if (path.closed && points.size() >= 2) {
        Vec2 first = points[0];
        Vec2 last  = points.back();
        if (glm::distance(last, first) > 1e-6f) {
            points.push_back(first);
        }
    }

    return points;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// PathSampler::compile
// ═══════════════════════════════════════════════════════════════════════════

PathSampler PathSampler::compile(const PathShape& path, float flatness_tolerance) {
    PathSampler sampler;
    auto points = flatten_path(path, flatness_tolerance);

    if (points.size() < 2) {
        sampler.m_total_length = 0.0f;
        return sampler;
    }

    sampler.m_samples.reserve(points.size());

    float cumulative = 0.0f;
    Vec2 prev_tangent{1.0f, 0.0f};

    for (size_t i = 0; i < points.size(); ++i) {
        ArcLengthPoint alp;
        alp.position = points[i];
        alp.cumulative_distance = cumulative;

        // Tangent: direction to next point (or same as previous at end)
        if (i + 1 < points.size()) {
            Vec2 dir = points[i + 1] - points[i];
            float len = glm::length(dir);
            if (len > 1e-9f) {
                alp.tangent = dir / len;
                prev_tangent = alp.tangent;
                cumulative += len;
            } else {
                alp.tangent = prev_tangent;
            }
        } else {
            alp.tangent = prev_tangent;
        }

        sampler.m_samples.push_back(alp);
    }

    sampler.m_total_length = cumulative;
    return sampler;
}

// ═══════════════════════════════════════════════════════════════════════════
// PathSampler::sample_distance
// ═══════════════════════════════════════════════════════════════════════════

PathSample PathSampler::sample_distance(float distance) const {
    PathSample result;
    if (m_samples.empty() || m_total_length <= 0.0f) {
        return result;
    }

    distance = std::clamp(distance, 0.0f, m_total_length);

    // Binary search for the segment containing `distance`.
    auto it = std::lower_bound(
        m_samples.begin(), m_samples.end(), distance,
        [](const ArcLengthPoint& alp, float d) {
            return alp.cumulative_distance < d;
        }
    );

    if (it == m_samples.begin()) {
        // Exact start.
        result.position = m_samples[0].position;
        result.tangent  = m_samples[0].tangent;
        result.distance = 0.0f;
        result.normalized_t = 0.0f;
    } else if (it == m_samples.end()) {
        // Exact end.
        const auto& last = m_samples.back();
        result.position = last.position;
        result.tangent  = last.tangent;
        result.distance = m_total_length;
        result.normalized_t = 1.0f;
    } else {
        // Interpolate between it-1 and it.
        const auto& prev = *(it - 1);
        const auto& next = *it;

        float seg_start = prev.cumulative_distance;
        float seg_end   = next.cumulative_distance;
        float seg_len   = seg_end - seg_start;

        float t = (seg_len > 1e-9f) ? (distance - seg_start) / seg_len : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);

        result.position = glm::mix(prev.position, next.position, t);
        result.tangent  = glm::normalize(glm::mix(prev.tangent, next.tangent, t));
        result.distance = distance;
        result.normalized_t = distance / m_total_length;
    }

    // Normal: rotate tangent 90° clockwise: (tx, ty) → (ty, -tx)
    result.normal = Vec2{result.tangent.y, -result.tangent.x};

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// PathSampler::sample_normalized
// ═══════════════════════════════════════════════════════════════════════════

PathSample PathSampler::sample_normalized(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    return sample_distance(t * m_total_length);
}

} // namespace chronon3d
