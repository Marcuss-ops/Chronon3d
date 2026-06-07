#include "execution_state.hpp"
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace chronon3d::graph {

void emit_node_records(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const cache::NodeCacheKey& key,
    const CachedFB& result,
    const std::optional<raster::BBox>& clip_rect,
    const std::string& cache_status,
    bool is_cacheable,
    int input_count,
    double duration_ms
) {
    const bool do_lazy_strings = is_cacheable && ctx.node_cache;
    const bool has_layer = !node.layer_id().empty();

    // Cache repeated string values to avoid redundant copies.
    // node.name() and node.layer_id() are called up to 3× each.
    // Only allocate when at least one consumer needs them.
    const bool need_strings = do_lazy_strings || ctx.diagnostics_enabled || has_layer;
    const std::string cached_name = need_strings ? node.name() : std::string{};
    const std::string cached_layer_id = need_strings ? node.layer_id() : std::string{};
    const std::string cached_kind_str = need_strings ? std::string(to_string(node.kind())) : std::string{};

    // Cache telemetry record
    {
        telemetry::CacheTelemetryRecord cache_rec;
        cache_rec.frame_number = static_cast<int>(ctx.frame);
        cache_rec.node_name = cached_name;
        cache_rec.cacheable = is_cacheable;
        cache_rec.cache_status = cache_status;
        if (do_lazy_strings) {
            cache_rec.key_digest = key.digest();
            cache_rec.params_hash = key.params_hash;
            cache_rec.source_hash = key.source_hash;
            cache_rec.input_hash = key.input_hash;
            cache_rec.key_width = key.width;
            cache_rec.key_height = key.height;
            cache_rec.key_frame = static_cast<int>(key.frame);
            cache_rec.key_tile_x = key.tile_x;
            cache_rec.key_tile_y = key.tile_y;
            cache_rec.key_tile_size = key.tile_size;
            cache_rec.key_tile_hash = key.tile_hash;
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
        rec.node_name = cached_name;
        rec.node_type = cached_kind_str;
        rec.duration_ms = duration_ms;
        rec.cache_status = cache_status;
        if (do_lazy_strings) {
            rec.cache_key_digest = std::to_string(key.digest());
        }
        rec.input_count = input_count;
        rec.layer_id = cached_layer_id;
        if (result) {
            rec.output_width = result->width();
            rec.output_height = result->height();
            rec.output_bytes = static_cast<uint64_t>(result->width()) *
                               static_cast<uint64_t>(result->height()) * 4;
        }
        telemetry::record_node_telemetry(std::move(rec));
    }

    // Layer telemetry record
    if (has_layer) {
        telemetry::LayerTelemetryRecord layer_rec;
        layer_rec.frame_number = static_cast<int>(ctx.frame);
        layer_rec.layer_id = cached_layer_id;
        layer_rec.layer_name = cached_name;
        layer_rec.layer_type = cached_kind_str;
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
                     static_cast<int>(ctx.frame), cached_name, cached_kind_str, cache_status, duration_ms);
    }
}

} // namespace chronon3d::graph
