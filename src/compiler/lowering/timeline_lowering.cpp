#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
void lower_timeline(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    out.timelines.reserve(plan.layers.size());
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        const auto& layer = plan.layers[i];
        out.timelines.push_back({
            i,
            layer.start_frame.value_or(0).integral(),
            layer.duration_frames.value_or(plan.canvas.duration).integral(),
            i});
    }
}
}
