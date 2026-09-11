#include <chronon3d/media/video/direct_yuv_program.hpp>
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

namespace chronon3d::media::video {
namespace media = ::chronon3d::media;
namespace source_video = ::chronon3d::video;
namespace {

std::uint32_t pack_text_color(const Color& color) noexcept {
    const auto to_byte = [](float value) {
        return static_cast<std::uint32_t>(std::clamp(
            std::lround(value * 255.0f), 0L, 255L));
    };
    return to_byte(color.r) | (to_byte(color.g) << 8) |
           (to_byte(color.b) << 16) | (to_byte(color.a) << 24);
}

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
    // Z4 is enforced twice by design: the session resolver chooses the lane
    // before encoder open(), while the prepared media program refuses a
    // direct execution contract that was assembled by any other caller.
    const auto capability = resolve_support(compiled, DirectYuvPolicy::Require);
    if (!capability.eligible) {
        reason = "DirectYUV capability matrix rejected the composition: " +
            capability.reason;
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

    const auto prepare_t0 = profiling::now();
    const auto collect_layer = [&](const Layer& layer) -> bool {
        if (layer.video_source) return true;
        const std::string layer_name = std::string(layer.name);
        if (layer_resources.find(layer_name) != layer_resources.end()) return true;
        if (layer.nodes.empty()) return true;
        if (layer.nodes.size() > 1) {
            reason = "DirectYUV preparation rejects multi-node overlay layer: " +
                layer_name;
            return false;
        }

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
            std::string text_content;
            std::string font_path;
            const TextRunShape* prepared_shape = nullptr;
            // No fast-path typography defaults: every value must come from the
            // prepared TextRun shape produced by the canonical text/layout
            // subsystem. A layer without one is a preparation failure, not a
            // license to invent 58px white text.
            float font_size = 0.0f;

            if (node.shape.type() == ShapeType::TextRun) {
                const auto handle = node.shape.text_run_shape_handle();
                if (handle.value) {
                    const auto& s = *handle.value;
                    prepared_shape = &s;
                    if (s.layout) {
                        text_content = s.layout->source_text;
                        if (!s.layout->font.font_path.empty()) font_path = s.layout->font.font_path;
                        if (font_path.empty() && !s.layout->font_spans.empty()) {
                            font_path = s.layout->font_spans.front().font.font_path;
                        }
                        if (s.layout->font_size > 0.0f) font_size = s.layout->font_size;
                    }
                }
            }

            if (font_path.empty()) {
                reason = "text layer has no prepared font asset: " + layer_name;
                return false;
            }
            if (font_size <= 0.0f) {
                reason = "text layer has no prepared font size (TextRun shape must carry the canonical layout): " + layer_name;
                return false;
            }
            if (text_content.empty()) {
                // A text layer with no prepared source text cannot be
                // rasterized faithfully from the layer name.
                reason = "text layer has no prepared source text: " + layer_name;
                return false;
            }

            const auto text_t0 = profiling::now();
            if (!prepared_shape) {
                reason = "text layer has no prepared TextRunShape: " + layer_name;
                return false;
            }
            auto rasterized = rasterize_text_texture(*prepared_shape);
            watermark_load_ms += profiling::duration_ms(text_t0, profiling::now());

            bool cache_hit = false;
            double upload_ms = 0.0;
            auto resource = video_runtime->get_or_upload_image(
                rasterized.digest, ImageDecodeOptions{},
                static_cast<std::uint32_t>(rasterized.width),
                static_cast<std::uint32_t>(rasterized.height), rasterized.gpu_rgba,
                cache_hit, upload_ms, reason, runtime::PixelFormat::Rgba8Unorm);
            if (!resource) return false;
            watermark_upload_ms += upload_ms;

            DirectLayerResourceEntry entry;
            entry.gpu_resource = resource;
            entry.native_width = rasterized.display_width;
            entry.native_height = rasterized.display_height;
            entry.kind = media::CudaLayerResourceKind::Mtsdf;
            entry.distance_range_px = rasterized.distance_range_px;
            const Color fill_color = node.color.a > 0.0f
                ? node.color : prepared_shape->paint.fill;
            entry.color = pack_text_color(fill_color);
            entry.stroke_color = pack_text_color(prepared_shape->paint.stroke_color);
            entry.stroke_width = prepared_shape->paint.stroke_enabled
                ? prepared_shape->paint.stroke_width : 0.0f;
            layer_resources[layer_name] = entry;
            persistent_resources.push_back(resource);
            return true;
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

    for (const auto& layer : scene_0.layers()) {
        if (!layer_is_static(layer)) continue;
        if (!collect_layer(layer)) return nullptr;
    }
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
                    } catch (const std::exception& error) {
                        // FAIL-CLOSED: evaluation failure during asset
                        // discovery is a fatal program-preparation error.
                        reason = std::string("scene evaluation failed for layer '") +
                                 std::string(layer.name) + "': " + error.what();
                        return nullptr;
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

    bool all_overlay_layers_static = true;
    for (const auto& layer : scene_0.layers()) {
        if (layer.video_source) continue;
        if (!layer_is_static(layer)) {
            all_overlay_layers_static = false;
            break;
        }
    }

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
    program->all_overlay_layers_static_ = all_overlay_layers_static;
    // AUDIT-04B: build the allocation-free lookup index once. The map keys
    // are node-stable (unordered_map never moves node storage), so the
    // string_views remain valid for the program's lifetime.
    program->resource_index_.reserve(program->layer_resources_.size());
    for (const auto& [name, entry] : program->layer_resources_) {
        program->resource_index_.emplace_back(name, &entry);
    }
    std::sort(program->resource_index_.begin(), program->resource_index_.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.first < rhs.first;
              });
    return program;
#endif
}

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
    auto decoded = decoder.decode_native_frame_at(
        video_path_, composition_->frame_rate().presentation_time(frame), width_, height_);
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

    if (all_overlay_layers_static_ && video_layers_.empty()) {
        if (cached_template_.template_frame &&
            frame >= cached_template_.valid_from &&
            frame < cached_template_.valid_until) {
            ++template_reuse_count_;
            result->program = cached_template_.template_frame;
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
