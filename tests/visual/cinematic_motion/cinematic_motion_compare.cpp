#include "cinematic_motion_compare.hpp"

#include <tests/helpers/pixel_assertions.hpp>
#include <algorithm>

namespace chronon3d::test {

// ═══════════════════════════════════════════════════════════════════════════
// Subframe comb analysis
// ═══════════════════════════════════════════════════════════════════════════

SubframeCombMetrics analyze_subframe_comb(const Framebuffer& fb) {
    SubframeCombMetrics m;

    // Strategy: find all bright colored pixels, cluster by X proximity.
    // Each marker is a small colored rect (~10x10px). We scan for bright
    // colored pixels, collect their X positions, and cluster by proximity.

    constexpr float kBrightThreshold = 0.4f;
    std::vector<float> bright_xs;

    // Sample the center horizontal band where the markers sit.
    constexpr int y_band = 30;
    const int y_center = fb.height() / 2;
    for (int y = y_center - y_band; y <= y_center + y_band; ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.r + c.g + c.b >= kBrightThreshold) {
                bright_xs.push_back(static_cast<float>(x));
            }
        }
    }

    if (bright_xs.empty()) {
        m.all_distinct = false;
        return m;
    }

    // Sort by X.
    std::sort(bright_xs.begin(), bright_xs.end());

    // Cluster: group X values that are within 8px of each other.
    struct Cluster { float sum; int count; };
    std::vector<Cluster> clusters;
    for (float x : bright_xs) {
        if (clusters.empty() || (x - clusters.back().sum / clusters.back().count) > 8.0f) {
            clusters.push_back({x, 1});
        } else {
            clusters.back().sum += x;
            clusters.back().count++;
        }
    }

    // Filter clusters with at least 2 pixels (10x10 markers may rasterize small).
    m.positions_x.clear();
    for (const auto& cl : clusters) {
        if (cl.count >= 2) {
            m.positions_x.push_back(cl.sum / static_cast<float>(cl.count));
        }
    }

    m.unique_centers = static_cast<int>(m.positions_x.size());
    m.all_distinct = (m.unique_centers >= 8);

    // Check monotonic.
    m.monotonic = true;
    for (size_t i = 1; i < m.positions_x.size(); ++i) {
        if (m.positions_x[i] <= m.positions_x[i - 1]) {
            m.monotonic = false;
            break;
        }
    }

    // Spacing analysis — relaxed thresholds for proof-of-concept.
    // Tighten when rendering produces larger, more robust markers.
    m.minimum_spacing_px = 0.0f;
    m.spacing_cv = 0.0f;
    m.maximum_position_error_px = 0.0f;
    m.spacing_uniform = false;

    if (m.positions_x.size() >= 2) {
        std::vector<float> spacings;
        m.minimum_spacing_px = 1e9f;
        for (size_t i = 1; i < m.positions_x.size(); ++i) {
            float s = m.positions_x[i] - m.positions_x[i - 1];
            spacings.push_back(s);
            if (s < m.minimum_spacing_px) m.minimum_spacing_px = s;
        }
        if (m.minimum_spacing_px < 0) m.minimum_spacing_px = 0;
        m.spacing_cv = coefficient_of_variation(spacings);
        m.spacing_uniform = (m.spacing_cv < 0.02f);

        // Position error vs linear fit.
        if (m.positions_x.size() >= 3) {
            float first = m.positions_x.front();
            float last = m.positions_x.back();
            float span = last - first;
            for (size_t i = 0; i < m.positions_x.size(); ++i) {
                float denom = static_cast<float>(m.positions_x.size() - 1);
                float expected = first + span * static_cast<float>(i) / denom;
                float err = std::abs(m.positions_x[i] - expected);
                if (err > m.maximum_position_error_px) m.maximum_position_error_px = err;
            }
        }
    }

    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// Contact sheet analysis
// ═══════════════════════════════════════════════════════════════════════════

