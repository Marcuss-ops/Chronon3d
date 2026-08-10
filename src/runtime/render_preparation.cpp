#include <chronon3d/runtime/render_preparation.hpp>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <algorithm>
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

namespace {

RenderPreparationResult prepare_render_scene(
    SoftwareRenderer* renderer,
    const Scene& scene,
    const CompositionSpec& spec,
    const RenderPreparationOptions& options) {
    RenderPreparationResult result;
    if (renderer == nullptr) {
        result.preparation_error = PreparationError{
            .code = PreparationError::Code::InternalError,
            .message = "Render preparation requires a renderer",
            .phase = "setup",
        };
        return result;
    }

    // The scene has already been materialized by the canonical compiled
    // composition path. Preparation validates exactly that payload.
    result.preflight = AssetPreflightResolver::check(
        scene, renderer->runtime().resolver(), options.preflight_mode,
        options.reference_frame);
    if (!result.preflight.ok()) {
        const auto issue = std::find_if(
            result.preflight.issues.begin(), result.preflight.issues.end(),
            [](const auto& candidate) {
                return candidate.severity == PreflightSeverity::Error;
            });
        const auto& error_issue = issue != result.preflight.issues.end()
            ? *issue : result.preflight.issues.front();
        result.preparation_error = PreparationError{
            .code = PreparationError::Code::PreflightFailed,
            .message = error_issue.message.empty()
                ? "asset preflight validation failed"
                : error_issue.message,
            .cause_code = error_issue.code,
            .path = error_issue.path,
            .owner = error_issue.layer_id,
            .phase = "preflight",
        };
        return result;
    }

    auto resource_options = options.resources;
    resource_options.mesh_cache = &renderer->runtime().mesh_cache();
    // An injected MediaFrameProvider is the authoritative video source for
    // this render.  It may intentionally support synthetic/test media and
    // therefore cannot be probed through the optional native FFmpeg path.
    // Keep manifest existence validation above, but do not reject a valid
    // provider-backed render merely because native metadata probing is off.
    if (renderer->video_decoder() != nullptr) {
        resource_options.prepare_video_metadata = false;
    }
    auto prepared = ResourcePreparation::prepare(
        scene.asset_manifest(), renderer->runtime().resolver(), resource_options);
    if (!prepared.has_value()) {
        result.preparation_error = std::move(prepared.error());
        return result;
    }
    result.prepared_assets = std::move(prepared.value());

    // Complete the two phases that have canonical runtime services. The
    // ResourcePreparation pass above remains the deterministic manifest
    // barrier; these calls perform the actual cache population before the
    // first frame and therefore make invalid payloads fail early.
    if (options.resources.prepare_fonts &&
        !scene.asset_manifest().filter(assets::AssetKind::Font).empty()) {
        const auto fonts = renderer->preflight_fonts(
            scene, renderer->runtime().resolver());
        if (fonts.preflight_missing != 0) {
            result.preparation_error = PreparationError{
                .code = PreparationError::Code::CorruptedAsset,
                .message = "one or more fonts could not be loaded",
                .phase = "font",
            };
            return result;
        }
    }

    if (options.resources.prepare_images) {
        for (const auto& ref : scene.asset_manifest().filter(assets::AssetKind::Image)) {
            const auto image = renderer->runtime().image_cache().get_or_load(ref.path);
            if (!image || !image->valid()) {
                result.preparation_error = PreparationError{
                    .code = PreparationError::Code::CorruptedAsset,
                    .message = "image could not be decoded: " + ref.path,
                    .path = ref.path,
                    .owner = ref.owner,
                    .phase = "image",
                };
                return result;
            }
            if (auto it = result.prepared_assets->images.find(ref.owner);
                it != result.prepared_assets->images.end()) {
                it->second.width = image->width;
                it->second.height = image->height;
            }
        }
    }

    return result;
}

} // namespace

RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const Composition& composition,
    const RenderPreparationOptions& options) {
    if (renderer == nullptr) {
        return prepare_render_scene(renderer, Scene{},
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
    auto result = prepare_render_scene(renderer, composition.evaluate(context),
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
    if (renderer == nullptr || !compiled.definition) {
        return prepare_render_scene(renderer, Scene{}, CompositionSpec{}, options);
    }
    const auto& runtime = renderer->runtime();
    const auto context = make_frame_context({
        .global_time = SampleTime::from_frame_int(
            options.reference_frame, compiled.definition->composition.frame_rate),
        .duration = compiled.definition->composition.duration,
        .width = compiled.definition->composition.width,
        .height = compiled.definition->composition.height,
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
    auto result = prepare_render_scene(renderer, scene,
        compiled.definition->composition, options);
    if (result.ok() && options.warmup_renderer) {
        result.warmup = warmup_renderer(*renderer, compiled, options.warmup);
        result.warmup_performed = true;
    }
    return result;
}

} // namespace chronon3d::runtime
