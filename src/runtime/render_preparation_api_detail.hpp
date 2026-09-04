#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "render_preparation_internal.hpp"

#include <sstream>

namespace chronon3d::runtime {

std::string RenderPreparationResult::diagnostic() const {
    std::ostringstream out;
    for (const auto& issue : preflight.issues) {
        out << issue.message << '\n';
    }
    if (preparation_error) {
        out << preparation_error->message;
        if (!preparation_error->cause_code.empty()) {
            out << " [" << preparation_error->cause_code << ']';
        }
        if (!preparation_error->path.empty()) {
            out << " (" << preparation_error->path << ')';
        }
    }
    return out.str();
}

RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const Composition& composition,
    const RenderPreparationOptions& options) {
    if (renderer == nullptr) {
        return detail::prepare_render_scene(renderer, Scene{},
            CompositionSpec{}, options);
    }
    const auto sample = SampleTime::from_frame_int(
        options.reference_frame, composition.frame_rate());
    const auto& runtime = renderer->runtime();
    const auto context = make_frame_context({
        .global_time = sample,
        .duration = composition.duration(),
        .width = composition.width(),
        .height = composition.height(),
        .assets_root = runtime.resolver().mount_root().string(),
        .font_engine = &runtime.font_engine(),
        .runtime = &runtime,
    });
    auto result = detail::prepare_render_scene(renderer, composition.evaluate(context),
        CompositionSpec{
            .name = composition.name(),
            .width = composition.width(),
            .height = composition.height(),
            .frame_rate = composition.frame_rate(),
            .duration = composition.duration()}, options);
    if (result.ok() && options.warmup_renderer) {
        result.warmup = warmup_renderer(*renderer, composition, options.warmup);
        result.warmup_performed = true;
    }
    return result;
}

RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const CompiledComposition& compiled,
    const RenderPreparationOptions& options) {
    if (renderer == nullptr || !compiled.composition) {
        return detail::prepare_render_scene(
            renderer, Scene{}, CompositionSpec{}, options);
    }
    const auto& runtime = renderer->runtime();
    const auto context = make_frame_context({
        .global_time = SampleTime::from_frame_int(
            options.reference_frame, compiled.composition->frame_rate()),
        .duration = compiled.composition->duration(),
        .width = compiled.composition->width(),
        .height = compiled.composition->height(),
        .assets_root = runtime.resolver().mount_root().string(),
        .font_engine = &runtime.font_engine(),
        .runtime = &runtime,
    });
    const auto evaluated = chronon3d::evaluate(
        compiled,
        CompositionEvaluateContext{.frame_context = context},
        options.reference_frame);
    if (!evaluated) {
        RenderPreparationResult result;
        result.preparation_error = PreparationError{
            .code = PreparationError::Code::InternalError,
            .message = evaluated.error().message,
            .phase = "compiled evaluation",
        };
        return result;
    }
    auto& evaluated_frame = evaluated.value();
    Scene scene = evaluated_frame.scene.clone();
    if (evaluated_frame.camera) scene.set_camera_2_5d(*evaluated_frame.camera);
    auto result = detail::prepare_render_scene(
        renderer, scene, compiled.composition->spec(), options);
    if (result.ok() && options.warmup_renderer) {
        result.warmup = warmup_renderer(*renderer, compiled, options.warmup);
        result.warmup_performed = true;
    }
    return result;
}

} // namespace chronon3d::runtime