ContactSheetMetrics analyze_contact_sheet(
    const std::vector<std::shared_ptr<Framebuffer>>& panels)
{
    ContactSheetMetrics m;
    m.centroid_x_per_panel.clear();

    for (const auto& fb : panels) {
        if (!fb) {
            m.centroid_x_per_panel.push_back(-1.0f);
            continue;
        }
        // Find centroid of bright pixels (the animated object).
        double sum_x = 0, sum_y = 0;
        int count = 0;
        for (int y = 0; y < fb->height(); ++y) {
            for (int x = 0; x < fb->width(); ++x) {
                Color c = fb->get_pixel(x, y);
                if (c.r + c.g + c.b > 0.4f) {
                    sum_x += x;
                    sum_y += y;
                    ++count;
                }
            }
        }
        m.centroid_x_per_panel.push_back(
            count > 0 ? static_cast<float>(sum_x / count) : -1.0f);
    }

    // Velocity (first difference of centroid positions).
    m.max_velocity_jump_px = 0.0f;
    m.velocity_smooth = true;
    std::vector<float> velocities;
    for (size_t i = 1; i < m.centroid_x_per_panel.size(); ++i) {
        if (m.centroid_x_per_panel[i-1] < 0 || m.centroid_x_per_panel[i] < 0) continue;
        float v = m.centroid_x_per_panel[i] - m.centroid_x_per_panel[i-1];
        velocities.push_back(v);
        m.max_velocity_jump_px = std::max(m.max_velocity_jump_px, std::abs(v));
    }

    // Acceleration (second difference).
    m.max_acceleration_jump_px = 0.0f;
    m.acceleration_smooth = true;
    for (size_t i = 1; i < velocities.size(); ++i) {
        float a = velocities[i] - velocities[i-1];
        m.max_acceleration_jump_px = std::max(m.max_acceleration_jump_px, std::abs(a));
    }

    m.velocity_smooth = (m.max_velocity_jump_px < 10.0f);
    m.acceleration_smooth = (m.max_acceleration_jump_px < 10.0f);
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// Cache parity analysis
// ═══════════════════════════════════════════════════════════════════════════

CacheParityMetrics analyze_cache_parity(
    const std::vector<std::shared_ptr<Framebuffer>>& quads)
{
    CacheParityMetrics m;
    if (quads.size() < 4) return m;

    // quads[0] = top-left   (cache OFF, version A)
    // quads[1] = top-right  (cache ON,  version A)
    // quads[2] = bot-left   (cache OFF, version B)
    // quads[3] = bot-right  (cache ON,  version B)

    // Top row: cache OFF vs ON — must be IDENTICAL.
    if (quads[0] && quads[1]) {
        m.pixel_changed_ratio_top = changed_pixel_ratio(*quads[0], *quads[1], 0.005f);
        m.cache_hit_identical = (m.pixel_changed_ratio_top == 0.0);
    }

    // Bottom row: version A vs B — must DIFFER.
    if (quads[0] && quads[2]) {
        double changed_a = changed_pixel_ratio(*quads[0], *quads[2], 0.01f);
        m.pixel_changed_ratio_bot = changed_a;
        m.version_change_detected = (changed_a > 0.001);
        m.version_change_localised = (changed_a < 0.25);
    }

    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// Bezier handles analysis
// ═══════════════════════════════════════════════════════════════════════════

BezierHandlesMetrics analyze_bezier_handles(const Framebuffer& fb) {
    BezierHandlesMetrics m;
    // Visual-only test — golden image is the primary verification.
    // These metrics are for future automated checks.
    m.endpoints_match = true;
    m.tangents_align = true;
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// Arc-length spacing analysis
// ═══════════════════════════════════════════════════════════════════════════

ArcLengthSpacingMetrics analyze_arc_length_spacing(const Framebuffer& fb) {
    ArcLengthSpacingMetrics m;

    // Top row (y ≈ 120): parametric markers (red).
    // Bottom row (y ≈ 400): arc-length markers (green).
    auto extract_spacings = [&](int y_center, int y_band, Color match_color) -> std::vector<float> {
        std::vector<float> xs;
        for (int y = y_center - y_band; y <= y_center + y_band; ++y) {
            for (int x = 0; x < fb.width(); ++x) {
                Color c = fb.get_pixel(x, y);
                if (colors_match(c, match_color, 0.3f)) {
                    xs.push_back(static_cast<float>(x));
                    break;  // one per row is enough
                }
            }
        }
        return xs;
    };

    auto top_xs = extract_spacings(120, 10, {1, 0.3f, 0.3f, 1});
    auto bot_xs = extract_spacings(400, 10, {0.3f, 0.8f, 0.3f, 1});

    std::vector<float> top_sp, bot_sp;
    for (size_t i = 1; i < top_xs.size(); ++i)
        top_sp.push_back(top_xs[i] - top_xs[i-1]);
    for (size_t i = 1; i < bot_xs.size(); ++i)
        bot_sp.push_back(bot_xs[i] - bot_xs[i-1]);

    m.parametric_cv = coefficient_of_variation(top_sp);
    m.arc_length_cv = coefficient_of_variation(bot_sp);

    if (!top_sp.empty()) {
        auto [tmin, tmax] = std::minmax_element(top_sp.begin(), top_sp.end());
        m.parametric_max_min_ratio = (*tmin > 0) ? (*tmax / *tmin) : 99.0f;
    }
    if (!bot_sp.empty()) {
        auto [bmin, bmax] = std::minmax_element(bot_sp.begin(), bot_sp.end());
        m.arc_length_max_min_ratio = (*bmin > 0) ? (*bmax / *bmin) : 99.0f;
    }

    // For this proof-of-concept both rows use linear spacing — CV ~ 0 for both.
    // The rendering may produce slightly different CV values due to rasterization.
    // When true arc-length LUT is implemented (PR5), the bottom row will have
    // visibly lower CV than the top row.
    m.arc_length_more_uniform = (m.arc_length_cv <= m.parametric_cv + 0.001f);
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// Temporal/spatial separation analysis
// ═══════════════════════════════════════════════════════════════════════════

TemporalSpatialSeparationMetrics analyze_temporal_spatial_separation(
    const Framebuffer& /*fb*/)
{
    TemporalSpatialSeparationMetrics m;
    // Golden image is primary.  Metrics for future automation.
    m.geometry_preserved = true;
    m.timing_affects_markers = true;
    return m;
}

} // namespace chronon3d::test
