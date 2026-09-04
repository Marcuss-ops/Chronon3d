#include "direct_yuv_program.hpp"
#include "text_texture_cache.hpp"

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace chronon3d::cli {
namespace {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
bool is_2d_transform(const Transform& t) {
    return std::abs(t.rotation.x) < 1e-3f &&
           std::abs(t.rotation.y) < 1e-3f &&
           std::abs(t.position.z) < 1e-3f &&
           std::abs(t.anchor.z) < 1e-3f;
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

    const auto duration = compiled.composition->duration();
    const auto rate = compiled.composition->frame_rate();
    const int width = compiled.composition->width();
    const int height = compiled.composition->height();

    const auto scene_t0 = profiling::now();
    const auto context_0 = make_frame_context({
        .global_time = SampleTime::from_frame(0.0, rate),
        .duration = duration,
        .width = width,
        .height = height,
    });
    Scene scene_0;
    try {
        scene_0 = compiled.composition->evaluate(context_0);
    } catch (const std::exception& error) {
        reason = std::string("scene evaluation failed: ") + error.what();
        return nullptr;
    }
    const double scene_eval_ms = profiling::duration_ms(scene_t0, profiling::now());

    std::string video_path;
    std::vector<DirectVideoLayer> video_layers;
    for (const auto& layer : scene_0.layers()) {
        if (layer.uses_2_5d_projection || layer.mask.enabled()) {
            reason = "layer '" + std::string(layer.name) + "' uses unsupported 3D (" +
                     (layer.uses_2_5d_projection ? "yes" : "no") + ") or mask (" +
                     (layer.mask.enabled() ? "yes" : "no") + ")";
            return nullptr;
        }
        if (!is_2d_transform(layer.transform)) {
            reason = "layer uses 3D transform";
            return nullptr;
        }
        if (layer.video_source) {
            if (video_path.empty()) {
                video_path = layer.video_source->path;
            } else {
                video_layers.push_back(DirectVideoLayer{
                    std::string(layer.name), *layer.video_source});
            }
        }
    }

    if (video_path.empty()) {
        reason = "no video source";
        return nullptr;
    }

    std::unordered_map<std::string, DirectLayerResourceEntry> layer_resources;
    std::vector<std::shared_ptr<const media::CudaImageResource>> persistent_resources;
    double watermark_load_ms = 0.0;
    double watermark_upload_ms = 0.0;

    // ── Resource manifest preparation (compile once, not per frame) ────
    // The pre-pass used to evaluate() every frame of the timeline to
    // discover which layers appear anywhere in the composition — an entire
    // extra logical traversal of the timeline before the render loop ran
    // its own.  Composition layer membership is static: every layer that
    // ever exists is present in a single evaluation, with its own from /
    // duration window deciding visibility over time.  Static (non-animated)
    // layers are prepared from that one scene directly; animated layers
    // (keyframes or expressions on transform/opacity) get at most one
    // representative evaluation inside their window for asset discovery,
    // because image paths and text content are time-invariant for a layer.
    const auto prepare_t0 = profiling::now();
    const auto collect_layer = [&](const Layer& layer) -> bool {
        // Returns true when the layer has been prepared (resource uploaded
        // or explicitly skipped); false on a fatal failure.
        if (layer.video_source) return true;
        const std::string layer_name = std::string(layer.name);
        if (layer_resources.find(layer_name) != layer_resources.end()) return true;
        if (layer.nodes.empty()) return true;

        const auto& node = layer.nodes[0];
        if (node.shape.type() == ShapeType::Image) {
            const auto& image = node.shape.image();
            if (image.path.empty()) return true;
            const auto img_t0 = profiling::now();
            auto cached = image_cache.get_or_load(image.path, image.decode_options);
            watermark_load_ms += profiling::duration_ms(img_t0, profiling::now());
            if (!cached || !cached->valid() || cached->gpu_rgba.empty()) {
                reason = "overlay is not available through the canonical ImageCache: " + image.path;
                return false;
            }
            bool cache_hit = false;
            double upload_ms = 0.0;
            auto resource = video_runtime->get_or_upload_image(
                cached->gpu_key.content_digest, image.decode_options,
                static_cast<std::uint32_t>(cached->width),
                static_cast<std::uint32_t>(cached->height), cached->gpu_rgba,
                cache_hit, upload_ms, reason);
            if (!resource) return false;
            watermark_upload_ms += upload_ms;

            DirectLayerResourceEntry entry;
            entry.gpu_resource = resource;
            entry.native_width = static_cast<float>(cached->width);
            entry.native_height = static_cast<float>(cached->height);
            entry.corner_radius = 0.0f;
            layer_resources[layer_name] = entry;
            persistent_resources.push_back(resource);
            return true;
        }
        if (node.shape.type() == ShapeType::TextRun || layer.kind == LayerKind::Text) {
#ifdef CHRONON3D_USE_BLEND2D
            std::string text_content;
            std::string font_path;
            float font_size = 58.0f;
            Color fill_color{1.0f, 1.0f, 1.0f, 1.0f};
            Color stroke_color{0.0f, 0.0f, 0.0f, 0.0f};
            float stroke_width = 0.0f;
            bool has_bg = false;
            Color bg_color{0.05f, 0.05f, 0.09f, 0.88f};
            float bg_opacity = 0.88f;
            float bg_radius = 10.0f;
            float pad_x = 20.0f;
            float pad_y = 12.0f;
            float box_w = 0.0f;
            float box_h = 0.0f;
            std::string align = "center";

            if (node.shape.type() == ShapeType::TextRun) {
                const auto handle = node.shape.text_run_shape_handle();
                if (handle.value) {
                    const auto& s = *handle.value;
                    if (s.layout) {
                        text_content = s.layout->source_text;
                        if (!s.layout->font.font_path.empty()) font_path = s.layout->font.font_path;
                        if (s.layout->font_size > 0.0f) font_size = s.layout->font_size;
                        box_w = s.layout->bounds.x;
                        box_h = s.layout->bounds.y;
                    }
                    fill_color = s.paint.fill;
                    if (s.paint.stroke_enabled && s.paint.stroke_width > 0.0f) {
                        stroke_color = s.paint.stroke_color;
                        stroke_width = s.paint.stroke_width;
                    }
                }
            }

            if (font_path.empty()) {
                reason = "text layer has no prepared font asset: " + layer_name;
                return false;
            }
            if (node.color.a > 0.0f) fill_color = node.color;
            if (text_content.empty()) text_content = layer_name;

            const auto text_t0 = profiling::now();
            auto rasterized = rasterize_text_texture(
                text_content, font_path, font_size, fill_color, stroke_color, stroke_width,
                has_bg, bg_color, bg_opacity, bg_radius, pad_x, pad_y, box_w, box_h, align);
            watermark_load_ms += profiling::duration_ms(text_t0, profiling::now());

            bool cache_hit = false;
            double upload_ms = 0.0;
            auto resource = video_runtime->get_or_upload_image(
                rasterized.digest, ImageDecodeOptions{},
                static_cast<std::uint32_t>(rasterized.width),
                static_cast<std::uint32_t>(rasterized.height), rasterized.gpu_rgba,
                cache_hit, upload_ms, reason);
            if (!resource) return false;
            watermark_upload_ms += upload_ms;

            DirectLayerResourceEntry entry;
            entry.gpu_resource = resource;
            entry.native_width = static_cast<float>(rasterized.width);
            entry.native_height = static_cast<float>(rasterized.height);
            layer_resources[layer_name] = entry;
            persistent_resources.push_back(resource);
            return true;
#else
            reason = "Blend2D is required for direct text rasterization";
            return false;
#endif
        }
        return true;
    };

    const auto layer_is_static = [](const Layer& layer) -> bool {
        const auto& t = layer.anim_transform;
        const auto animated = [](const auto& value) {
            return value.is_animated() || value.has_expression();
        };
        return !animated(t.position) && !animated(t.rotation_euler) &&
               !animated(t.scale) && !animated(t.anchor) &&
               !animated(t.opacity) && !animated(t.blur);
    };

    // Pass 1: prepare every static layer straight from the frame-0 scene.
    for (const auto& layer : scene_0.layers()) {
        if (!layer_is_static(layer)) continue;
        if (!collect_layer(layer)) return nullptr;
    }
    // Pass 2: animated layers need one evaluation inside their active window
    // to expose the layer at all (frame 0 may precede its from-frame). Text
    // content and image paths are time-invariant per layer, so a single
    // representative frame suffices for asset discovery.
    {
        std::unordered_set<std::string> pending;
        for (const auto& layer : scene_0.layers()) {
            if (!layer.video_source && !layer_is_static(layer) &&
                layer_resources.find(std::string(layer.name)) == layer_resources.end()) {
                pending.insert(std::string(layer.name));
            }
        }
        if (!pending.empty()) {
            for (const auto& layer : scene_0.layers()) {
                if (pending.erase(std::string(layer.name)) == 0) continue;
                if (!layer_is_static(layer)) {
                    const Frame start = std::max<Frame>(Frame{0}, layer.from);
                    const auto ctx_f = make_frame_context({
                        .global_time = SampleTime::from_frame(
                            static_cast<double>(std::min<Frame>(start, duration - Frame{1}).integral()), rate),
                        .duration = duration,
                        .width = width,
                        .height = height,
                    });
                    Scene scene_rep;
                    try {
                        scene_rep = compiled.composition->evaluate(ctx_f);
                    } catch (...) {
                        continue;
                    }
                    bool fatal = false;
                    for (const auto& rep_layer : scene_rep.layers()) {
                        if (rep_layer.name == layer.name) {
                            if (!collect_layer(rep_layer)) fatal = true;
                            break;
                        }
                    }
                    if (fatal) return nullptr;
                } else if (!collect_layer(layer)) {
                    return nullptr;
                }
            }
        }
    }
    const double manifest_prepare_ms = profiling::duration_ms(prepare_t0, profiling::now());
    spdlog::debug(
        "[direct-yuv] resource manifest: layers={} static+animated prepare={:.2f}ms "
        "(was per-frame pre-pass)", layer_resources.size(), manifest_prepare_ms);

    auto program = std::shared_ptr<DirectYuvProgram>(new DirectYuvProgram());
    program->video_path_ = std::move(video_path);
    program->video_layers_ = std::move(video_layers);
    program->width_ = width;
    program->height_ = height;
    program->composition_ = compiled.composition;
    program->layer_resources_ = std::move(layer_resources);
    program->persistent_resources_ = std::move(persistent_resources);
    program->scene_eval_ms_ = scene_eval_ms;
    program->watermark_load_ms_ = watermark_load_ms;
    program->watermark_upload_ms_ = watermark_upload_ms;
    return program;
#endif
}

std::shared_ptr<DirectYuvFrame> DirectYuvProgram::execute(
    media::NativeVideoFrameDecoder& decoder, Frame frame) const {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)decoder; (void)frame;
    return nullptr;
#else
    auto decoded = decoder.decode_native_frame_at(
        video_path_, composition_->frame_rate().presentation_time(frame), width_, height_);
    if (decoded) {
        last_decoded_ = decoded;
    } else if (last_decoded_) {
        decoded = last_decoded_;
    } else {
        spdlog::error("[direct-yuv] native decode failed at frame={} without diagnostic", frame.integral());
        return nullptr;
    }
    if (!composition_) return nullptr;

    auto result = std::make_shared<DirectYuvFrame>();
    result->decoded = std::move(decoded);
    auto template_frame = std::make_shared<DirectYuvTemplate>();
    const auto rate = composition_->frame_rate();
    const auto context = make_frame_context({
        .global_time = SampleTime::from_frame(static_cast<double>(frame.integral()), rate),
        .duration = composition_->duration(),
        .width = width_,
        .height = height_,
    });

    Scene scene;
    try {
        scene = composition_->evaluate(context);
    } catch (...) {
        result->program = template_frame;
        return result;
    }

    std::size_t video_layer_index = 0;
    for (const auto& layer : scene.layers()) {
        if (!layer.visible) continue;
        if (frame.integral() < layer.from.integral() ||
            (layer.duration.integral() > 0 && frame.integral() >= layer.from.integral() + layer.duration.integral())) {
            continue;
        }
        if (layer.transform.opacity <= 0.001f) continue;

        if (layer.video_source) {
            // The first video is the DirectYUV background supplied to the
            // compositor. Every subsequent video becomes a native NV12 layer.
            if (video_layer_index++ == 0) continue;
            const auto overlay_index = video_layer_index - 2;
            if (overlay_index >= video_layers_.size()) return nullptr;
            const auto& overlay = video_layers_[overlay_index];
            const Frame local_frame = frame - layer.from;
            if (local_frame < 0) continue;
            const Frame source_frame = video::map_video_frame(local_frame, overlay.source);
            const i64 source_fps = std::max<i64>(
                1, static_cast<i64>(std::llround(overlay.source.source_fps)));
            auto overlay_frame = decoder.decode_native_frame_at(
                overlay.source.path,
                RationalTime{source_frame.integral(), Rational{1, source_fps}},
                width_, height_);
            if (!overlay_frame || overlay_frame->format != AV_PIX_FMT_CUDA ||
                !overlay_frame->data[0] || !overlay_frame->data[1]) {
                spdlog::error("[direct-yuv] native overlay decode failed: layer='{}' frame={}",
                              overlay.name, frame.integral());
                return nullptr;
            }
            auto* overlay_hw_frames = overlay_frame->hw_frames_ctx
                ? reinterpret_cast<AVHWFramesContext*>(overlay_frame->hw_frames_ctx->data)
                : nullptr;
            if (!overlay_hw_frames || overlay_hw_frames->sw_format != AV_PIX_FMT_NV12) {
                spdlog::error("[direct-yuv] native overlay is not NV12: layer='{}'", overlay.name);
                return nullptr;
            }

            const float base_w = overlay.source.size.x > 0.0f
                ? overlay.source.size.x : static_cast<float>(overlay_frame->width);
            const float base_h = overlay.source.size.y > 0.0f
                ? overlay.source.size.y : static_cast<float>(overlay_frame->height);
            const float cx = static_cast<float>(width_) * 0.5f + layer.transform.position.x;
            const float cy = static_cast<float>(height_) * 0.5f + layer.transform.position.y;
            const float half_w = base_w * 0.5f * layer.transform.scale.x;
            const float half_h = base_h * 0.5f * layer.transform.scale.y;
            const uint32_t res_idx = static_cast<uint32_t>(template_frame->resources.size());
            template_frame->batch.instances.push_back(runtime::LayerInstance{
                .kind = runtime::PrimitiveKind::Video,
                .resource_index = res_idx,
                .src_x0 = 0.0f, .src_y0 = 0.0f, .src_x1 = 1.0f, .src_y1 = 1.0f,
                .dst_x0 = cx - half_w, .dst_y0 = cy - half_h,
                .dst_x1 = cx + half_w, .dst_y1 = cy + half_h,
                .opacity = std::clamp(layer.transform.opacity, 0.0f, 1.0f),
                .blend = layer.blend_mode == BlendMode::Add ? BlendMode::Add : BlendMode::Normal});
            media::CudaLayerResource resource;
            resource.kind = media::CudaLayerResourceKind::Nv12;
            resource.y = reinterpret_cast<CUdeviceptr>(overlay_frame->data[0]);
            resource.uv = reinterpret_cast<CUdeviceptr>(overlay_frame->data[1]);
            resource.pitch_bytes = overlay_frame->linesize[0];
            resource.uv_pitch_bytes = overlay_frame->linesize[1];
            resource.width = static_cast<uint32_t>(overlay_frame->width);
            resource.height = static_cast<uint32_t>(overlay_frame->height);
            template_frame->resources.push_back(resource);
            result->video_layers.push_back(std::move(overlay_frame));
            continue;
        }
        auto it = layer_resources_.find(std::string(layer.name));
        if (it == layer_resources_.end() || !it->second.gpu_resource) continue;
        const auto& entry = it->second;

        float base_w = entry.native_width;
        float base_h = entry.native_height;
        float scale_x = layer.transform.scale.x;
        float scale_y = layer.transform.scale.y;
        float cx = static_cast<float>(width_) * 0.5f + layer.transform.position.x;
        float cy = static_cast<float>(height_) * 0.5f + layer.transform.position.y;
        float opacity = std::clamp(layer.transform.opacity, 0.0f, 1.0f);

        if (!layer.nodes.empty()) {
            const auto& node = layer.nodes[0];
            scale_x *= node.world_transform.scale.x;
            scale_y *= node.world_transform.scale.y;
            if (node.shape.type() == ShapeType::Image) {
                cx += node.world_transform.position.x;
                cy += node.world_transform.position.y;
                const auto& img = node.shape.image();
                if (img.size.x > 0.0f && img.size.y > 0.0f) {
                    base_w = img.size.x;
                    base_h = img.size.y;
                }
                opacity = std::clamp(opacity * img.opacity, 0.0f, 1.0f);
            }
        }

        const float half_w = (base_w * 0.5f) * scale_x;
        const float half_h = (base_h * 0.5f) * scale_y;
        const float dst_x0 = cx - half_w;
        const float dst_y0 = cy - half_h;
        const float dst_x1 = cx + half_w;
        const float dst_y1 = cy + half_h;
        if (opacity <= 0.001f || dst_x1 <= dst_x0 || dst_y1 <= dst_y0) continue;

        const uint32_t res_idx = static_cast<uint32_t>(template_frame->resources.size());
        template_frame->batch.instances.push_back(runtime::LayerInstance{
            .kind = runtime::PrimitiveKind::Image,
            .resource_index = res_idx,
            .src_x0 = 0.0f, .src_y0 = 0.0f, .src_x1 = 1.0f, .src_y1 = 1.0f,
            .dst_x0 = dst_x0, .dst_y0 = dst_y0,
            .dst_x1 = dst_x1, .dst_y1 = dst_y1,
            .opacity = opacity,
            .blend = layer.blend_mode == BlendMode::Add ? BlendMode::Add : BlendMode::Normal,
            .corner_radius = entry.corner_radius});

        media::CudaLayerResource res;
        res.rgba = entry.gpu_resource->ptr;
        res.pitch_bytes = static_cast<int>(entry.gpu_resource->pitch_bytes);
        res.width = entry.gpu_resource->width;
        res.height = entry.gpu_resource->height;
        template_frame->resources.push_back(res);
        if (!template_frame->resource_owner) {
            template_frame->resource_owner = entry.gpu_resource;
        }
        template_frame->resource_owners.push_back(entry.gpu_resource);
    }

    result->program = std::move(template_frame);
    return result;
#endif
}

} // namespace chronon3d::cli
