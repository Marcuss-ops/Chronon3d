#include "../compiler_stages.hpp"

#include <chronon3d/math/transform.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>
#include <string>
#include <unordered_map>

namespace chronon3d::compiler::stages {
void lower_scene(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    out.nodes.reserve(plan.layers.size());
    out.layers.reserve(plan.layers.size());
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        const auto& layer = plan.layers[i];
        const auto invalid_payload = [&]() -> std::runtime_error {
            return std::runtime_error("compiler lower_scene: inline mesh contract violation in layer '" + layer.id + "'");
        };
        if (layer.inline_mesh) {
            if (layer.type != render_plan::LayerType::Mesh || !layer.asset.empty() ||
                !layer.source.empty() || layer.inline_mesh->vertices.size() < 3 ||
                layer.inline_mesh->vertices.size() > render_plan::kInlineMeshMaxVertices ||
                layer.inline_mesh->indices.size() < 3 ||
                layer.inline_mesh->indices.size() > render_plan::kInlineMeshMaxIndices ||
                layer.inline_mesh->indices.size() % 3 != 0)
                throw invalid_payload();
            for (const auto& vertex : layer.inline_mesh->vertices) {
                for (const float value : vertex.position)
                    if (!std::isfinite(value) || std::abs(value) > 1'000'000.0f) throw invalid_payload();
                for (const float value : vertex.normal)
                    if (!std::isfinite(value) || std::abs(value) > 1.0f) throw invalid_payload();
                for (const float value : vertex.uv)
                    if (!std::isfinite(value) || std::abs(value) > 1'000'000.0f) throw invalid_payload();
            }
            if (layer.inline_mesh->animation.size() > render_plan::kInlineMeshMaxAnimationSamples)
                throw invalid_payload();
            std::int64_t previous_sample_frame = -1;
            for (const auto& sample : layer.inline_mesh->animation) {
                if (plan.schema != render_plan::kRenderPlanSchemaV3 ||
                    sample.frame <= previous_sample_frame ||
                    sample.frame < 0 || sample.frame >= plan.canvas.duration.integral() ||
                    sample.positions.size() != layer.inline_mesh->vertices.size())
                    throw invalid_payload();
                previous_sample_frame = sample.frame;
                for (const auto& position : sample.positions)
                    for (const float value : position)
                        if (!std::isfinite(value) || std::abs(value) > 1'000'000.0f)
                            throw invalid_payload();
            }
            for (const auto index : layer.inline_mesh->indices)
                if (index >= layer.inline_mesh->vertices.size()) throw invalid_payload();
        } else if (layer.type == render_plan::LayerType::Mesh) {
            throw invalid_payload();
        }
        NodeKind kind = NodeKind::Color;
        switch (layer.type) {
            case render_plan::LayerType::Image: kind = NodeKind::Image; break;
            case render_plan::LayerType::Video: kind = NodeKind::Video; break;
            case render_plan::LayerType::Text: kind = NodeKind::Text; break;
            case render_plan::LayerType::Color: kind = NodeKind::Color; break;
            case render_plan::LayerType::Shape: kind = NodeKind::Shape; break;
            case render_plan::LayerType::Mesh: kind = NodeKind::Mesh; break;
            case render_plan::LayerType::Null:
            default:
                kind = NodeKind::Unknown;
                break;
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
        // Native phrase style positions, shape positions and video box
        // positions are authored in top-left canvas space, while the scene
        // graph uses a canvas-centred basis. Text positions are already box
        // centers; video positions are box origins, so video receives its
        // half-size offset below as well. Shapes follow the same box-center
        // convention as the legacy render-plan compiler
        // (apply_layer_primitives converts Text/Shape identically); without
        // this the modular resolver's canvas-center shift applies twice and
        // every shape lands a half-canvas down-right of its authored spot.
        // Screen-space shapes are already expressed in framebuffer
        // coordinates and must keep their authored position.
        if ((layer.type == render_plan::LayerType::Text ||
             layer.type == render_plan::LayerType::Video ||
             (layer.type == render_plan::LayerType::Shape && !layer.screen_space)) &&
            layer.position_dimensions >= 2) {
            lowered.position[0] -= static_cast<float>(plan.canvas.width) * 0.5f;
            lowered.position[1] -= static_cast<float>(plan.canvas.height) * 0.5f;
            if (layer.type == render_plan::LayerType::Video) {
                const float sx = std::abs(layer.scale[0]) > 1e-6f
                    ? std::abs(layer.scale[0]) : 1.0f;
                const float sy = std::abs(layer.scale[1]) > 1e-6f
                    ? std::abs(layer.scale[1]) : 1.0f;
                const float box_w = layer.size_dimensions == 2
                    ? layer.size[0] / sx : static_cast<float>(plan.canvas.width);
                const float box_h = layer.size_dimensions == 2
                    ? layer.size[1] / sy : static_cast<float>(plan.canvas.height);
                lowered.position[0] += box_w * 0.5f;
                lowered.position[1] += box_h * 0.5f;
            }
        }
        lowered.scale = layer.scale;
        lowered.rotation = layer.rotation;
        lowered.size_dimensions = static_cast<std::uint8_t>(layer.size_dimensions);
        lowered.position_dimensions = static_cast<std::uint8_t>(layer.position_dimensions);
        if (layer.type == render_plan::LayerType::Video &&
            layer.position_dimensions >= 2 && lowered.position_dimensions < 3) {
            // Video box geometry is translated to its center at materialization;
            // carry Z explicitly so the canonical modular resolver does not
            // treat the positioned source as an unpinned 2D layer.
            lowered.position_dimensions = 3;
        }
        lowered.scale_dimensions = static_cast<std::uint8_t>(layer.scale_dimensions);
        lowered.rotation_dimensions = static_cast<std::uint8_t>(layer.rotation_dimensions);
        lowered.start_frame = layer.start_frame.value_or(Frame{0}).integral();
        lowered.duration_frames = layer.duration_frames.value_or(plan.canvas.duration).integral();
        lowered.opacity = layer.opacity.value_or(1.0f);
        lowered.radius = layer.radius.value_or(0.0f);
        lowered.shape = layer.shape;
        lowered.inline_mesh = layer.inline_mesh;
        if (layer.inline_mesh && !layer.inline_mesh->animation.empty()) {
            for (std::size_t vertex = 0; vertex < lowered.inline_mesh->vertices.size(); ++vertex)
                lowered.inline_mesh->vertices[vertex].position =
                    layer.inline_mesh->animation.front().positions[vertex];
        }
        if (layer.type == render_plan::LayerType::Shape && lowered.size_dimensions == 0) {
            lowered.size = {static_cast<float>(plan.canvas.width),
                            static_cast<float>(plan.canvas.height)};
            lowered.size_dimensions = 2;
        }
        lowered.loop = layer.loop;
        if (layer.parent) {
            const auto parent = std::find_if(
                plan.layers.begin(), plan.layers.end(), [&](const auto& candidate) {
                    return candidate.id == *layer.parent;
                });
            if (parent != plan.layers.end())
                lowered.parent_index = static_cast<std::uint32_t>(parent - plan.layers.begin());
        }
        if (layer.track_matte) {
            const auto source = std::find_if(
                plan.layers.begin(), plan.layers.end(), [&](const auto& candidate) {
                    return candidate.id == layer.track_matte->source;
                });
            if (source != plan.layers.end())
                lowered.matte_source_index = static_cast<std::uint32_t>(source - plan.layers.begin());
            switch (layer.track_matte->type) {
                case render_plan::TrackMatteTypePlan::Alpha:
                    lowered.matte_type = LayerTrackMatteType::Alpha; break;
                case render_plan::TrackMatteTypePlan::AlphaInverted:
                    lowered.matte_type = LayerTrackMatteType::AlphaInverted; break;
                case render_plan::TrackMatteTypePlan::Luma:
                    lowered.matte_type = LayerTrackMatteType::Luma; break;
                case render_plan::TrackMatteTypePlan::LumaInverted:
                    lowered.matte_type = LayerTrackMatteType::LumaInverted; break;
            }
        }
        if (layer.time_remap) {
            lowered.time_remap_speed = layer.time_remap->speed.value_or(1.0f) *
                (layer.time_remap->reverse ? -1.0f : 1.0f);
            if (layer.time_remap->freeze_frame)
                lowered.time_remap_freeze_frame = layer.time_remap->freeze_frame->integral();
        }
        if (layer.fit.has_value()) {
            lowered.fit = static_cast<LayerFitMode>(static_cast<std::uint8_t>(*layer.fit));
        }
        Transform static_transform;
        static_transform.position = Vec3{
            lowered.position[0], lowered.position[1], lowered.position[2]};
        static_transform.scale = Vec3{
            lowered.scale[0], lowered.scale[1], lowered.scale[2]};
        static_transform.rotation = glm::quat(glm::radians(Vec3{
            lowered.rotation[0], lowered.rotation[1], lowered.rotation[2]}));
        lowered.static_matrix = static_transform.to_matrix();
        out.layers.push_back(std::move(lowered));
        if (!layer.asset.empty()) out.resources.push_back({i, ResourceKind::Asset, i, 0});
        if (!layer.font.empty()) out.resources.push_back({i, ResourceKind::Font, i, 0});
    }

    // Parent transforms must be sampled before their children while the IR
    // remains indexed in authored layer order for graph bindings.
    std::vector<std::uint8_t> visited(out.layers.size(), 0);
    const auto append_parent_first = [&](const auto& self, std::uint32_t index) -> void {
        if (visited[index] == 2) return;
        if (visited[index] == 1) return; // decoder/compiler relation validation rejects cycles
        visited[index] = 1;
        const auto parent = out.layers[index].parent_index;
        if (parent < out.layers.size()) {
            self(self, parent);
            out.layers[index].hierarchy_depth = out.layers[parent].hierarchy_depth + 1;
        }
        visited[index] = 2;
        out.layer_evaluation_order.push_back(index);
    };
    out.layer_evaluation_order.reserve(out.layers.size());
    for (std::uint32_t index = 0; index < out.layers.size(); ++index)
        append_parent_first(append_parent_first, index);
}
}
