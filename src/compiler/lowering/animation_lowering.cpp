#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
void lower_animation(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        const auto& layer = plan.layers[i];
        if (layer.animation || !layer.text_animators.empty()) {
            out.animations.push_back({i, 0, 0, i});
            out.parameters.push_back({i, i, 1});
        }
    }
}
}
