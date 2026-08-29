#include "direct_yuv_program.hpp"

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>

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
    void* cuda_context,
    std::string& reason) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)compiled; (void)image_cache; (void)cuda_context;
    reason = "CUDA interop is not compiled";
    return nullptr;
#else
    if (!compiled.composition) {
        reason = "compiled composition is empty";
        return nullptr;
    }
    if (!cuda_context) {
        reason = "encoder did not expose its CUDA context";
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
    } overlay;
    bool have_overlay = false;
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
            if (have_overlay) {
                reason = "more than one image overlay";
                return nullptr;
            }
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
            overlay.path = image.path;
            overlay.x0 = static_cast<float>(compiled.composition->width()) * 0.5f +
                         layer.transform.position.x +
                         node.world_transform.position.x - w * 0.5f;
            overlay.y0 = static_cast<float>(compiled.composition->height()) * 0.5f +
                         layer.transform.position.y +
                         node.world_transform.position.y - h * 0.5f;
            overlay.x1 = overlay.x0 + w;
            overlay.y1 = overlay.y0 + h;
            overlay.opacity = image.opacity * node.world_transform.opacity *
                              layer.transform.opacity;
            overlay.options = image.decode_options;
            have_overlay = true;
        }
    }
    if (video_path.empty()) {
        reason = "no video source";
        return nullptr;
    }
    if (!have_overlay) {
        reason = "direct program currently requires one static image overlay";
        return nullptr;
    }

    const auto img_t0 = profiling::now();
    auto cached = image_cache.get_or_load(
        overlay.path, overlay.options);
    const double watermark_load_ms = profiling::duration_ms(img_t0, profiling::now());
    if (!cached || !cached->valid() || cached->gpu_rgba.empty()) {
        reason = "watermark is not available through the canonical ImageCache";
        return nullptr;
    }
    if (cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context)) != CUDA_SUCCESS) {
        reason = "failed to select encoder CUDA context";
        return nullptr;
    }
    auto resource = std::make_shared<media::CudaImageResource>();
    const std::size_t bytes = cached->gpu_rgba.size() * sizeof(float);
    const auto upload_t0 = profiling::now();
    if (cuMemAlloc(&resource->ptr, bytes) != CUDA_SUCCESS ||
        cuMemcpyHtoD(resource->ptr, cached->gpu_rgba.data(), bytes) != CUDA_SUCCESS) {
        reason = "failed to upload watermark into CUDA resident memory";
        return nullptr;
    }
    resource->width = static_cast<std::uint32_t>(cached->width);
    resource->height = static_cast<std::uint32_t>(cached->height);
    resource->pitch_bytes = static_cast<std::size_t>(cached->width) * sizeof(float) * 4;
    const double watermark_upload_ms = profiling::duration_ms(upload_t0, profiling::now());

    auto program = std::shared_ptr<DirectYuvProgram>(new DirectYuvProgram());
    program->video_path_ = std::move(video_path);
    program->width_ = compiled.composition->width();
    program->height_ = compiled.composition->height();
    program->scene_eval_ms_ = scene_eval_ms;
    program->watermark_load_ms_ = watermark_load_ms;
    program->watermark_upload_ms_ = watermark_upload_ms;
    auto template_frame = std::make_shared<DirectYuvTemplate>();
    template_frame->batch.instances.push_back(runtime::LayerInstance{
        .kind = runtime::PrimitiveKind::Image,
        .resource_index = 0,
        .src_x0 = 0.0f, .src_y0 = 0.0f, .src_x1 = 1.0f, .src_y1 = 1.0f,
        .dst_x0 = overlay.x0, .dst_y0 = overlay.y0,
        .dst_x1 = overlay.x1, .dst_y1 = overlay.y1,
        .opacity = overlay.opacity,
        .blend = BlendMode::Normal});
    media::CudaLayerResource layer;
    layer.rgba = resource->ptr;
    layer.pitch_bytes = static_cast<int>(resource->pitch_bytes);
    layer.width = resource->width;
    layer.height = resource->height;
    template_frame->resources.push_back(layer);
    template_frame->resource_owner = std::move(resource);
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
