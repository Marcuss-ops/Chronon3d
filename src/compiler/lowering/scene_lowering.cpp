#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
void lower_scene(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    out.nodes.reserve(plan.layers.size());
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        NodeKind kind = NodeKind::Color;
        switch (plan.layers[i].type) {
            case render_plan::LayerType::Image: kind = NodeKind::Image; break;
            case render_plan::LayerType::Video: kind = NodeKind::Video; break;
            case render_plan::LayerType::Text: kind = NodeKind::Text; break;
            case render_plan::LayerType::Color: kind = NodeKind::Color; break;
        }
        out.nodes.push_back({i, kind, 0, 0, i});
        if (!plan.layers[i].asset.empty()) out.resources.push_back({i, ResourceKind::Asset, i, 0});
        if (!plan.layers[i].font.empty()) out.resources.push_back({i, ResourceKind::Font, i, 0});
    }
}
}
