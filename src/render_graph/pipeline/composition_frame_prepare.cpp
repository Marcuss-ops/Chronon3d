#include "composition_frame_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace chronon3d::graph::detail {
namespace {
const CompositionSpec& require_composition_spec(const CompiledComposition& compiled) {
    if (!compiled.composition) {
        throw std::invalid_argument("compiled composition has no definition");
    }
    return compiled.composition->spec();
}
}  // namespace

CompositionFrameState::CompositionFrameState(
    RenderBackend& backend_value,
    cache::NodeCache& node_cache_value,
    const RenderSettings& settings_value,
    const CompositionRegistry* registry_value,
    media::MediaFrameProvider* video_decoder_value,
    const CompiledComposition& compiled_value,
    Frame frame_value,
    SoftwareRenderer* sw_sidecar_value,
    CancellationToken* cancellation_value,
    runtime::FrameExecution* execution_value)
    : backend(backend_value), node_cache(node_cache_value), settings(settings_value),
      registry(registry_value), video_decoder(video_decoder_value),
      compiled(compiled_value), frame(frame_value), sw_sidecar(sw_sidecar_value),
      cancellation(cancellation_value), execution(execution_value),
      frame_runtime(sw_sidecar_value != nullptr
          ? sw_sidecar_value->execution_context().runtime : nullptr),
      spec(require_composition_spec(compiled_value)),
      frame_assets_root(frame_runtime
          ? frame_runtime->resolver().mount_root().string() : std::string{}),
      ssaa(std::max(1.0f, settings_value.ssaa_factor)),
      w(spec.width), h(spec.height), rw(0), rh(0),
      temporal_accumulation(
          settings_value.motion_blur.mode == MotionBlurMode::TemporalAccumulation &&
          settings_value.motion_blur.samples > 1 &&
          settings_value.motion_blur.shutter_angle_deg > 0.0f) {
    if (!std::isfinite(settings.ssaa_factor)) {
        throw std::invalid_argument("SSAA factor must be finite");
    }
    if (w <= 0 || h <= 0 || !std::isfinite(ssaa)) {
        throw std::invalid_argument("render dimensions and SSAA must be finite and positive");
    }
    const double scaled_width = static_cast<double>(w) * static_cast<double>(ssaa);
    const double scaled_height = static_cast<double>(h) * static_cast<double>(ssaa);
    if (scaled_width > static_cast<double>(std::numeric_limits<int>::max()) ||
        scaled_height > static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("SSAA render dimensions exceed the integer framebuffer limit");
    }
    rw = static_cast<int>(scaled_width);
    rh = static_cast<int>(scaled_height);
}

Scene CompositionFrameState::evaluate_scene(const FrameContext& context) const {
    auto evaluated = chronon3d::evaluate(
        compiled,
        CompositionEvaluateContext{.frame_context = context},
        context.local_time());
    if (!evaluated) {
        throw std::runtime_error(
            "compiled composition evaluation failed: " + evaluated.error().message);
    }
    return std::move(evaluated.value().scene);
}

std::shared_ptr<Framebuffer> CompositionFrameState::call_graph(
    const Scene& scene,
    Frame target_frame,
    f32 time,
    const TemporalRenderContext* temporal_context,
    const RenderSettings* graph_settings) const {
    const auto& effective_settings = graph_settings ? *graph_settings : settings;
    Camera context_camera{};
    if (temporal_context) {
        return render_scene_via_graph_temporal(
            backend, node_cache, scene, context_camera,
            rw, rh, target_frame, time, effective_settings,
            registry, video_decoder, static_cast<float>(spec.frame_rate.fps()),
            spec.name, sw_sidecar, temporal_context, execution);
    }
    return render_scene_via_graph(
        backend, node_cache, scene, context_camera,
        rw, rh, target_frame, time, effective_settings,
        registry, video_decoder, static_cast<float>(spec.frame_rate.fps()),
        spec.name, sw_sidecar, execution);
}

}  // namespace chronon3d::graph::detail
