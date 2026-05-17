#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>

namespace chronon3d::graph {

namespace {

std::unique_ptr<Framebuffer> downsample_fb(const Framebuffer& src, i32 dst_w, i32 dst_h) {
    auto dst = std::make_unique<Framebuffer>(dst_w, dst_h);
    const float sx = static_cast<float>(src.width()) / static_cast<float>(dst_w);
    const float sy = static_cast<float>(src.height()) / static_cast<float>(dst_h);
    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            const int x0 = static_cast<int>(x * sx);
            const int y0 = static_cast<int>(y * sy);
            const int x1 = std::min(static_cast<int>((x + 1) * sx), src.width());
            const int y1 = std::min(static_cast<int>((y + 1) * sy), src.height());
            for (int iy = y0; iy < y1; ++iy) {
                for (int ix = x0; ix < x1; ++ix) {
                    const Color c = src.get_pixel(ix, iy);
                    r += c.r; g += c.g; b += c.b; a += c.a;
                    ++count;
                }
            }
            if (count > 0) {
                const float inv = 1.0f / count;
                dst->set_pixel(x, y, Color{r * inv, g * inv, b * inv, a * inv});
            }
        }
    }
    return dst;
}

RenderGraphContext make_graph_context(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder
) {
    return RenderGraphContext{
        .frame = frame,
        .time_seconds = frame_time,
        .width = width,
        .height = height,
        .camera = camera,
        .backend = &backend,
        .node_cache = &node_cache,
        .registry = registry,
        .video_decoder = video_decoder,
        .ssaa_factor = settings.ssaa_factor,
        .modular_coordinates = settings.use_modular_graph
    };
}

} // namespace

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
    video::VideoFrameDecoder* video_decoder
) {
    ZoneScoped;
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits_before = node_cache.stats().hits;

    const auto t_build0 = std::chrono::steady_clock::now();
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder
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
    
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    const auto t_build1 = std::chrono::steady_clock::now();

    const auto t_exec0 = std::chrono::steady_clock::now();
    GraphExecutor executor;
    auto fb_shared = executor.execute(graph, graph.output(), ctx);
    const auto t_exec1 = std::chrono::steady_clock::now();
    const auto hits_after = node_cache.stats().hits;

    const auto t1 = std::chrono::steady_clock::now();
    telemetry::record_render_telemetry({
        .event = "scene_render",
        .frame = frame,
        .width = width,
        .height = height,
        .total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(),
        .setup_ms = std::chrono::duration<double, std::milli>(t_build1 - t_build0).count(),
        .composite_ms = std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count(),
        .cache_hit = hits_after > hits_before ? 1 : 0,
        .layer_count = static_cast<int>(scene.layers().size()),
    });
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
    video::VideoFrameDecoder* video_decoder
) {
    auto ctx = make_graph_context(
        backend, node_cache, camera, width, height, frame, frame_time,
        settings, registry, video_decoder
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
    bool include_dot
) {
    SceneGraphStats stats;
    stats.scene_layers = static_cast<int>(scene.layers().size());

    const auto t0 = std::chrono::steady_clock::now();

    auto ctx = make_graph_context(backend, node_cache, camera, width, height,
                                   frame, frame_time, settings, registry, video_decoder);
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
        executor.execute(graph, graph.output(), ctx);
        const auto t_exec1 = std::chrono::steady_clock::now();
        stats.cache_after  = node_cache.stats();
        stats.execute_ms   = std::chrono::duration<double, std::milli>(t_exec1 - t_exec0).count();
    }

    stats.total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return stats;
}

std::unique_ptr<Framebuffer> render_composition_frame(
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

    std::unique_ptr<Framebuffer> render_fb;
    double evaluate_ms = 0.0;
    double scene_ms = 0.0;
    double motion_blur_ms = 0.0;
    double downsample_ms = 0.0;
    int layer_count = 0;

    auto call_graph = [&](const Scene& scene, Frame fr, f32 t) {
        return render_scene_via_graph(
            backend, node_cache, scene, comp.camera, rw, rh, fr, t, settings, registry, video_decoder
        );
    };

    if (!settings.motion_blur.enabled || settings.motion_blur.samples <= 1) {
        const auto t_eval0 = std::chrono::steady_clock::now();
        Scene scene = comp.evaluate(frame);
        evaluate_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_eval0).count();
        layer_count = static_cast<int>(scene.layers().size());

        const auto t_scene0 = std::chrono::steady_clock::now();
        render_fb = std::make_unique<Framebuffer>(*call_graph(scene, frame, 0.0f));
        scene_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_scene0).count();
    } else {
        const int N = std::max(2, settings.motion_blur.samples);
        const float shutter = settings.motion_blur.shutter_angle / 360.0f;
        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);
        const float weight = 1.0f / static_cast<float>(N);

        const auto t_mb0 = std::chrono::steady_clock::now();
        for (int s = 0; s < N; ++s) {
            const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
            Scene sub = comp.evaluate(frame, t);
            if (s == 0) layer_count = static_cast<int>(sub.layers().size());
            const Framebuffer sub_fb = *call_graph(sub, frame, t);
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
        motion_blur_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_mb0).count();

        render_fb = std::make_unique<Framebuffer>(rw, rh);
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                render_fb->set_pixel(x, y, Color{accum[idx], accum[idx+1], accum[idx+2], accum[idx+3]});
            }
        }
    }

    if (ssaa > 1.0f) {
        const auto t_down0 = std::chrono::steady_clock::now();
        auto out = downsample_fb(*render_fb, w, h);
        downsample_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_down0).count();
        telemetry::record_render_telemetry({
            .event = "frame_render", .frame = frame, .width = w, .height = h,
            .total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
            .setup_ms = evaluate_ms, .composite_ms = scene_ms,
            .blur_ms = motion_blur_ms, .encode_ms = downsample_ms,
            .cache_hit = node_cache.stats().hits > hits_before ? 1 : 0,
            .layer_count = layer_count,
        });
        return out;
    }

    telemetry::record_render_telemetry({
        .event = "frame_render", .frame = frame, .width = w, .height = h,
        .total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
        .setup_ms = evaluate_ms, .composite_ms = scene_ms,
        .blur_ms = motion_blur_ms, .encode_ms = downsample_ms,
        .cache_hit = node_cache.stats().hits > hits_before ? 1 : 0,
        .layer_count = layer_count,
    });
    return render_fb;
}

} // namespace chronon3d::graph
