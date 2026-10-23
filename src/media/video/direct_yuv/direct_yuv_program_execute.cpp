#include <chronon3d/media/video/direct_yuv_program.hpp>
#include "direct_yuv_text_contract.hpp"
#include "text_texture_cache.hpp"

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/text/text_color_packing.hpp>

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
#include <unordered_set>
#include <utility>

namespace chronon3d::media::video {
namespace media = ::chronon3d::media;
namespace source_video = ::chronon3d::video;
const DirectLayerResourceEntry* DirectYuvProgram::find_resource(
    std::string_view name) const noexcept {
    const auto it = std::lower_bound(
        resource_index_.begin(), resource_index_.end(), name,
        [](const auto& entry, std::string_view value) {
            return entry.first < value;
        });
    return it != resource_index_.end() && it->first == name ? it->second : nullptr;
}

std::shared_ptr<DirectYuvTemplate> DirectYuvProgram::acquire_dynamic_template() {
    constexpr std::size_t kMaxPooledTemplates = 16;
    for (auto& candidate : dynamic_template_pool_) {
        if (candidate.use_count() == 1) {
            candidate->reset_for_reuse();
            return candidate;
        }
    }
    auto candidate = std::make_shared<DirectYuvTemplate>();
    if (dynamic_template_pool_.size() < kMaxPooledTemplates) {
        dynamic_template_pool_.push_back(candidate);
    }
    return candidate;
}

std::shared_ptr<DirectYuvFrame> DirectYuvProgram::acquire_frame() {
    constexpr std::size_t kMaxPooledFrames = 16;
    for (auto& candidate : frame_pool_) {
        if (candidate.use_count() == 1) {
            candidate->decoded = {};
            candidate->video_layers.clear();
            candidate->program.reset();
            candidate->decode_ms = 0.0;
            candidate->template_reused = false;
            return candidate;
        }
    }
    auto candidate = std::make_shared<DirectYuvFrame>();
    if (frame_pool_.size() < kMaxPooledFrames) {
        frame_pool_.push_back(candidate);
    }
    return candidate;
}

std::shared_ptr<DirectYuvFrame> DirectYuvProgram::execute(
    media::NativeVideoFrameDecoder& decoder, Frame frame) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)decoder; (void)frame;
    return nullptr;
