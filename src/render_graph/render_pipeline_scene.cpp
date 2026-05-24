#include <chronon3d/math/projector_2_5d.hpp>
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
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
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

    std::optional<uint64_t> current_scene_fingerprint;

    auto ensure_scene_fingerprint = [&]() -> uint64_t {
        if (!current_scene_fingerprint.has_value()) {
            if (sw_renderer) {
                current_scene_fingerprint = sw_renderer->m_scene_hasher.compute_fingerprint(scene, frame);
            } else {
                current_scene_fingerprint = compute_scene_fingerprint(scene, frame);
            }
        }
        return *current_scene_fingerprint;
    };

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

        const uint64_t current_fingerprint = ensure_scene_fingerprint();
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

        tbb::enumerable_thread_specific<std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState>> tls_bboxes;
        
        tbb::parallel_for(tbb::blocked_range<size_t>(0, resolved.layers.size()), [&](const tbb::blocked_range<size_t>& r) {
            auto& local_map = tls_bboxes.local();
            for (size_t i = r.begin(); i < r.end(); ++i) {
                const auto& rl = resolved.layers[i];
                if (rl.layer && rl.layer->active_at(frame)) {
                    raster::BBox bbox = compute_bbox_for_resolved(rl, cam25d);
                    SoftwareRenderer::LayerBBoxState state;
                    state.bbox = bbox; state.world_matrix = rl.world_matrix;
                    state.opacity = rl.world_transform.opacity; state.visible = rl.layer->visible;
                    state.cache_static = rl.layer->cache_static; state.is_3d = rl.layer->is_3d;
                    local_map[std::string(rl.layer->name)] = state;
                }
            }
        });

        for (auto& local_map : tls_bboxes) {
            for (auto&& [name, state] : local_map) {
                current_layer_bboxes[name] = std::move(state);
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

            auto same_bbox = [](const raster::BBox& a, const raster::BBox& b) {
                return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
            };

            auto add_layer_dirty = [&](const SoftwareRenderer::LayerBBoxState& curr,
                                       const SoftwareRenderer::LayerBBoxState* prev) {
                const bool prev_exists = prev != nullptr;
                const bool curr_visible = curr.visible;
                const bool prev_visible = prev_exists ? prev->visible : false;

                if (!prev_exists) {
                    add_dirty_bbox(curr.bbox);
                    return;
                }

                if (curr_visible != prev_visible) {
                    add_dirty_bbox(curr_visible ? curr.bbox : prev->bbox);
                    return;
                }

                if (!curr_visible) {
                    return;
                }

                const bool geometry_changed =
                    (camera_changed && curr.is_3d) ||
                    (curr.world_matrix != prev->world_matrix);
                const bool content_changed =
                    !curr.cache_static ||
                    curr.opacity != prev->opacity;

                if (geometry_changed) {
                    if (same_bbox(curr.bbox, prev->bbox)) {
                        add_dirty_bbox(curr.bbox);
                    } else {
                        add_dirty_bbox(curr.bbox);
                        add_dirty_bbox(prev->bbox);
                    }
                    return;
                }

                if (content_changed) {
                    add_dirty_bbox(curr.bbox);
                }
            };

            for (const auto& pair : current_layer_bboxes) {
                const auto& name = pair.first;
                const auto& curr = pair.second;
                auto prev_it = sw_renderer->m_prev_layer_bboxes.find(name);
                add_layer_dirty(
                    curr,
                    prev_it == sw_renderer->m_prev_layer_bboxes.end()
                        ? nullptr
                        : &prev_it->second
                );
            }
            for (const auto& pair : sw_renderer->m_prev_layer_bboxes) {
                if (current_layer_bboxes.find(pair.first) == current_layer_bboxes.end()) {
                    add_dirty_bbox(pair.second.bbox);
                }
            }

            // ── P2: Scroll Optimization ─────────────────────────────────────
            bool scroll_detected = false;
            i32 dx = 0, dy = 0;

            if (sw_renderer->m_prev_camera_valid && sw_renderer->m_prev_camera.enabled && cam25d.enabled) {
                const Vec3& cp = cam25d.position;
                const Vec3& pp = sw_renderer->m_prev_camera.position;
                Vec3 camera_delta = cp - pp;
                
                bool camera_params_compatible = (std::abs(sw_renderer->m_prev_camera.zoom - cam25d.zoom) < 1e-3f);
                
                if (camera_params_compatible && std::abs(camera_delta.z) < 1e-3f &&
                    std::abs(camera_delta.x - std::round(camera_delta.x)) < 0.1f &&
                    std::abs(camera_delta.y - std::round(camera_delta.y)) < 0.1f) {
                    
                    dx = -static_cast<i32>(std::round(camera_delta.x));
                    dy = -static_cast<i32>(std::round(camera_delta.y));
                    scroll_detected = true;
                }
            }

            if (scroll_detected && (dx != 0 || dy != 0) && std::abs(dx) < width && std::abs(dy) < height) {
                CHRONON_ZONE_C("scroll_optimization", trace_category::kFrame);

                if (sw_renderer->m_prev_framebuffer.use_count() > 1) {
                    sw_renderer->m_prev_framebuffer = std::make_shared<Framebuffer>(*sw_renderer->m_prev_framebuffer);
                }
                sw_renderer->m_prev_framebuffer->shift(dx, dy);

                raster::BBox strip{0, 0, width, height};
                if (dx > 0) strip.x1 = dx;
                else if (dx < 0) strip.x0 = width + dx;
                if (dy > 0) strip.y1 = dy;
                else if (dy < 0) strip.y0 = height + dy;
                dirty_rect = strip;
            } else {
                dirty_rect = has_dirty ? std::optional(union_dirty) : std::optional(raster::BBox{0, 0, 0, 0});
            }
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
        sw_renderer->m_prev_scene_fingerprint = ensure_scene_fingerprint();
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
    std::shared_ptr<Framebuffer> fb_shared;
    {
        CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
        if (sw_renderer && sw_renderer->executor()) {
            fb_shared = sw_renderer->executor()->execute(graph, graph.output(), ctx);
        } else {
            GraphExecutor local_executor;
            fb_shared = local_executor.execute(graph, graph.output(), ctx);
        }
    }
    const auto t_exec1 = std::chrono::steady_clock::now();

    if (sw_renderer) {
        sw_renderer->m_prev_framebuffer = fb_shared;
        sw_renderer->m_prev_layer_bboxes = std::move(current_layer_bboxes);
        sw_renderer->m_prev_frame = frame;
        sw_renderer->m_prev_scene_fingerprint = ensure_scene_fingerprint();
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

} // namespace chronon3d::graph
