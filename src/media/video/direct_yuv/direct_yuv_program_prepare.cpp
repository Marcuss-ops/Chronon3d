#include <chronon3d/media/video/direct_yuv_program.hpp>
#include "direct_yuv_program_scene.hpp"
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
#include <string_view>

namespace chronon3d::media::video {
namespace media = ::chronon3d::media;
namespace source_video = ::chronon3d::video;
namespace {

// DirectYUV programs are short-lived job objects, while the daemon process
// is persistent. Keep the immutable MTSDF preparation at daemon scope so
// repeated exports of the same prepared text do not reopen fonts and rebuild
// the atlas. Device-specific CUDA residency is still handled by the
// VideoDeviceRuntime image cache below.
DirectYuvTextTextureCache& text_texture_cache() {
    static DirectYuvTextTextureCache cache;
    return cache;
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
    const assets::AssetResolver& asset_resolver,
    runtime::LaneReason& reason) {
    if (!compiled.composition) {
        reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                     "error", "compiled_composition_empty"};
        return nullptr;
    }
    const auto composition = compiled.composition;
    const auto evaluate = [composition](Frame frame) {
        return composition->evaluate(make_frame_context({
            .global_time = SampleTime::from_frame(
                static_cast<double>(frame.integral()), composition->frame_rate()),
            .duration = composition->duration(),
            .width = composition->width(),
            .height = composition->height(),
        }));
    };
    return prepare_impl(&compiled, nullptr, {}, composition->width(),
                        composition->height(), composition->duration(),
                        composition->frame_rate(), evaluate, image_cache,
                        std::move(video_runtime), asset_resolver, reason);
}

std::shared_ptr<DirectYuvProgram> DirectYuvProgram::prepare(
    const compiler::CompiledProgram& program,
    ImageCache& image_cache,
    std::shared_ptr<media::VideoDeviceRuntime> video_runtime,
    const assets::AssetResolver& asset_resolver,
    runtime::LaneReason& reason) {
    auto topology = detail::materialize_program_topology(program);
    const auto evaluate = [&program, topology](Frame frame) {
        return detail::sample_program_scene(program, *topology, frame);
    };
    const auto& metadata = program.metadata();
    return prepare_impl(nullptr, &program, std::move(topology),
                        static_cast<int>(metadata.width),
                        static_cast<int>(metadata.height),
                        Frame{metadata.duration_frames},
                        FrameRate{metadata.fps_numerator, metadata.fps_denominator},
                        evaluate, image_cache, std::move(video_runtime),
                        asset_resolver, reason);
}

std::shared_ptr<DirectYuvProgram> DirectYuvProgram::prepare_impl(
    const CompiledComposition* compiled,
    const compiler::CompiledProgram* typed_program,
    std::shared_ptr<const Scene> typed_topology,
    int width,
    int height,
    Frame duration,
    FrameRate rate,
    std::function<Scene(Frame)> scene_evaluator,
    ImageCache& image_cache,
    std::shared_ptr<media::VideoDeviceRuntime> video_runtime,
    const assets::AssetResolver& asset_resolver,
    runtime::LaneReason& reason) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)compiled; (void)typed_program; (void)typed_topology;
    (void)width; (void)height; (void)duration; (void)rate;
    (void)scene_evaluator; (void)image_cache; (void)video_runtime;
    (void)asset_resolver;
    reason = runtime::LaneReason{runtime::LaneDecision::FallbackUnsupported,
                                 "unsupported_feature", "cuda_interop_not_compiled"};
    return nullptr;
}
#else
    // Lane selection is final before this preparation boundary. The resolver
    // and compiled-composition classifier already produced the immutable
    // DirectYUV decision; preparation may fail for missing assets, upload
    // errors, or broken invariants, but it must not classify the composition
    // again and silently switch lanes.
    if (!video_runtime) {
        reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                     "error", "video_device_runtime_unavailable"};
        return nullptr;
    }

    const auto scene_t0 = profiling::now();
    Scene scene_0;
    try {
        scene_0 = scene_evaluator(Frame{0});
    } catch (const std::exception& error) {
        reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                     "error", "scene_evaluation_failed"};
        return nullptr;
    }
    const double scene_eval_ms = profiling::duration_ms(scene_t0, profiling::now());
    if (profiling::g_current_counters) {
        profiling::g_current_counters->scene_eval_wall_ms_total.fetch_add(
            static_cast<uint64_t>(scene_eval_ms), std::memory_order_relaxed);
    }

    std::string video_path;
    std::vector<DirectVideoLayer> video_layers;
    for (const auto& layer : scene_0.layers()) {
        if (layer.uses_2_5d_projection) {
            reason = runtime::LaneReason{runtime::LaneDecision::FallbackUnsupported,
                                         "unsupported_feature", "layer_2_5d"};
            return nullptr;
        }
        if (layer.mask.enabled()) {
            reason = runtime::LaneReason{runtime::LaneDecision::FallbackUnsupported,
                                         "unsupported_feature", "mask"};
            return nullptr;
        }
        if (!is_2d_transform(layer.transform)) {
            reason = runtime::LaneReason{runtime::LaneDecision::FallbackUnsupported,
                                         "unsupported_feature", "transform_3d"};
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
        reason = runtime::LaneReason{runtime::LaneDecision::FallbackUnsupported,
                                     "unsupported_feature", "no_video_source"};
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
            reason = runtime::LaneReason{runtime::LaneDecision::FallbackUnsupported,
                                         "unsupported_feature", "multi_node_overlay_layer"};
            return false;
        }

        const auto& node = layer.nodes[0];
        if (node.shape.type() == ShapeType::Image) {
            const auto& image = node.shape.image();
            if (image.path.empty()) return true;
            const auto img_t0 = profiling::now();
            // P0-1 fix: resolver-explicit path. The VideoDeviceRuntime ImageCache
            // is process-persistent and shared by all jobs on that device; it
            // must never retain a job-scoped AssetResolver* (dangling + race on
            // concurrent jobs).  Pass the job resolver per-call instead.
            auto cached = image_cache.get_or_load(image.path, image.decode_options, asset_resolver);
            watermark_load_ms += profiling::duration_ms(img_t0, profiling::now());
            if (!cached || !cached->valid() || cached->gpu_rgba.empty()) {
                reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                             "error", "image_cache_missing"};
                return false;
            }
            bool cache_hit = false;
            double upload_ms = 0.0;
            std::string upload_error;
            auto resource = video_runtime->get_or_upload_image(
                cached->gpu_key.content_digest, image.decode_options,
                static_cast<std::uint32_t>(cached->width),
                static_cast<std::uint32_t>(cached->height), cached->gpu_rgba,
                cache_hit, upload_ms, upload_error);
            if (!resource) {
                reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                             "error", "upload_failed"};
                return false;
            }
            watermark_upload_ms += upload_ms;

            DirectLayerResourceEntry entry;
            entry.gpu_resource = resource;
            entry.kind = media::CudaLayerResourceKind::RgbaLinearPremultiplied;
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
                reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                             "error", "text_font_missing"};
                return false;
            }
            if (font_size <= 0.0f) {
                reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                             "error", "text_font_size_missing"};
                return false;
            }
            if (text_content.empty()) {
                // A text layer with no prepared source text cannot be
                // rasterized faithfully from the layer name.
                reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                             "error", "text_source_missing"};
                return false;
            }

            const auto text_t0 = profiling::now();
            if (!prepared_shape) {
                reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                             "error", "text_shape_missing"};
                return false;
            }
            // The capability matrix already accepted this scene through the
            // same classifier, so a rejection here means the prepared run
            // changed after resolution. Fail closed rather than dropping an
            // authored shadow silently.
            const auto text_contract =
                resolve_direct_yuv_text_contract(*prepared_shape);
            if (text_contract.rejection) {
                reason = runtime::LaneReason{runtime::LaneDecision::FallbackUnsupported,
                                             "unsupported_feature", "text_contract_rejected"};
                return false;
            }
            auto rasterized = text_texture_cache().get_or_create(
                *prepared_shape, asset_resolver);
            watermark_load_ms += profiling::duration_ms(text_t0, profiling::now());

            bool cache_hit = false;
            double upload_ms = 0.0;
            std::string upload_error;
            auto resource = video_runtime->get_or_upload_image(
                rasterized->digest, ImageDecodeOptions{},
                static_cast<std::uint32_t>(rasterized->width),
                static_cast<std::uint32_t>(rasterized->height), rasterized->gpu_rgba,
                cache_hit, upload_ms, upload_error, runtime::PixelFormat::Rgba8Unorm);
            if (!resource) {
                reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                             "error", "upload_failed"};
                return false;
            }
            watermark_upload_ms += upload_ms;

            DirectLayerResourceEntry entry;
            entry.gpu_resource = resource;
            entry.native_width = rasterized->display_width;
            entry.native_height = rasterized->display_height;
            entry.kind = media::CudaLayerResourceKind::Mtsdf;
            entry.distance_range_px = rasterized->distance_range_px;
            const Color fill_color = node.color.a > 0.0f
                ? node.color : prepared_shape->paint.fill;
            entry.color = ::chronon3d::text::pack_text_color(fill_color);
            entry.stroke_color = ::chronon3d::text::pack_text_color(prepared_shape->paint.stroke_color);
            entry.stroke_width = prepared_shape->paint.stroke_enabled
                ? prepared_shape->paint.stroke_width : 0.0f;
            // Canonical single drop shadow. The kernel resamples the prepared
            // distance field for it, so only the parameters travel here; no
            // extra texture, upload or per-frame work is created.
            if (text_contract.shadow.enabled) {
                entry.shadow_color = ::chronon3d::text::pack_text_color(text_contract.shadow.color);
                entry.shadow_opacity = text_contract.shadow.opacity;
                entry.shadow_dx = text_contract.shadow.offset_x;
                entry.shadow_dy = text_contract.shadow.offset_y;
                entry.shadow_blur = text_contract.shadow.blur;
            }
            layer_resources[layer_name] = entry;
            persistent_resources.push_back(resource);
            return true;
        }
        return true;
    };

    const auto layer_is_static = [typed_program](const Layer& layer) -> bool {
        if (typed_program) {
            for (const auto& source : typed_program->layers()) {
                const auto expected_name = source.name.empty()
                    ? "layer_" + std::to_string(source.id) : source.name;
                if (std::string_view{layer.name} !=
                    std::string_view{expected_name}) continue;
                return !source.has_transform_animation &&
                    source.transform_samples.empty() &&
                    source.animation_program_index ==
                        compiler::StaticLayerProgram::kInvalidProgramIndex;
            }
            return true;
        }
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
                    Scene scene_rep;
                    try {
                        scene_rep = scene_evaluator(
                            std::min<Frame>(start, duration - Frame{1}));
                    } catch (const std::exception& error) {
                        // FAIL-CLOSED: evaluation failure during asset
                        // discovery is a fatal program-preparation error.
                        reason = runtime::LaneReason{runtime::LaneDecision::Failed,
                                                     "error", "scene_evaluation_failed"};
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
    program->composition_ = compiled ? compiled->composition : nullptr;
    program->typed_program_ = typed_program;
    program->typed_topology_ = std::move(typed_topology);
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
    reason = runtime::LaneReason{runtime::LaneDecision::Selected, "none", "selected"};
    return program;
}

std::shared_ptr<DirectYuvProgram> DirectYuvProgram::prepare(
    const CompiledComposition& compiled,
    ImageCache& image_cache,
    std::shared_ptr<media::VideoDeviceRuntime> video_runtime,
    const assets::AssetResolver& asset_resolver,
    std::string& reason) {
    runtime::LaneReason lane_reason;
    auto res = prepare(compiled, image_cache, video_runtime, asset_resolver, lane_reason);
    reason = lane_reason.to_string();
    return res;
}
#endif


} // namespace chronon3d::media::video
