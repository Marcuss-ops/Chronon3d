#include "graph_executor_internal.hpp"
#include <chronon3d/cache/disk_node_cache.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

namespace chronon3d::graph {

double run_node(
    RenderGraphNode& node,
    RenderGraphContext& node_ctx,
    std::span<const std::shared_ptr<Framebuffer>> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes,
    bool use_cache,
    const cache::NodeCacheKey& key,
    std::shared_ptr<Framebuffer>& result,
    const RenderGraphContext& ctx
) {
    if (result) {
        return 0.0;
    }

    const auto exec_t0 = std::chrono::steady_clock::now();
    {
        TraceScope scope(node_ctx.trace, node.name(), "node_execute", node_ctx.frame);
        result = node.execute(node_ctx, inputs, input_bboxes);
    }
    if (ctx.counters) {
        ctx.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
    if (result) {
        result->set_key_digest(key.digest());
    }
    if (use_cache && result) {
        ctx.node_cache->store(key, result);
        if (node.cache_policy().disk_cacheable) {
            cache::DiskNodeCache::instance().put(key, *result);
        }
    }
    const auto exec_t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(exec_t1 - exec_t0).count();
}

void emit_node_records(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const cache::NodeCacheKey& key,
    const std::shared_ptr<Framebuffer>& result,
    const std::optional<raster::BBox>& clip_rect,
    const std::string& cache_status,
    bool is_cacheable,
    int input_count,
    double duration_ms
) {
    const bool do_lazy_strings = is_cacheable && ctx.node_cache;

    // Cache telemetry record
    {
        telemetry::CacheTelemetryRecord cache_rec;
        cache_rec.frame_number = static_cast<int>(ctx.frame);
        cache_rec.node_name = node.name();
        cache_rec.cacheable = is_cacheable;
        cache_rec.cache_status = cache_status;
        if (do_lazy_strings) {
            cache_rec.key_digest = std::to_string(key.digest());
            cache_rec.params_hash = std::to_string(key.params_hash);
            cache_rec.source_hash = std::to_string(key.source_hash);
            cache_rec.input_hash = std::to_string(key.input_hash);
        }
        if (result) {
            cache_rec.output_bytes = static_cast<uint64_t>(result->width()) *
                                     static_cast<uint64_t>(result->height()) * 4;
        }
        telemetry::record_cache_telemetry(std::move(cache_rec));
    }

    // Node telemetry record
    {
        telemetry::NodeTelemetryRecord rec;
        rec.frame_number = static_cast<int>(ctx.frame);
        rec.node_name = node.name();
        rec.node_type = std::string(to_string(node.kind()));
        rec.duration_ms = duration_ms;
        rec.cache_status = cache_status;
        if (do_lazy_strings) {
            rec.cache_key_digest = std::to_string(key.digest());
        }
        rec.input_count = input_count;
        rec.layer_id = node.layer_id();
        if (result) {
            rec.output_width = result->width();
            rec.output_height = result->height();
            rec.output_bytes = static_cast<uint64_t>(result->width()) *
                               static_cast<uint64_t>(result->height()) * 4;
        }
        telemetry::record_node_telemetry(std::move(rec));
    }

    // Layer telemetry record
    if (!node.layer_id().empty()) {
        telemetry::LayerTelemetryRecord layer_rec;
        layer_rec.frame_number = static_cast<int>(ctx.frame);
        layer_rec.layer_id = node.layer_id();
        layer_rec.layer_name = node.name();
        layer_rec.layer_type = std::string(to_string(node.kind()));
        layer_rec.duration_ms = duration_ms;
        layer_rec.visible = true;
        if (clip_rect) {
            const int w = std::max(0, clip_rect->x1 - clip_rect->x0);
            const int h = std::max(0, clip_rect->y1 - clip_rect->y0);
            layer_rec.bbox_w = static_cast<float>(w);
            layer_rec.bbox_h = static_cast<float>(h);
            layer_rec.area_pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
            layer_rec.visible_pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
        } else if (result) {
            layer_rec.bbox_w = static_cast<float>(result->width());
            layer_rec.bbox_h = static_cast<float>(result->height());
            layer_rec.area_pixels = result->width() * result->height();
            layer_rec.visible_pixels = result->width() * result->height();
        }
        telemetry::record_layer_telemetry(std::move(layer_rec));
    }

    // Per-node log line
    if (ctx.diagnostics_enabled) {
        spdlog::info("[graph-debug] frame={} node='{}' kind='{}' cache_status='{}' dur={:.3f}ms",
                     static_cast<int>(ctx.frame), node.name(), to_string(node.kind()), cache_status, duration_ms);
    }
}

} // namespace chronon3d::graph
