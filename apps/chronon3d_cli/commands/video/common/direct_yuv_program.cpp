#include "direct_yuv_program.hpp"

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#include <cmath>
#include <stdexcept>

namespace chronon3d::cli {
namespace {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
bool identity_2d(const Transform& t) {
    return std::abs(t.rotation.w - 1.0f) < 1e-4f &&
           std::abs(t.rotation.x) < 1e-4f &&
           std::abs(t.rotation.y) < 1e-4f &&
           std::abs(t.rotation.z) < 1e-4f &&
           std::abs(t.scale.x - 1.0f) < 1e-4f &&
           std::abs(t.scale.y - 1.0f) < 1e-4f &&
           std::abs(t.scale.z - 1.0f) < 1e-4f &&
           std::abs(t.position.z) < 1e-4f &&
           std::abs(t.anchor.z) < 1e-4f;
}
#endif

} // namespace

std::shared_ptr<DirectYuvProgram> DirectYuvProgram::prepare(
    const CompiledComposition& compiled,
    ImageCache& image_cache,
    std::shared_ptr<media::VideoDeviceRuntime> video_runtime,
    std::string& reason) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)compiled; (void)image_cache; (void)video_runtime;
    reason = "CUDA interop is not compiled";
    return nullptr;
#else
    if (!compiled.composition) {
        reason = "compiled composition is empty";
        return nullptr;
    }
    if (!video_runtime) {
        reason = "video device runtime is unavailable";
        return nullptr;
    }

    // Evaluate the canonical scene once at frame zero.  The current direct
    // contract admits only a static topology with constant transforms; any
    // temporal scene must stay on the general resolver/RenderGraph path.
    const auto rate = compiled.composition->frame_rate();
    const auto context = make_frame_context({
        .global_time = SampleTime::from_frame(0.0, rate),
        .duration = compiled.composition->duration(),
        .width = compiled.composition->width(),
        .height = compiled.composition->height(),
    });
    Scene scene;
    const auto scene_t0 = profiling::now();
    try {
        scene = compiled.composition->evaluate(context);
    } catch (const std::exception& error) {
        reason = std::string("scene evaluation failed: ") + error.what();
        return nullptr;
    }
    const double scene_eval_ms = profiling::duration_ms(scene_t0, profiling::now());

    std::string video_path;
    struct Overlay {
        std::string path;
        float x0{}, y0{}, x1{}, y1{};
        float opacity{1.0f};
        ImageDecodeOptions options{};
    };
    std::vector<Overlay> overlays;
    for (const auto& layer : scene.layers()) {
        if (!layer.visible || layer.uses_2_5d_projection || layer.screen_space ||
            layer.mask.enabled() || layer.blend_mode != BlendMode::Normal ||
            !layer.effects().empty() || !identity_2d(layer.transform)) {
            reason = "layer uses unsupported visibility, mask, effect, blend, or transform";
            return nullptr;
        }
        if (layer.video_source) {
            if (!video_path.empty()) {
                reason = "more than one video source";
                return nullptr;
            }
            video_path = layer.video_source->path;
            continue;
        }
        for (const auto& node : layer.nodes) {
            if (!node.visible || node.shape.type() != ShapeType::Image ||
                node.corner_radius > 0.0f) {
                reason = "scene contains a non-simple image node";
                return nullptr;
            }
            const auto& image = node.shape.image();
            if (image.path.empty() || image.crop.enabled ||
                !identity_2d(node.world_transform)) {
                reason = "image crop or transform is unsupported";
                return nullptr;
            }
            const float w = image.size.x;
            const float h = image.size.y;
            if (!(w > 0.0f && h > 0.0f)) {
                reason = "image dimensions are invalid";
                return nullptr;
            }
            Overlay ov;
            ov.path = image.path;
            ov.x0 = static_cast<float>(compiled.composition->width()) * 0.5f +
                    layer.transform.position.x +
                    node.world_transform.position.x - w * 0.5f;
            ov.y0 = static_cast<float>(compiled.composition->height()) * 0.5f +
                    layer.transform.position.y +
                    node.world_transform.position.y - h * 0.5f;
            ov.x1 = ov.x0 + w;
            ov.y1 = ov.y0 + h;
            ov.opacity = image.opacity * node.world_transform.opacity *
                         layer.transform.opacity;
            ov.options = image.decode_options;
            overlays.push_back(std::move(ov));
        }
    }
    if (video_path.empty()) {
        reason = "no video source";
        return nullptr;
    }
    double watermark_load_ms = 0.0;
    double watermark_upload_ms = 0.0;
    auto template_frame = std::make_shared<DirectYuvTemplate>();

    for (const auto& ov : overlays) {
        const auto img_t0 = profiling::now();
        auto cached = image_cache.get_or_load(ov.path, ov.options);
        watermark_load_ms += profiling::duration_ms(img_t0, profiling::now());
        if (!cached || !cached->valid() || cached->gpu_rgba.empty()) {
            reason = "overlay is not available through the canonical ImageCache: " + ov.path;
            return nullptr;
        }
        bool cache_hit = false;
        double upload_ms = 0.0;
        auto resource = video_runtime->get_or_upload_image(
            cached->gpu_key.content_digest, ov.options,
            static_cast<std::uint32_t>(cached->width),
            static_cast<std::uint32_t>(cached->height), cached->gpu_rgba,
            cache_hit, upload_ms, reason);
        if (!resource) return nullptr;
        watermark_upload_ms += upload_ms;

        uint32_t res_idx = static_cast<uint32_t>(template_frame->resources.size());
        template_frame->batch.instances.push_back(runtime::LayerInstance{
            .kind = runtime::PrimitiveKind::Image,
            .resource_index = res_idx,
            .src_x0 = 0.0f, .src_y0 = 0.0f, .src_x1 = 1.0f, .src_y1 = 1.0f,
            .dst_x0 = ov.x0, .dst_y0 = ov.y0,
            .dst_x1 = ov.x1, .dst_y1 = ov.y1,
            .opacity = ov.opacity,
            .blend = BlendMode::Normal});
        media::CudaLayerResource layer;
        layer.rgba = resource->ptr;
        layer.pitch_bytes = static_cast<int>(resource->pitch_bytes);
        layer.width = resource->width;
        layer.height = resource->height;
        template_frame->resources.push_back(layer);
        if (!template_frame->resource_owner) {
            template_frame->resource_owner = resource;
        }
        template_frame->resource_owners.push_back(std::move(resource));
    }

    auto program = std::shared_ptr<DirectYuvProgram>(new DirectYuvProgram());
    program->video_path_ = std::move(video_path);
    program->width_ = compiled.composition->width();
    program->height_ = compiled.composition->height();
    program->scene_eval_ms_ = scene_eval_ms;
    program->watermark_load_ms_ = watermark_load_ms;
    program->watermark_upload_ms_ = watermark_upload_ms;
    program->template_frame_ = std::move(template_frame);
    return program;
#endif
}

std::shared_ptr<DirectYuvFrame> DirectYuvProgram::execute(
    media::NativeVideoFrameDecoder& decoder, Frame frame) const {
    auto decoded = decoder.decode_native_frame(
        video_path_, frame, width_, height_, 0.0f);
    if (!decoded || !template_frame_) return nullptr;
    auto result = std::make_shared<DirectYuvFrame>();
    result->decoded = std::move(decoded);
    result->program = template_frame_;
    return result;
}

} // namespace chronon3d::cli
