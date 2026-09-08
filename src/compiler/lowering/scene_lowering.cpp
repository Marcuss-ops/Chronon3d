#include "../compiler_stages.hpp"

#include <utility>

namespace chronon3d::compiler::stages {
void lower_scene(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    out.nodes.reserve(plan.layers.size());
    out.layers.reserve(plan.layers.size());
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        const auto& layer = plan.layers[i];
        NodeKind kind = NodeKind::Color;
        switch (layer.type) {
            case render_plan::LayerType::Image: kind = NodeKind::Image; break;
            case render_plan::LayerType::Video: kind = NodeKind::Video; break;
            case render_plan::LayerType::Text: kind = NodeKind::Text; break;
            case render_plan::LayerType::Color: kind = NodeKind::Color; break;
        }
        out.nodes.push_back({i, kind, 0, 0, i});
        StaticLayerProgram lowered;
        lowered.id = i;
        lowered.name = layer.id;
        lowered.kind = kind;
        lowered.asset = layer.asset;
        lowered.source = layer.source;
        lowered.text = layer.text;
        lowered.font = layer.font;
        lowered.size = layer.size;
        lowered.color = layer.color;
        lowered.position = layer.position;
        lowered.scale = layer.scale;
        lowered.rotation = layer.rotation;
        lowered.size_dimensions = static_cast<std::uint8_t>(layer.size_dimensions);
        lowered.position_dimensions = static_cast<std::uint8_t>(layer.position_dimensions);
        lowered.scale_dimensions = static_cast<std::uint8_t>(layer.scale_dimensions);
        lowered.rotation_dimensions = static_cast<std::uint8_t>(layer.rotation_dimensions);
        lowered.start_frame = layer.start_frame.value_or(Frame{0}).integral();
        lowered.duration_frames = layer.duration_frames.value_or(plan.canvas.duration).integral();
        lowered.opacity = layer.opacity.value_or(1.0f);
        lowered.radius = layer.radius.value_or(0.0f);
        lowered.loop = layer.loop;
        if (layer.fit.has_value()) {
            lowered.fit = static_cast<LayerFitMode>(static_cast<std::uint8_t>(*layer.fit));
        }
        out.layers.push_back(std::move(lowered));
        if (!layer.asset.empty()) out.resources.push_back({i, ResourceKind::Asset, i, 0});
        if (!layer.font.empty()) out.resources.push_back({i, ResourceKind::Font, i, 0});
    }
}
}
