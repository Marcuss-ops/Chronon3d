#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/core/counters.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "builder/graph_builder_pipeline.hpp"
#include "builder/graph_builder_internal.hpp"
#include "render_pipeline_helpers.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>

namespace chronon3d::graph {


std::shared_ptr<Framebuffer> render_scene_via_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    float fps
) {
    ZoneScoped;
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = node_cache.stats().hits;

    const auto t_build0 = std::chrono::steady_clock::now();
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps
    );
    
    profiling::g_current_trace = ctx.trace;
    profiling::g_current_frame = static_cast<int32_t>(frame);
    profiling::g_current_counters = ctx.counters;
    
    ctx.light_context = scene.light_context();
    if (scene.camera_2_5d().enabled) {
        ctx.camera_2_5d = scene.camera_2_5d();
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(
            ctx.camera_2_5d,
            ctx.width,
            ctx.height
        );
        ctx.projection_ctx.ready = true;
    }
    
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);

    // ── Quick skip: consecutive frame with no layer changes ─────────────
    // Avoid resolve_layers entirely when nothing can have changed.
    if (sw_renderer &&
        settings.enable_dirty_rects &&
        sw_renderer->m_prev_framebuffer &&
        sw_renderer->m_prev_framebuffer->width() == width &&
        sw_renderer->m_prev_framebuffer->height() == height &&
        sw_renderer->m_prev_frame == frame - 1)
    {
        CHRONON_ZONE_C("dirty_fast_check", trace_category::kFrame);
        const Camera2_5D& cam = ctx.camera_2_5d;
        const bool camera_changed = !sw_renderer->m_prev_camera_valid ||
                                     sw_renderer->m_prev_camera.enabled != cam.enabled ||
                                     (cam.enabled && (
                                         sw_renderer->m_prev_camera.position != cam.position ||
                                         sw_renderer->m_prev_camera.zoom != cam.zoom ||
                                         sw_renderer->m_prev_camera.fov_deg != cam.fov_deg ||
                                         sw_renderer->m_prev_camera.rotation != cam.rotation ||
                                         sw_renderer->m_prev_camera.projection_mode != cam.projection_mode
                                     ));

        const uint64_t current_fingerprint = compute_scene_fingerprint(scene, frame);
        if (!camera_changed &&
            sw_renderer->m_prev_scene_fingerprint == current_fingerprint) {
            // Fast path: nothing changed, reuse previous framebuffer
                sw_renderer->m_last_dirty_area_ratio = 0.0;
                sw_renderer->m_prev_frame = frame;
                sw_renderer->m_prev_scene_fingerprint = current_fingerprint;
                sw_renderer->m_prev_camera = cam;
                sw_renderer->m_prev_camera_valid = cam.enabled;
                if (ctx.counters) {
                    ctx.counters->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
                }
                telemetry::record_render_telemetry(make_telemetry_row(
                    "scene_render", frame, width, height,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                    0.0, 0.0, 0.0, 0.0, 1,
                    static_cast<int>(scene.layers().size()), ctx.counters
                ));
                profiling::g_current_counters = nullptr;
                return sw_renderer->m_prev_framebuffer;
            }
        }

    // ── Full path ───────────────────────────────────────────────────────
    const auto resolved = detail::resolve_layers(scene, ctx);

    std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState> current_layer_bboxes;
    std::optional<raster::BBox> dirty_rect;
    bool use_dirty_rects = false;

    if (sw_renderer) {
        CHRONON_ZONE_C("dirty_rect_compute", trace_category::kFrame);
        const Camera2_5DRuntime& cam25d = resolved.camera.camera;

        auto compute_bbox_for_resolved = [&](const ResolvedLayer& rl, const Camera2_5DRuntime& cam) -> raster::BBox {
            LayerGraphItem item;
            if (cam.enabled && rl.layer->is_3d) {
                Transform effective_transform = rl.world_transform;
                if (!ctx.modular_coordinates) {
                    effective_transform.position.x -= ctx.width * 0.5f;
                    effective_transform.position.y -= ctx.height * 0.5f;
                }
                const Mat4 projection_world_matrix = effective_transform.to_mat4();
                auto proj = project_layer_2_5d(
                    effective_transform, projection_world_matrix, cam,
                    static_cast<f32>(ctx.width), static_cast<f32>(ctx.height), ctx.diagnostics_enabled);
                if (proj.visible) {
                    const Mat4 eff_proj = detail::is_native_3d_layer(*rl.layer) ? Mat4(1.0f) : proj.projection_matrix;
                    item = LayerGraphItem{.layer = rl.layer, .transform = proj.transform, .world_matrix = rl.world_matrix,
                        .projection_matrix = eff_proj, .depth = proj.depth, .world_z = rl.world_transform.position.z,
                        .projected = true, .native_3d = detail::is_native_3d_layer(*rl.layer), .insertion_index = rl.insertion_index};
                } else {
                    return raster::BBox{0, 0, 0, 0};
                }
            } else {
                item = LayerGraphItem{.layer = rl.layer, .transform = rl.world_transform, .world_matrix = rl.world_matrix,
                    .depth = 0.0f, .world_z = rl.world_transform.position.z, .projected = false,
                    .native_3d = detail::is_native_3d_layer(*rl.layer), .insertion_index = rl.insertion_index};
            }
            return detail::compute_layer_bbox(item, ctx, sw_renderer);
        };

        for (const auto& rl : resolved.layers) {
            if (rl.layer && rl.layer->active_at(frame)) {
                raster::BBox bbox = compute_bbox_for_resolved(rl, cam25d);
                SoftwareRenderer::LayerBBoxState state;
                state.bbox = bbox; state.world_matrix = rl.world_matrix;
                state.opacity = rl.world_transform.opacity; state.visible = rl.layer->visible;
                state.cache_static = rl.layer->cache_static; state.is_3d = rl.layer->is_3d;
                current_layer_bboxes[std::string(rl.layer->name)] = state;
            }
        }

        use_dirty_rects = settings.enable_dirty_rects &&
                          sw_renderer->m_prev_framebuffer &&
                          sw_renderer->m_prev_framebuffer->width() == width &&
                          sw_renderer->m_prev_framebuffer->height() == height &&
                          sw_renderer->m_prev_frame == frame - 1;

        if (use_dirty_rects) {
            bool camera_changed = !sw_renderer->m_prev_camera_valid ||
                                   sw_renderer->m_prev_camera.enabled != cam25d.enabled ||
                                   (cam25d.enabled && (
                                       sw_renderer->m_prev_camera.position != cam25d.position ||
                                       sw_renderer->m_prev_camera.zoom != cam25d.zoom ||
                                       sw_renderer->m_prev_camera.fov_deg != cam25d.fov_deg ||
                                       sw_renderer->m_prev_camera.rotation != cam25d.rotation ||
                                       sw_renderer->m_prev_camera.projection_mode != cam25d.projection_mode
                                   ));

            raster::BBox union_dirty{0, 0, 0, 0};
            bool has_dirty = false;
            auto add_dirty_bbox = [&](const raster::BBox& b) {
                if (b.is_empty()) return;
                raster::BBox clipped = b;
                clipped.clip_to(width, height);
                if (clipped.is_empty()) return;
                if (!has_dirty) {
                    union_dirty = clipped;
                    has_dirty = true;
                } else {
                    union_dirty.x0 = std::min(union_dirty.x0, clipped.x0);
                    union_dirty.y0 = std::min(union_dirty.y0, clipped.y0);
                    union_dirty.x1 = std::max(union_dirty.x1, clipped.x1);
                    union_dirty.y1 = std::max(union_dirty.y1, clipped.y1);
                }
            };

            for (const auto& pair : current_layer_bboxes) {
                const auto& name = pair.first;
                const auto& curr = pair.second;
                auto prev_it = sw_renderer->m_prev_layer_bboxes.find(name);
                if (prev_it == sw_renderer->m_prev_layer_bboxes.end()) {
                    add_dirty_bbox(curr.bbox);
                } else {
                    const auto& prev = prev_it->second;
                    bool layer_dirty = false;
                    if (camera_changed && curr.is_3d) layer_dirty = true;
                    else if (!curr.cache_static) layer_dirty = true;
                    else if (curr.world_matrix != prev.world_matrix ||
                             curr.opacity != prev.opacity || curr.visible != prev.visible) layer_dirty = true;
                    if (layer_dirty) {
                        add_dirty_bbox(curr.bbox);
                        add_dirty_bbox(prev.bbox);
                    }
                }
            }
            for (const auto& pair : sw_renderer->m_prev_layer_bboxes) {
                if (current_layer_bboxes.find(pair.first) == current_layer_bboxes.end()) {
                    add_dirty_bbox(pair.second.bbox);
                }
            }

            dirty_rect = has_dirty ? std::optional(union_dirty) : std::optional(raster::BBox{0, 0, 0, 0});
        } else {
            dirty_rect = raster::BBox{0, 0, width, height};
        }
    } else {
        dirty_rect = raster::BBox{0, 0, width, height};
    }

    double dirty_ratio = 1.0;
    u64 dirty_union_area_pixels = 0;
    if (dirty_rect) {
        const int dbg_w = std::max(0, dirty_rect->x1 - dirty_rect->x0);
        const int dbg_h = std::max(0, dirty_rect->y1 - dirty_rect->y0);
        const double dirty_area = static_cast<double>(dbg_w) * static_cast<double>(dbg_h);
        const double total_area = static_cast<double>(width) * height;
        if (total_area > 0) dirty_ratio = dirty_area / total_area;
        dirty_union_area_pixels = static_cast<u64>(dirty_area);
    }
    if (ctx.counters) {
        ctx.counters->dirty_union_area_pixels.store(dirty_union_area_pixels, std::memory_order_relaxed);
    }
    if (sw_renderer) {
        sw_renderer->m_last_dirty_area_ratio = dirty_ratio;
    }
    ctx.dirty_rect = dirty_rect;
    ctx.reuse_prev_framebuffer = use_dirty_rects;

    if (sw_renderer && ctx.diagnostics_enabled) {
        if (dirty_rect) {
            spdlog::info(
                "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=[{},{} -> {},{}] prev_frame={}",
                static_cast<int>(frame),
                use_dirty_rects ? 1 : 0,
                sw_renderer->m_prev_framebuffer ? 1 : 0,
                dirty_rect->x0, dirty_rect->y0, dirty_rect->x1, dirty_rect->y1,
                static_cast<int>(sw_renderer->m_prev_frame)
            );
        } else {
            spdlog::info(
                "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=null prev_frame={}",
                static_cast<int>(frame),
                use_dirty_rects ? 1 : 0,
                sw_renderer->m_prev_framebuffer ? 1 : 0,
                static_cast<int>(sw_renderer->m_prev_frame)
            );
        }
    }

    const bool fast_path_reuse = sw_renderer &&
                                 settings.enable_dirty_rects &&
                                 dirty_rect && dirty_rect->is_empty() &&
                                 sw_renderer->m_prev_framebuffer &&
                                 sw_renderer->m_prev_framebuffer->width() == width &&
                                 sw_renderer->m_prev_framebuffer->height() == height;

    const auto t_build1 = std::chrono::steady_clock::now();

    if (fast_path_reuse) {
        if (ctx.diagnostics_enabled) {
            spdlog::info("[dirty-debug] frame={} fast_path_reuse=1", static_cast<int>(frame));
        }
        sw_renderer->m_last_dirty_area_ratio = 0.0;
        sw_renderer->m_prev_layer_bboxes = std::move(current_layer_bboxes);
        sw_renderer->m_prev_frame = frame;
        const uint64_t current_fingerprint = compute_scene_fingerprint(scene, frame);
        sw_renderer->m_prev_scene_fingerprint = current_fingerprint;
        sw_renderer->m_prev_camera = resolved.camera.camera;
        sw_renderer->m_prev_camera_valid = resolved.camera.camera.enabled;
        telemetry::record_render_telemetry(make_telemetry_row(
            "scene_render", frame, width, height,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
            std::chrono::duration<double, std::milli>(t_build1 - t_build0).count(),
            0.0, 0.0, 0.0, 1,
            static_cast<int>(scene.layers().size()), ctx.counters
        ));
        profiling::g_current_counters = nullptr;
        return sw_renderer->m_prev_framebuffer;
    }

    RenderGraph graph = [&]() {
        CHRONON_ZONE_C("build_graph", trace_category::kGraph);
        return detail::build_graph(scene, ctx, resolved);
    }();
    const auto t_build2 = std::chrono::steady_clock::now();

    const auto t_exec0 = std::chrono::steady_clock::now();
    GraphExecutor executor;
    std::shared_ptr<Framebuffer> fb_shared;
    {
        CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
        fb_shared = executor.execute(graph, graph.output(), ctx);
    }
    const auto t_exec1 = std::chrono::steady_clock::now();

    if (sw_renderer) {
        sw_renderer->m_prev_framebuffer = fb_shared;
        sw_renderer->m_prev_layer_bboxes = std::move(current_layer_bboxes);
        sw_renderer->m_prev_frame = frame;
        sw_renderer->m_prev_scene_fingerprint = compute_scene_fingerprint(scene, frame);
        sw_renderer->m_prev_camera = resolved.camera.camera;
        sw_renderer->m_prev_camera_valid = resolved.camera.camera.enabled;
    }
    const auto hits_after = node_cache.stats().hits;

    const auto t1 = std::chrono::steady_clock::now();
    telemetry::record_render_telemetry(make_telemetry_row(
        "scene_render", frame, width, height,
        std::chrono::duration<double, std::milli>(t1 - t0).count(),
        std::chrono::duration<double, std::milli>(t_build2 - t_build0).count(),
        std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count(),
        0.0, 0.0,
        hits_after > hits_before ? 1 : 0,
        static_cast<int>(scene.layers().size()), ctx.counters
    ));

    profiling::g_current_counters = nullptr;
    return fb_shared;
}

