#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
void lower_text(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        const auto& layer = plan.layers[i];
        if (layer.type != render_plan::LayerType::Text) continue;
        const auto text_index = static_cast<std::uint32_t>(out.text_values.size());
        out.text_values.push_back(layer.text);
        const auto program_index = static_cast<std::uint32_t>(out.texts.size());
        out.texts.push_back({
            i, text_index, static_cast<std::uint32_t>(layer.text_animators.size())});
        out.layers[i].text_program_index = program_index;
    }
}
}
