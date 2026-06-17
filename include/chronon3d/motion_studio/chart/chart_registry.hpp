#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  ChartRegistry — data-driven chart builder for motion_studio.
//
//  Slice scope: line() with values, draw_animation (seconds), show_points,
//  area_fill_marker.  Implementation emits the full polyline as one Path
//  layer + one Circle layer per data point with a staggered opacity_anim
//  keyed to the draw animation.  See chart_registry.cpp for emit_all().
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

#include <string>
#include <vector>

namespace chronon3d::motion_studio {

struct LineChartOptions {
    std::vector<f32> values{0.0f};
    chronon3d::Vec2  size{600.0f, 240.0f};
    f32              draw_animation   = 0.6f;
    f32              point_stagger    = 0.06f;
    f32              start            = 0.0f;
    bool             show_points      = true;
    bool             area_fill_marker = false;
    chronon3d::Color stroke_color     = {0.30f, 0.60f, 1.0f, 1.0f};
    chronon3d::Color fill_color       = {0.30f, 0.60f, 1.0f, 0.20f};
    chronon3d::Color dot_color        = {0.95f, 0.96f, 1.0f, 1.0f};
    f32              stroke_width     = 3.0f;
    f32              dot_radius       = 5.0f;
};

class ChartRegistry {
public:
    static ChartRegistry& instance() {
        static ChartRegistry s;
        return s;
    }

    void line(const std::string& id, const LineChartOptions& opts, f32 fps) {
        pending_.push_back({id, opts, fps});
    }

    /// Flush every queued chart into `s`.  Bodies live in chart_registry.cpp.
    void emit_all(chronon3d::SceneBuilder& s, const std::string& prefix = "chart");

    void clear() { pending_.clear(); }
    std::size_t pending_count() const { return pending_.size(); }

private:
    struct PendingLine {
        std::string id;
        LineChartOptions opts;
        f32 fps;
    };
    std::vector<PendingLine> pending_;

    ChartRegistry() = default;
    ChartRegistry(const ChartRegistry&) = delete;
    ChartRegistry& operator=(const ChartRegistry&) = delete;
};

} // namespace chronon3d::motion_studio
