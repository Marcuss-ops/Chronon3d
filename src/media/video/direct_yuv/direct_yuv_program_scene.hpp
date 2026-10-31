#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace chronon3d::media::video::detail {

inline std::shared_ptr<const Scene> materialize_program_topology(
    const compiler::CompiledProgram& program) {
    return std::make_shared<const Scene>(
        compiler::SceneMaterializer{}.materialize_scene(program));
}

inline Scene sample_program_scene(
    const compiler::CompiledProgram& program,
    const Scene& topology,
    Frame frame) {
    Scene scene = topology.clone();
    const auto parameters = compiler::TypedProgramSampler{}.sample(program, frame);
    for (const auto& layer_program : program.layers()) {
        const auto* sampled = compiler::find_layer(parameters, layer_program.id);
        if (!sampled) continue;
        const std::string name = layer_program.name.empty()
            ? "layer_" + std::to_string(layer_program.id)
            : layer_program.name;
        for (auto& layer : scene.layers()) {
            if (std::string_view{layer.name} != std::string_view{name}) continue;
            layer.transform = from_mat4(sampled->matrix, sampled->opacity);
            layer.visible = sampled->active;
            break;
        }
    }
    return scene;
}

} // namespace chronon3d::media::video::detail
