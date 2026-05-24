#pragma once

#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <string>

namespace chronon3d::telemetry {

struct RenderTelemetryRow {
    std::string event;
    chronon3d::Frame frame{0};
    int width{0};
    int height{0};
    double total_ms{0.0};
    double setup_ms{0.0};
    double composite_ms{0.0};
    double blur_ms{0.0};
    double encode_ms{0.0};
    double ram_mb{0.0};
    int cache_hit{0};
    int layer_count{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint64_t nodes_executed{0};
    uint64_t clear_calls{0};
    uint64_t clear_pixels{0};
    uint64_t composite_calls{0};
    uint64_t composite_pixels{0};
    uint64_t transform_calls{0};
    uint64_t transform_pixels{0};
    uint64_t effect_stack_calls{0};
    uint64_t effect_pixels{0};
    uint64_t text_glyphs_rasterized{0};
    uint64_t framebuffer_allocations{0};
    uint64_t framebuffer_reuses{0};
    uint64_t dirty_full_fallbacks{0};
    uint64_t dirty_full_fallback_predicted_bounds_missing{0};
    uint64_t dirty_full_fallback_composite_missing_input_bounds{0};
    uint64_t dirty_full_fallback_transform_bounds_unknown{0};
    uint64_t dirty_full_fallback_effect_bounds_unknown{0};
};

} // namespace chronon3d::telemetry
