#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

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
    const compiler::CompiledProgram& program,
    const RenderPreparationOptions& options) {
    if (renderer == nullptr) {
        return detail::prepare_render_scene(
            renderer, Scene{}, CompositionSpec{}, options);
    }
    const auto& metadata = program.metadata();
    const FrameRate canvas_fps{metadata.fps_numerator, metadata.fps_denominator};
    // Preparation needs graph topology only. Frame-varying values are sampled
    // by TypedProgramSampler at execution time and are not materialized here.
    const auto& runtime = *renderer->execution_context().runtime;
    Scene scene = compiler::SceneMaterializer{}.materialize_scene(
        program, std::pmr::get_default_resource(), &runtime.font_engine());
    auto result = detail::prepare_render_scene(
        renderer, scene,
        CompositionSpec{
            .name = metadata.job_id,
            .width = static_cast<i32>(metadata.width),
            .height = static_cast<i32>(metadata.height),
            .frame_rate = canvas_fps,
            .duration = Frame{metadata.duration_frames}},
        options);
    if (result.ok() && options.warmup_renderer) {
        result.warmup = warmup_renderer(*renderer, program, options.warmup);
        result.warmup_performed = true;
    }
    return result;
}

} // namespace chronon3d::runtime