std::string debug_scene_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    float fps
) {
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps
    );
    
    ctx.light_context = scene.light_context();
    if (scene.camera_2_5d().enabled) {
        ctx.camera_2_5d = scene.camera_2_5d();
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(
            ctx.camera_2_5d,
            ctx.width,
            ctx.height
        );
        ctx.projection_ctx.ready = true;
    }
    
    return GraphBuilder::build(scene, ctx).to_dot();
}

SceneGraphStats analyze_scene_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width, i32 height,
    Frame frame, f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    bool execute,
    bool include_dot,
    float fps
) {
    SceneGraphStats stats;
    stats.scene_layers = static_cast<int>(scene.layers().size());

    const auto t0 = std::chrono::steady_clock::now();

    auto ctx = make_graph_context(backend, node_cache, camera, width, height,
                                   frame, frame_time, settings, registry, video_decoder, fps);
    ctx.light_context = scene.light_context();
    if (scene.camera_2_5d().enabled) {
        ctx.camera_2_5d  = scene.camera_2_5d();
        ctx.has_camera_2_5d = true;
        ctx.projection_ctx = renderer::make_projection_context(ctx.camera_2_5d, ctx.width, ctx.height);
        ctx.projection_ctx.ready = true;
    }

    const auto t_build0 = std::chrono::steady_clock::now();
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    const auto t_build1 = std::chrono::steady_clock::now();

    stats.total_nodes    = graph.size();
    stats.output_id      = graph.has_output() ? graph.output() : k_invalid_node;
    for (GraphNodeId i = 0; i < static_cast<GraphNodeId>(graph.size()); ++i) {
        const RenderGraphNode& n = graph.node(i);
        switch (n.kind()) {
            case RenderGraphNodeKind::Source:     ++stats.source_nodes;      break;
            case RenderGraphNodeKind::Transform:  ++stats.transform_nodes;   break;
            case RenderGraphNodeKind::Effect:     ++stats.effect_nodes;      break;
            case RenderGraphNodeKind::Mask:       ++stats.mask_nodes;        break;
            case RenderGraphNodeKind::Composite:  ++stats.composite_nodes;   break;
            case RenderGraphNodeKind::Adjustment: ++stats.adjustment_nodes;  break;
            case RenderGraphNodeKind::Precomp:    ++stats.precomp_nodes;     break;
            case RenderGraphNodeKind::Video:      ++stats.video_nodes;       break;
            case RenderGraphNodeKind::MotionBlur: ++stats.motion_blur_nodes; break;
            default:                              ++stats.other_nodes;       break;
        }
        if (n.cacheable()) ++stats.cacheable_nodes;
    }

    if (include_dot) stats.dot = graph.to_dot();

    stats.build_ms = std::chrono::duration<double, std::milli>(t_build1 - t_build0).count();

    if (execute && graph.has_output()) {
        stats.cache_before = node_cache.stats();
    const auto t_exec0 = std::chrono::steady_clock::now();
    GraphExecutor executor;
    std::shared_ptr<Framebuffer> fb_shared;
    {
        CHRONON_ZONE_C("execute_graph", trace_category::kGraph);
        fb_shared = executor.execute(graph, graph.output(), ctx);
    }
    const auto t_exec1 = std::chrono::steady_clock::now();
        stats.cache_after  = node_cache.stats();
        stats.execute_ms   = std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count();
    }

    stats.total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return stats;
}

