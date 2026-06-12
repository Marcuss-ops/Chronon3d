#include "pool_preallocation.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_map>
#include <cmath>

namespace chronon3d::graph {

std::vector<cache::FramebufferPoolPreallocOptions> predict_pool_requirements(
    const CompiledFrameGraph& compiled,
    int canvas_width,
    int canvas_height)
{
    if (compiled.empty() || compiled.levels.empty()) {
        return {};
    }

    std::unordered_map<cache::FramebufferPoolKey, size_t, cache::FramebufferPoolKeyHash> peak_counts;

    for (const auto& level : compiled.levels) {
        std::unordered_map<cache::FramebufferPoolKey, size_t, cache::FramebufferPoolKeyHash> level_counts;
        for (GraphNodeId id : level) {
            if (id >= compiled.nodes.size()) continue;
            const auto& info = compiled.nodes[id];
            if (!info.reachable) continue;

            int w = canvas_width;
            int h = canvas_height;
            if (info.predicted_bbox && !info.predicted_bbox->is_empty()) {
                w = std::max(0, info.predicted_bbox->x1 - info.predicted_bbox->x0);
                h = std::max(0, info.predicted_bbox->y1 - info.predicted_bbox->y0);
            }
            const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(w, h);
            if (bw > 0 && bh > 0) {
                ++level_counts[cache::FramebufferPoolKey{bw, bh}];
            }
        }
        for (const auto& [bucket, cnt] : level_counts) {
            auto& peak = peak_counts[bucket];
            peak = std::max(peak, cnt);
        }
    }

    constexpr size_t kMaxPreallocPerBucket = 5;
    std::vector<cache::FramebufferPoolPreallocOptions> predictions;
    predictions.reserve(peak_counts.size() + 1);

    {
        const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(canvas_width, canvas_height);
        predictions.push_back(cache::FramebufferPoolPreallocOptions{
            .width  = bw,
            .height = bh,
            .count  = 4,
            .clear  = true,
            .touch_memory = false,
        });
    }

    for (const auto& [bucket, count] : peak_counts) {
        const size_t capped_count = std::min(count, kMaxPreallocPerBucket);
        predictions.push_back(cache::FramebufferPoolPreallocOptions{
            .width  = bucket.width,
            .height = bucket.height,
            .count  = capped_count,
            .clear  = true,
            .touch_memory = false,
        });
    }
    return predictions;
}

size_t preallocate_for_frame(
    cache::FramebufferPool& pool,
    const CompiledFrameGraph& compiled,
    int width,
    int height,
    chronon3d::RenderCounters* counters,
    bool diagnostics_enabled)
{
    const auto t_prealloc0 = profiling::now();
    auto predictions = predict_pool_requirements(compiled, width, height);
    size_t prealloc_total = 0;
    for (const auto& pred : predictions) {
        prealloc_total += pool.ensure_preallocated(pred);
    }
    const auto t_prealloc1 = profiling::now();
    const double prealloc_ms = profiling::duration_ms(t_prealloc0, t_prealloc1);

    if (counters) {
        counters->framebuffer_prealloc_created.fetch_add(prealloc_total, std::memory_order_relaxed);
    }

    if (diagnostics_enabled) {
        spdlog::info("[pool-prealloc] ensured={} predictions={} ms={:.2f}",
            prealloc_total, predictions.size(), prealloc_ms);
    }

    return prealloc_total;
}

} // namespace chronon3d::graph
