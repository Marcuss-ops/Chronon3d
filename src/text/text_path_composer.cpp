// ═══════════════════════════════════════════════════════════════════════════
// text_path_composer.cpp — Text-on-path placement via PathSampler
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_path_composer.hpp>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace chronon3d {

namespace {

// ── Helper: radians → degrees ────────────────────────────────────────────

[[nodiscard]] constexpr float rad2deg(float rad) {
    return rad * 57.29577951308232f;  // 180/π
}

// ── Compute total text advance from clusters ─────────────────────────────

[[nodiscard]] float total_text_advance(const PlacedGlyphRun& shaped) {
    float total = 0.0f;
    for (const auto& cl : shaped.clusters) {
        total += cl.advance;
    }
    return total;
}

// ── Extrapolate beyond path end ──────────────────────────────────────────

[[nodiscard]] PathSample extrapolate_past_end(
    const PathSampler& sampler,
    float distance
) {
    // Sample at the end and use that tangent to extrapolate linearly.
    auto end_sample = sampler.sample_distance(sampler.total_length());
    float extra = distance - sampler.total_length();
    PathSample result = end_sample;
    result.position = end_sample.position + end_sample.tangent * extra;
    result.distance = distance;
    if (sampler.total_length() > 0.0f) {
        result.normalized_t = distance / sampler.total_length();
    }
    return result;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// compose_text_on_path
// ═══════════════════════════════════════════════════════════════════════════

PlacedTextOnPath compose_text_on_path(
    const PlacedGlyphRun& shaped,
    const PathSampler& sampler,
    const TextPathSpec& spec,
    std::string_view source_text
) {
    (void)source_text;  // reserved for RTL detection
    PlacedTextOnPath result;
    const size_t n = shaped.clusters.size();
    if (n == 0 || sampler.total_length() <= 0.0f) {
        return result;
    }

    result.clusters.reserve(n);

    // ── Compute usable length ─────────────────────────────────────────
    float total_len = sampler.total_length();
    float usable = total_len - spec.first_margin - spec.last_margin;
    if (usable < 0.0f) usable = 0.0f;

    float text_width = total_text_advance(shaped);

    // ── Determine start distance based on alignment ───────────────────
    float start_distance = spec.first_margin;
    float extra_tracking = 0.0f;

    switch (spec.alignment) {
    case TextPathAlignment::Start:
        break;

    case TextPathAlignment::Center:
        start_distance = spec.first_margin + (usable - text_width) * 0.5f;
        break;

    case TextPathAlignment::End:
        start_distance = spec.first_margin + usable - text_width;
        break;

    case TextPathAlignment::Justify:
        if (n > 1 && (text_width < usable || spec.force_alignment)) {
            extra_tracking = (usable - text_width) / static_cast<float>(n - 1);
        }
        break;
    }

    // ── Normal direction ──────────────────────────────────────────────
    // By default, PathSample::normal = (tangent.y, -tangent.x) — clockwise
    // rotation.  flip_normal negates this.
    float normal_sign = spec.flip_normal ? -1.0f : 1.0f;

    // ── Place each cluster ─────────────────────────────────────────────
    float cursor = start_distance;

    for (size_t i = 0; i < n; ++i) {
        float cluster_advance = shaped.clusters[i].advance;
        float cluster_center  = cursor + cluster_advance * 0.5f;

        PlacedCluster pc;
        pc.cluster_index = i;

        // Compute effective distance (possibly reversed).
        float effective_dist = cluster_center;
        if (spec.reverse_path) {
            effective_dist = total_len - cluster_center;
        }

        // Overflow check.
        if (spec.overflow == TextPathOverflow::Clip && cluster_center > total_len) {
            pc.position = Vec2{0.0f, 0.0f};
            pc.rotation_deg = 0.0f;
            pc.path_distance = cluster_center;
            result.clusters.push_back(pc);
            cursor += cluster_advance + extra_tracking;
            continue;
        }

        // WrapClosedPath: wrap around via modulo (both positive and negative overflow).
        if (spec.overflow == TextPathOverflow::WrapClosedPath
            && spec.closed
            && (effective_dist > total_len || effective_dist < 0.0f)) {
            effective_dist = std::fmod(effective_dist, total_len);
            if (effective_dist < 0.0f) effective_dist += total_len;
        }

        // Sample the path.
        PathSample sample;
        if (effective_dist > total_len && spec.overflow == TextPathOverflow::Continue) {
            sample = extrapolate_past_end(sampler, effective_dist);
        } else if (effective_dist < 0.0f && spec.overflow == TextPathOverflow::Continue) {
            // Before path start — extrapolate backward.
            auto start_s = sampler.sample_distance(0.0f);
            sample = start_s;
            sample.position = start_s.position - start_s.tangent * (-effective_dist);
            sample.distance = effective_dist;
        } else {
            sample = sampler.sample_distance(
                std::clamp(effective_dist, 0.0f, total_len)
            );
        }

        // Reverse tangent if reversed path.
        Vec2 tangent = spec.reverse_path ? -sample.tangent : sample.tangent;

        // Position = path position + normal offset.
        Vec2 normal = spec.reverse_path ? -sample.normal : sample.normal;
        pc.position = sample.position + normal * (normal_sign * spec.baseline_offset);

        // Rotation.
        if (spec.perpendicular_to_path) {
            pc.rotation_deg = rad2deg(std::atan2(tangent.y, tangent.x));
        } else {
            pc.rotation_deg = 0.0f;
        }

        pc.path_distance = cluster_center;
        result.clusters.push_back(pc);

        cursor += cluster_advance + extra_tracking;
    }

    return result;
}

} // namespace chronon3d
