#pragma once

#include <chronon3d/core/types/frame.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

struct RenderPlan;

enum class TemporalRateClass { Static, Cue, KeyframeInterval, PerFrame, Subframe };

struct LayerRateReport {
    std::string layer_id;
    TemporalRateClass rate{TemporalRateClass::Static};
    std::vector<Frame> eval_frames;
    std::uint64_t eval_frame_count{0};
    bool eval_frames_complete{true};
    struct PropertySchedule {
        std::string path;
        TemporalRateClass rate{TemporalRateClass::Static};
        std::vector<Frame> eval_frames;
        std::uint64_t eval_frame_count{0};
        bool eval_frames_complete{true};
    };
    std::vector<PropertySchedule> properties;
};

struct TemporalRateReport {
    double skippable_node_fraction{0.0};
    std::vector<LayerRateReport> layers;
};

[[nodiscard]] const char* temporal_rate_class_name(TemporalRateClass rate) noexcept;
[[nodiscard]] TemporalRateReport analyze_temporal_rates(const RenderPlan& plan);

} // namespace chronon3d::render_plan