std::shared_ptr<Framebuffer> render_composition_frame(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    const Composition& comp,
    Frame frame
) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = node_cache.stats().hits;
    const float ssaa = std::max(1.0f, settings.ssaa_factor);
    const int w = comp.width();
    const int h = comp.height();
    const int rw = static_cast<int>(w * ssaa);
    const int rh = static_cast<int>(h * ssaa);
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);

    std::shared_ptr<Framebuffer> render_fb;
    double evaluate_ms = 0.0;
    double scene_ms = 0.0;
    double motion_blur_ms = 0.0;
    double downsample_ms = 0.0;
    int layer_count = 0;

    auto call_graph = [&](const Scene& scene, Frame fr, f32 t) {
        return render_scene_via_graph(
            backend, node_cache, scene, comp.camera, rw, rh, fr, t, settings, registry, video_decoder,
            static_cast<float>(comp.frame_rate().fps())
        );
    };

    if (!settings.motion_blur.enabled || settings.motion_blur.samples <= 1) {
        const auto t_eval0 = std::chrono::steady_clock::now();
        Scene scene;
        {
            CHRONON_ZONE_C("evaluate_composition", trace_category::kTimeline);
            scene = comp.evaluate(frame);
        }
        evaluate_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_eval0).count();
        layer_count = static_cast<int>(scene.layers().size());

        const auto t_scene0 = std::chrono::steady_clock::now();
        {
            CHRONON_ZONE_C("render_scene_graph", trace_category::kGraph);
            render_fb = call_graph(scene, frame, 0.0f);
        }
        scene_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_scene0).count();
    } else {
        const int N = std::max(2, settings.motion_blur.samples);
        const float shutter = settings.motion_blur.shutter_angle / 360.0f;
        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);
        const float weight = 1.0f / static_cast<float>(N);

        const auto t_mb0 = std::chrono::steady_clock::now();
        {
            CHRONON_ZONE_C("motion_blur_accumulation", trace_category::kEffect);
            for (int s = 0; s < N; ++s) {
                const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
                Scene sub = comp.evaluate(frame, t);
                if (s == 0) layer_count = static_cast<int>(sub.layers().size());
                const Framebuffer& sub_fb = *call_graph(sub, frame, t);
                for (int y = 0; y < rh; ++y) {
                    for (int x = 0; x < rw; ++x) {
                        const Color c = sub_fb.get_pixel(x, y);
                        const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                        accum[idx + 0] += c.r * weight;
                        accum[idx + 1] += c.g * weight;
                        accum[idx + 2] += c.b * weight;
                        accum[idx + 3] += c.a * weight;
                    }
                }
            }
        }
        motion_blur_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_mb0).count();

        render_fb = std::make_shared<Framebuffer>(rw, rh);
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                render_fb->set_pixel(x, y, Color{accum[idx], accum[idx+1], accum[idx+2], accum[idx+3]});
            }
        }
    }

    if (sw_renderer) {
        sw_renderer->m_last_layer_count = layer_count;
    }

    if (ssaa > 1.0f) {
        const auto t_down0 = std::chrono::steady_clock::now();
        std::unique_ptr<Framebuffer> out;
        {
            CHRONON_ZONE_C("downsample_ssaa", trace_category::kDownsample);
            out = downsample_fb(*render_fb, w, h);
        }
        downsample_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_down0).count();
        telemetry::record_render_telemetry(make_telemetry_row(
            "frame_render",
            frame,
            w,
            h,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
            evaluate_ms,
            scene_ms,
            motion_blur_ms,
            downsample_ms,
            node_cache.stats().hits > hits_before ? 1 : 0,
            layer_count,
            backend.counters()
        ));
        return std::shared_ptr<Framebuffer>(out.release());
    }

    telemetry::record_render_telemetry(make_telemetry_row(
        "frame_render",
        frame,
        w,
        h,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
        evaluate_ms,
        scene_ms,
        motion_blur_ms,
        downsample_ms,
        node_cache.stats().hits > hits_before ? 1 : 0,
        layer_count,
        backend.counters()
    ));
    return render_fb;
}

} // namespace chronon3d::graph
