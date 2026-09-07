#pragma once

#include <string>

namespace chronon3d::render_plan {

struct RenderPlanFingerprintSettings {
    int width{1920};
    int height{1080};
    int antialiasing_samples{1};
    float ssaa_factor{1.0f};
    bool motion_blur{false};
    bool dirty_rects{false};
    bool deterministic{false};
    bool force_scalar_normal_blend{true};
    bool dirty_bitmask{true};
    bool dirty_tiles{true};
    bool parallel_tiles{true};
    int tile_size{0};
    double tile_dirty_ratio_threshold{0.30};
    bool optimize_compositing{true};
};

struct RenderPlanFingerprintOptions {
    std::string schema_version{"chronon.render-plan.v2"};
    std::string engine_compatibility_version{"chronon3d.engine.v1"};
    RenderPlanFingerprintSettings render_settings{};
};

}  // namespace chronon3d::render_plan
