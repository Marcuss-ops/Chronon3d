#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/counters.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace chronon3d::graph {

namespace {

template <typename T>
[[nodiscard]] bool contains_index(const std::vector<T>& values, GraphNodeId id) {
    return id < values.size();
}

} // namespace

GraphExecutor::GraphExecutor()
    : m_arena(std::max(1u, std::thread::hardware_concurrency())) {}

GraphExecutor::ExecutionPlan GraphExecutor::build_execution_plan(RenderGraph& graph, GraphNodeId output) const {
    ExecutionPlan plan;
    const size_t node_count = graph.size();
    if (node_count == 0 || output >= node_count) {
        return plan;
    }

    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    std::vector<size_t> indegree(node_count, 0);
    plan.consumer_counts.assign(node_count, 0);

    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) {
            continue;
        }
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) {
                continue;
            }
            children[parent].push_back(child);
            ++indegree[child];
            ++plan.consumer_counts[parent];
        }
    }

    std::vector<GraphNodeId> current_level;
    current_level.reserve(node_count);
    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (reachable[id] && indegree[id] == 0) {
            current_level.push_back(id);
        }
    }

    size_t scheduled = 0;
    while (!current_level.empty()) {
        plan.levels.push_back(current_level);
        scheduled += current_level.size();

        std::vector<GraphNodeId> next_level;
        for (GraphNodeId id : current_level) {
            for (GraphNodeId child : children[id]) {
                if (--indegree[child] == 0) {
                    next_level.push_back(child);
                }
            }
        }
        current_level.swap(next_level);
    }

    const size_t reachable_count = static_cast<size_t>(
        std::count(reachable.begin(), reachable.end(), static_cast<char>(1))
    );
    if (scheduled != reachable_count) {
        throw std::runtime_error("GraphExecutor: graph is not a DAG or contains unreachable dependency cycles");
    }

    return plan;
}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx
) {
    const auto plan = build_execution_plan(graph, output);
    if (plan.levels.empty()) {
        return nullptr;
    }

    return m_arena.execute([&]() -> std::shared_ptr<Framebuffer> {
        std::vector<std::shared_ptr<Framebuffer>> temp(graph.size());
        std::vector<u64> resolved_key_digest(graph.size(), 0);
        std::vector<char> resolved_frame_dependent(graph.size(), 0);
        std::vector<std::optional<raster::BBox>> resolved_bboxes(graph.size());
        std::vector<std::atomic_size_t> consumer_remaining(graph.size());
        for (size_t i = 0; i < plan.consumer_counts.size(); ++i) {
            consumer_remaining[i].store(plan.consumer_counts[i], std::memory_order_relaxed);
        }

        for (const auto& level : plan.levels) {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, level.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t level_index = range.begin(); level_index != range.end(); ++level_index) {
                        const GraphNodeId id = level[level_index];
                        auto trace = profiling::g_current_trace;
                        auto frame = profiling::g_current_frame;
                        auto counters = profiling::g_current_counters;
                        auto framebuffer_pool = profiling::g_current_framebuffer_pool;
                profiling::g_current_trace = trace;
                profiling::g_current_frame = frame;
                profiling::g_current_counters = counters;
                profiling::g_current_framebuffer_pool = framebuffer_pool;

                auto& node = graph.node(id);
                const auto& input_ids = graph.inputs(id);

                std::vector<std::shared_ptr<Framebuffer>> inputs(input_ids.size());
                std::vector<std::optional<raster::BBox>> input_bboxes(input_ids.size());

                bool inputs_frame_dependent = false;
                bool has_cacheable_inputs = false;
                u64 input_hash = 0;
                for (size_t i = 0; i < input_ids.size(); ++i) {
                    const GraphNodeId input_id = input_ids[i];
                    if (contains_index(temp, input_id)) {
                        inputs[i] = temp[input_id];
                    }
                    if (contains_index(resolved_bboxes, input_id)) {
                        input_bboxes[i] = resolved_bboxes[input_id];
                    }
                    if (contains_index(resolved_frame_dependent, input_id)) {
                        inputs_frame_dependent |= (resolved_frame_dependent[input_id] != 0);
                        has_cacheable_inputs = true;
                    }
                    if (contains_index(resolved_key_digest, input_id)) {
                        input_hash = hash_combine(input_hash, resolved_key_digest[input_id]);
                    }
                }

                const auto policy = node.cache_policy();
                const bool is_cacheable = policy.cacheable;
                cache::NodeCacheKey key;
                std::shared_ptr<Framebuffer> result;
                std::string cache_status;

                const bool node_frame_dependent =
                    policy.frame_dependent ||
                    (has_cacheable_inputs && inputs_frame_dependent);

                const bool use_cache = is_cacheable && ctx.node_cache && !node_frame_dependent;

                if (use_cache) {
                    key = node.cache_key(ctx);
                    key.input_hash = input_hash;

                    result = ctx.node_cache->get(key);
                    if (ctx.counters) {
                        if (result) {
                            ctx.counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
                            cache_status = "hit";
                        } else {
                            ctx.counters->cache_misses.fetch_add(1, std::memory_order_relaxed);
                            cache_status = "miss";
                        }
                    } else {
                        cache_status = result ? "hit" : "miss";
                    }
                } else {
                    if (!ctx.node_cache) {
                        cache_status = "bypass_no_cache";
                    } else if (!is_cacheable) {
                        cache_status = "bypass_not_cacheable";
                    } else {
                        cache_status = "bypass_frame_dependent";
                    }
                }

                const auto predicted_bbox = node.predicted_bbox(ctx, input_bboxes);

                RenderGraphContext node_ctx = ctx;
                if (ctx.dirty_rects_enabled) {
                    std::optional<raster::BBox> clip = predicted_bbox;
                    if (ctx.dirty_rect) {
                        if (clip) {
                            clip = raster::BBox{
                                .x0 = std::max(clip->x0, ctx.dirty_rect->x0),
                                .y0 = std::max(clip->y0, ctx.dirty_rect->y0),
                                .x1 = std::min(clip->x1, ctx.dirty_rect->x1),
                                .y1 = std::min(clip->y1, ctx.dirty_rect->y1)
                            };
                            if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1) {
                                clip = raster::BBox{0, 0, 0, 0};
                            }
                        } else {
                            clip = ctx.dirty_rect;
                        }
                    }
                    node_ctx.clip_rect = clip;

                    if (ctx.counters) {
                        if (predicted_bbox) {
                            const int w = std::max(0, predicted_bbox->x1 - predicted_bbox->x0);
                            const int h = std::max(0, predicted_bbox->y1 - predicted_bbox->y0);
                            ctx.counters->dirty_rect_count.fetch_add(1, std::memory_order_relaxed);
                            ctx.counters->dirty_pixels.fetch_add(
                                static_cast<uint64_t>(w) * static_cast<uint64_t>(h),
                                std::memory_order_relaxed
                            );
                        } else {
                            ctx.counters->dirty_full_fallbacks.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                } else {
                    node_ctx.clip_rect = predicted_bbox;
                }

                const auto exec_t0 = std::chrono::steady_clock::now();
                if (!result) {
                    TraceScope scope(node_ctx.trace, node.name(), "node_execute", node_ctx.frame);
                    result = node.execute(node_ctx, inputs, input_bboxes);
                    if (ctx.counters) {
                        ctx.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (use_cache && result) {
                        ctx.node_cache->store(key, result);
                    }
                }
                const auto exec_t1 = std::chrono::steady_clock::now();
                const double duration_ms = std::chrono::duration<double, std::milli>(exec_t1 - exec_t0).count();

                {
                    telemetry::CacheTelemetryRecord cache_rec;
                    cache_rec.frame_number = static_cast<int>(ctx.frame);
                    cache_rec.node_name = node.name();
                    cache_rec.cacheable = is_cacheable;
                    cache_rec.cache_status = cache_status;
                    cache_rec.key_digest = (is_cacheable && ctx.node_cache) ? std::to_string(key.digest()) : "";
                    cache_rec.params_hash = (is_cacheable && ctx.node_cache) ? std::to_string(key.params_hash) : "";
                    cache_rec.source_hash = (is_cacheable && ctx.node_cache) ? std::to_string(key.source_hash) : "";
                    cache_rec.input_hash = (is_cacheable && ctx.node_cache) ? std::to_string(key.input_hash) : "";
                    if (result) {
                        cache_rec.output_bytes = static_cast<uint64_t>(result->width()) *
                                                 static_cast<uint64_t>(result->height()) * 4;
                    }
                    telemetry::record_cache_telemetry(std::move(cache_rec));
                }

                {
                    telemetry::NodeTelemetryRecord rec;
                    rec.frame_number = static_cast<int>(ctx.frame);
                    rec.node_name = node.name();
                    rec.node_type = std::string(to_string(node.kind()));
                    rec.duration_ms = duration_ms;
                    rec.cache_status = cache_status;
                    rec.cache_key_digest = (is_cacheable && ctx.node_cache)
                        ? std::to_string(key.digest()) : "";
                    rec.input_count = static_cast<int>(input_ids.size());
                    rec.layer_id = node.layer_id();
                    if (result) {
                        rec.output_width = result->width();
                        rec.output_height = result->height();
                        rec.output_bytes = static_cast<uint64_t>(result->width()) *
                                           static_cast<uint64_t>(result->height()) * 4;
                    }
                    telemetry::record_node_telemetry(std::move(rec));
                }

                temp[id] = result;
                resolved_key_digest[id] = key.digest();
                resolved_frame_dependent[id] = node_frame_dependent ? 1 : 0;
                resolved_bboxes[id] = predicted_bbox;

                for (GraphNodeId input_id : input_ids) {
                    if (!contains_index(consumer_remaining, input_id)) {
                        continue;
                    }
                    if (consumer_remaining[input_id].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        temp[input_id].reset();
                        resolved_key_digest[input_id] = 0;
                        resolved_frame_dependent[input_id] = 0;
                        resolved_bboxes[input_id].reset();
                    }
                }
                    }
                }
            );
        }

        return temp[output];
    });
}

} // namespace chronon3d::graph