#else
    const auto decode_t0 = profiling::now();
    auto decoded = decoder.decode_native_frame_at(
        video_path_, composition_->frame_rate().presentation_time(frame), width_, height_);
    const double decode_ms = profiling::duration_ms(decode_t0, profiling::now());
    if (!decoded) {
        // A decode error is a failed frame, not an implicit hold-last-frame
        // policy. Repeating a previous image produces a formally successful
        // but visually corrupt export and hides the original failure.
        spdlog::error("[direct-yuv] native decode failed at frame={}", frame.integral());
        return nullptr;
    }
    if (!composition_) return nullptr;

    auto result = acquire_frame();
    result->decoded = std::move(decoded);
    result->decode_ms = decode_ms;
    result->template_reused = false;

    if (all_overlay_layers_static_ && video_layers_.empty()) {
        if (cached_template_.template_frame &&
            frame >= cached_template_.valid_from &&
            frame < cached_template_.valid_until) {
            ++template_reuse_count_;
            result->program = cached_template_.template_frame;
            result->template_reused = true;
            return result;
        }
    }

    ++template_eval_count_;
    auto template_frame = acquire_dynamic_template();
    if (template_frame->batch.instances.capacity() < overlay_instances_hint_) {
        template_frame->batch.instances.reserve(overlay_instances_hint_);
    }
    if (template_frame->resources.capacity() < overlay_instances_hint_) {
        template_frame->resources.reserve(overlay_instances_hint_);
    }
    if (template_frame->resource_owners.capacity() < overlay_instances_hint_) {
        template_frame->resource_owners.reserve(overlay_instances_hint_);
    }
    const auto rate = composition_->frame_rate();
    const auto duration = composition_->duration();
    const auto context = make_frame_context({
        .global_time = SampleTime::from_frame(static_cast<double>(frame.integral()), rate),
        .duration = duration,
        .width = width_,
        .height = height_,
    });

    const auto scene_t0 = profiling::now();
    Scene scene;
    try {
        scene = composition_->evaluate(context);
    } catch (const std::exception& error) {
        // FAIL-CLOSED: a failed timeline evaluation must fail the frame — and
        // therefore the render job — never silently fall back to an empty
        // overlay program that would produce a formally-SUCCESSFUL video with
        // every requested overlay missing.
        spdlog::error("[direct-yuv] scene evaluation failed at frame={}: {}",
                      frame.integral(), error.what());
        (void)error;
        return nullptr;
    } catch (...) {
        spdlog::error("[direct-yuv] scene evaluation failed at frame={} with an unknown exception",
                      frame.integral());
        return nullptr;
    }
    const double scene_eval_ms = profiling::duration_ms(scene_t0, profiling::now());
    if (profiling::g_current_counters) {
        profiling::g_current_counters->scene_eval_wall_ms_total.fetch_add(
            static_cast<uint64_t>(scene_eval_ms), std::memory_order_relaxed);
    }

    // AUDIT-13/14: no unsupported_blend flag — every unsupported blend fails
    // the frame at its emit site, before any instance is pushed.

    std::size_t video_layer_index = 0;
    for (const auto& layer : scene.layers()) {
        if (!layer.visible) continue;
        if (frame.integral() < layer.from.integral() ||
            (layer.duration.integral() > 0 && frame.integral() >= layer.from.integral() + layer.duration.integral())) {
            continue;
        }
        if (layer.transform.opacity <= 0.001f) continue;

        if (layer.nodes.size() > 1) {
            // Fail-closed: the fast path composites one primitive per layer.
            // A multi-node layer would render only node[0] and silently drop
            // the rest — reject the frame instead of approximating it.
            spdlog::error("[direct-yuv] multi-node layer not supported by Direct-YUV at frame={}: layer='{}' nodes={}",
                          frame.integral(), layer.name, layer.nodes.size());
            return nullptr;
        }

        if (layer.video_source) {
            if (video_layer_index++ == 0) continue;
            const auto overlay_index = video_layer_index - 2;
            if (overlay_index >= video_layers_.size()) return nullptr;
            const auto& overlay = video_layers_[overlay_index];
            const Frame local_frame = frame - layer.from;
            if (local_frame < 0) continue;
            const Frame source_frame = source_video::map_video_frame(local_frame, overlay.source);
            const float effective_fps = (overlay.source.source_fps > 0.0f)
                ? overlay.source.source_fps
                : static_cast<float>(rate.numerator) / (rate.denominator > 0 ? rate.denominator : 1);
            const i64 source_fps = std::max<i64>(
                1, static_cast<i64>(std::llround(effective_fps)));
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
            // AUDIT-13/14: validate BEFORE emitting. The frame is failed
            // here, never via a fake Normal instance pushed and rejected one
            // iteration later.
            if (layer.blend_mode != BlendMode::Add && layer.blend_mode != BlendMode::Normal) {
                spdlog::error("[direct-yuv] unsupported blend mode for video layer '{}': {}",
                              layer.name, static_cast<int>(layer.blend_mode));
                return nullptr;
            }
            const uint32_t res_idx = static_cast<uint32_t>(template_frame->resources.size());
            template_frame->batch.instances.push_back(runtime::LayerInstance{
                .kind = runtime::PrimitiveKind::Video,
                .resource_index = res_idx,
                .src_x0 = 0.0f, .src_y0 = 0.0f, .src_x1 = 1.0f, .src_y1 = 1.0f,
                .dst_x0 = cx - half_w, .dst_y0 = cy - half_h,
                .dst_x1 = cx + half_w, .dst_y1 = cy + half_h,
            .opacity = std::clamp(layer.transform.opacity, 0.0f, 1.0f),
            .blend = layer.blend_mode});
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
        auto* resource_entry = find_resource(layer.name);
        if (!resource_entry || !resource_entry->gpu_resource) {
            // FAIL-CLOSED: the plan requested this layer, so a missing GPU
            // resource is a render failure — never "render the base video and
            // pretend the overlay never existed".
            spdlog::error("[direct-yuv] required overlay resource is missing: layer='{}' frame={}",
                          layer.name, frame.integral());
            return nullptr;
        }
        const auto& entry = *resource_entry;

        float base_w = entry.native_width;
        float base_h = entry.native_height;
        float scale_x = layer.transform.scale.x;
        float scale_y = layer.transform.scale.y;
        float cx = static_cast<float>(width_) * 0.5f + layer.transform.position.x;
        float cy = static_cast<float>(height_) * 0.5f + layer.transform.position.y;
        float opacity = std::clamp(layer.transform.opacity, 0.0f, 1.0f);

        if (!layer.nodes.empty()) {
            // Direct-YUV equivalence requires exactly one shape node per
            // overlay layer. A multi-node layer has semantics (per-node
            // transforms, ordering) the fast path cannot reproduce, so it is
            // rejected instead of silently rendering only nodes[0].
            if (layer.nodes.size() > 1) {
                spdlog::error("[direct-yuv] layer '{}' has {} shape nodes; the fast path supports exactly one — refusing to render a visually-different result",
                              layer.name, layer.nodes.size());
                return nullptr;
            }
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

        // AUDIT-13/14: validate BEFORE emitting — same contract as video
        // layers. Only Add and Normal are implemented; anything else fails
        // the frame here rather than being coerced to Normal.
        if (layer.blend_mode != BlendMode::Add && layer.blend_mode != BlendMode::Normal) {
            spdlog::error("[direct-yuv] unsupported blend mode for layer '{}': {}",
                          layer.name, static_cast<int>(layer.blend_mode));
            return nullptr;
        }
        const uint32_t res_idx = static_cast<uint32_t>(template_frame->resources.size());
        template_frame->batch.instances.push_back(runtime::LayerInstance{
            .kind = runtime::PrimitiveKind::Image,
            .resource_index = res_idx,
            .src_x0 = 0.0f, .src_y0 = 0.0f, .src_x1 = 1.0f, .src_y1 = 1.0f,
            .dst_x0 = dst_x0, .dst_y0 = dst_y0,
            .dst_x1 = dst_x1, .dst_y1 = dst_y1,
            .opacity = opacity,
            .blend = layer.blend_mode,
            .corner_radius = entry.corner_radius});

        media::CudaLayerResource res;
        res.rgba = entry.gpu_resource->ptr;
        res.pitch_bytes = static_cast<int>(entry.gpu_resource->pitch_bytes);
        res.width = entry.gpu_resource->width;
        res.height = entry.gpu_resource->height;
        res.kind = entry.kind;
        res.distance_range_px = entry.distance_range_px;
        res.color = entry.color;
        res.stroke_color = entry.stroke_color;
        res.stroke_width = entry.stroke_width;
        res.shadow_color = entry.shadow_color;
        res.shadow_opacity = entry.shadow_opacity;
        res.shadow_dx = entry.shadow_dx;
        res.shadow_dy = entry.shadow_dy;
        res.shadow_blur = entry.shadow_blur;
        template_frame->resources.push_back(res);
        if (!template_frame->resource_owner) {
            template_frame->resource_owner = entry.gpu_resource;
        }
        template_frame->resource_owners.push_back(entry.gpu_resource);
    }

    if (all_overlay_layers_static_ && video_layers_.empty()) {
        Frame cue_from = Frame{0};
        Frame cue_until = composition_->duration();
        for (const auto& layer : scene.layers()) {
            if (layer.video_source) continue;
            const Frame l_from = std::max<Frame>(Frame{0}, layer.from);
            if (l_from <= frame) {
                cue_from = std::max(cue_from, l_from);
            } else {
                cue_until = std::min(cue_until, l_from);
            }
            if (layer.duration.integral() > 0) {
                const Frame l_to = l_from + layer.duration;
                if (l_to <= frame) {
                    cue_from = std::max(cue_from, l_to);
                } else {
                    cue_until = std::min(cue_until, l_to);
                }
            }
        }
        if (cue_until <= frame) {
            cue_until = frame + Frame{1};
        }
        cached_template_.valid_from = cue_from;
        cached_template_.valid_until = cue_until;
        cached_template_.template_frame = template_frame;
    }

    // AUDIT-02C: keep the next dynamic frame's reserve hint at the observed
    // high-water mark (monotonic; no shrinking).
    overlay_instances_hint_ = std::max<std::size_t>(
        overlay_instances_hint_, template_frame->batch.instances.size());

    result->program = std::move(template_frame);
    return result;
#endif
}

} // namespace chronon3d::media::video
