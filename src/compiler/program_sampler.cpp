#include <chronon3d/compiler/program_sampler.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <cmath>

namespace chronon3d::compiler {

FrameParameterBlock
ProgramSampler::sample(const CompiledProgram& program, Frame frame) const {
    return TypedProgramSampler{}.sample(program, frame);
}

Result<EvaluatedCompositionFrame, CompositionEvaluateError>
ProgramSampler::sample(const CompiledProgram& program,
                       const CompositionEvaluateContext& context,
                       Frame frame) const {
    const auto frame_params = TypedProgramSampler{}.sample(program, frame);
    const auto& meta = program.metadata();

    EvaluatedCompositionFrame result(context.frame_context.resource);
    SceneBuilder scene(context.frame_context.resource);

    const auto& layers = program.layers();
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto& layer = layers[i];
        const auto& param = (i < frame_params.layers.size()) ? frame_params.layers[i] : SampledLayerParameters{};
        if (!param.active) continue;

        const std::string name = layer.name.empty() ? ("layer_" + std::to_string(layer.id)) : layer.name;
        scene.layer(name, [&](LayerBuilder& builder) {
            builder.opacity(param.opacity);
            builder.transform(param.matrix);

            switch (layer.kind) {
                case NodeKind::Color: {
                    RectParams params;
                    params.size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    params.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                    builder.rect("color", std::move(params));
                    break;
                }
                case NodeKind::Image: {
                    ImageParams params;
                    params.asset_path = layer.asset;
                    params.size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    params.radius = layer.radius;
                    builder.image("image", std::move(params));
                    break;
                }
                case NodeKind::Video: {
                    video::VideoSource source{.path = layer.source.empty() ? layer.asset : layer.source};
                    if (layer.size_dimensions == 2) {
                        const float sx = std::abs(layer.scale[0]) > 1e-6f ? std::abs(layer.scale[0]) : 1.0f;
                        const float sy = std::abs(layer.scale[1]) > 1e-6f ? std::abs(layer.scale[1]) : 1.0f;
                        source.size = {layer.size[0] / sx, layer.size[1] / sy};
                    }
                    if (layer.loop) source.loop_mode = video::VideoLoopMode::Loop;
                    builder.video(std::move(source));
                    break;
                }
                case NodeKind::Text: {
                    builder.kind(LayerKind::Text);
                    break;
                }
                default:
                    break;
            }
        });
    }

    result.scene = scene.build();
    return result;
}

}  // namespace chronon3d::compiler
