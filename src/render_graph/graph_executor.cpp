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
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

namespace {

template <typename T>
[[nodiscard]] bool contains_index(const std::vector<T>& values, GraphNodeId id) {
    return id < values.size();
}

// ── Shared execution state (Phase A) ────────────────────────────────────────
//
// Wraps the mutable vectors that track per-node intermediate results across
// the level-by-level graph walk.  Owned inside execute() and passed to helpers.
struct ExecutionState {
    std::vector<std::shared_ptr<Framebuffer>> temp;
    std::vector<u64> resolved_key_digest;
    std::vector<char> resolved_frame_dependent;
    std::vector<char> resolved_cache_hit;
    std::vector<std::optional<raster::BBox>> resolved_bboxes;
};

// ── Pre-resolved inputs for a single node ───────────────────────────────────
//
// Built sequentially before the parallel_for loop to avoid concurrent reads
// on the shared ExecutionState vectors.
struct PreResolvedNode {
    std::vector<std::shared_ptr<Framebuffer>> inputs;
    std::vector<std::optional<raster::BBox>> input_bboxes;
    bool inputs_frame_dependent = false;
    bool has_cacheable_inputs = false;
    bool inputs_all_cache_hits = false;
    u64 input_hash = 0;
};

// ── Cache evaluation result ─────────────────────────────────────────────────
struct CacheEvalResult {
    std::shared_ptr<Framebuffer> result;
    cache::NodeCacheKey key;
    std::string cache_status;
    bool node_frame_dependent = false;
    bool use_cache = false;
    bool is_cacheable = false;
};

// ── Phase B: Resolve inputs for a single node ───────────────────────────────
//
// Reads the shared ExecutionState vectors to populate a PreResolvedNode.
// Must be called sequentially before the parallel_for for each level.
[[nodiscard]] PreResolvedNode resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    const ExecutionState& state
) {
    const auto& input_ids = graph.inputs(id);
    PreResolvedNode pr;
    pr.inputs.resize(input_ids.size());
    pr.input_bboxes.resize(input_ids.size());
    pr.inputs_all_cache_hits = !input_ids.empty();

    for (size_t j = 0; j < input_ids.size(); ++j) {
        const GraphNodeId input_id = input_ids[j];
        if (contains_index(state.temp, input_id)) {
            pr.inputs[j] = state.temp[input_id];
        }
        if (contains_index(state.resolved_bboxes, input_id)) {
            pr.input_bboxes[j] = state.resolved_bboxes[input_id];
        }
        if (contains_index(state.resolved_frame_dependent, input_id)) {
            pr.inputs_frame_dependent |= (state.resolved_frame_dependent[input_id] != 0);
            pr.has_cacheable_inputs = true;
        }
        if (contains_index(state.resolved_cache_hit, input_id)) {
            pr.inputs_all_cache_hits &= (state.resolved_cache_hit[input_id] != 0);
        } else {
            pr.inputs_all_cache_hits = false;
        }
        if (contains_index(state.resolved_key_digest, input_id)) {
            pr.input_hash = hash_combine(pr.input_hash, state.resolved_key_digest[input_id]);
        }
    }
    return pr;
}

// ── Phase C: Evaluate cache for a node ──────────────────────────────────────
//
// Decides cache eligibility, performs lookup, and updates cache hit/miss/bypass
// counters.  Logs bypass reasons via spdlog.
[[nodiscard]] CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id,
    bool inputs_all_cache_hits
) {
    CacheEvalResult cr;
    const auto policy = node.cache_policy();
    bool is_cacheable = policy.cacheable;

    cr.node_frame_dependent =
        policy.frame_dependent ||
        (has_cacheable_inputs && inputs_frame_dependent);

    if (node.kind() == RenderGraphNodeKind::Composite && inputs_all_cache_hits) {
        is_cacheable = true;
        cr.node_frame_dependent = false;
    }

    cr.use_cache = is_cacheable && ctx.node_cache && !cr.node_frame_dependent;

    cr.is_cacheable = is_cacheable;

    if (cr.use_cache) {
        cr.key = node.cache_key(ctx);
        cr.key.input_hash = input_hash;

        cr.result = ctx.node_cache->get(cr.key);
        if (ctx.counters) {
            if (cr.result) {
                ctx.counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "hit";
            } else {
                ctx.counters->cache_misses.fetch_add(1, std::memory_order_relaxed);
                cr.cache_status = "miss";
            }
        } else {
            cr.cache_status = cr.result ? "hit" : "miss";
        }
    } else {
        if (!ctx.node_cache) {
            cr.cache_status = "bypass_no_cache";
        } else if (!is_cacheable) {
            cr.cache_status = "bypass_not_cacheable";
            if (ctx.counters) {
                ctx.counters->bypass_not_cacheable_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (ctx.diagnostics_enabled) {
                spdlog::warn("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='not_cacheable'",
                             static_cast<int>(ctx.frame), node.name(), node_id, to_string(node.kind()));
            }
        } else {
            cr.cache_status = "bypass_frame_dependent";
            if (ctx.diagnostics_enabled) {
                spdlog::debug("[cache-bypass] frame={} node='{}' node_id={} kind='{}' reason='frame_dependent'",
                              static_cast<int>(ctx.frame), node.name(), node_id, to_string(node.kind()));
            }
        }
    }
    return cr;
}

// ── Phase D: Compute dirty-rect clip and update dirty counters ──────────────
//
// Returns the clip_rect for the node.  Updates dirty_rect_count, dirty_pixels,
// dirty_full_fallbacks, and dirty_full_fallback_reasons as side effects.
[[nodiscard]] std::optional<raster::BBox> compute_dirty_clip(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const std::optional<raster::BBox>& predicted_bbox
) {
    CHRONON_ZONE_C("dirty_rect_clip", trace_category::kPipeline);

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
            const auto reason = [&]() -> DirtyFallbackReason {
                switch (node.kind()) {
                    case RenderGraphNodeKind::Composite:
                        return DirtyFallbackReason::CompositeMissingInputBounds;
                    case RenderGraphNodeKind::Transform:
                        return DirtyFallbackReason::TransformBoundsUnknown;
                    case RenderGraphNodeKind::Effect:
                    case RenderGraphNodeKind::Mask:
                    case RenderGraphNodeKind::Adjustment:
                    case RenderGraphNodeKind::MotionBlur:
                    case RenderGraphNodeKind::ColorConvert:
                    case RenderGraphNodeKind::TrackMatte:
                    case RenderGraphNodeKind::Transition:
                        return DirtyFallbackReason::EffectBoundsUnknown;
                    case RenderGraphNodeKind::Source:
                    case RenderGraphNodeKind::Precomp:
                    case RenderGraphNodeKind::Video:
                    case RenderGraphNodeKind::Output:
                        return DirtyFallbackReason::PredictedBoundsMissing;
                }
                return DirtyFallbackReason::PredictedBoundsMissing;
            }();
            ctx.counters->increment_dirty_full_fallback_reason(reason);
        }
    }

    return clip;
}

// ── Phase E: Execute node (or use cached result) ────────────────────────────
//
// If result is already populated (cache hit), returns 0.0 and leaves result
// unchanged.  Otherwise executes the node, times it, stores the output in the
// node cache (when eligible), and returns the wall-clock duration in ms.
[[nodiscard]] double run_node(
    RenderGraphNode& node,
    RenderGraphContext& node_ctx,
    const std::vector<std::shared_ptr<Framebuffer>>& inputs,
    const std::vector<std::optional<raster::BBox>>& input_bboxes,
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
    }
    const auto exec_t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(exec_t1 - exec_t0).count();
}

// ── Phase F: Emit telemetry records for a node ──────────────────────────────
//
// Writes both CacheTelemetryRecord and NodeTelemetryRecord, then logs a
// one-line summary via spdlog.
void emit_node_records(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const cache::NodeCacheKey& key,
    const std::shared_ptr<Framebuffer>& result,
    const std::string& cache_status,
    bool is_cacheable,
    int input_count,
    double duration_ms
) {
    // Lazy string construction only when caching is enabled and cache exists
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
        if (result) {
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

// ── Phase G: Execute a single node inside the parallel_for loop ─────────────
//
// Orchestrates profiling-context restore, cache evaluation, dirty-rect
// clipping, node execution, telemetry, and state update for one node.
void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index
) {
    // Restore thread-local profiling context (TBB threads don't inherit it)
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
    const auto& pr = level_resolved[level_index];

    // ── Cache evaluation ────────────────────────────────────────────────
    auto cache_eval = evaluate_cache(
        node, ctx,
        pr.input_hash,
        pr.inputs_frame_dependent,
        pr.has_cacheable_inputs,
        id,
        pr.inputs_all_cache_hits
    );

    // ── Predicted bbox ──────────────────────────────────────────────────
    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);

    // ── Dirty-rect clipping ─────────────────────────────────────────────
    RenderGraphContext node_ctx = ctx;
    if (ctx.dirty_rects_enabled) {
        node_ctx.clip_rect = compute_dirty_clip(ctx, node, predicted_bbox);
    } else {
        node_ctx.clip_rect = predicted_bbox;
    }

    // ── Node execution ──────────────────────────────────────────────────
    const double duration_ms = run_node(
        node, node_ctx,
        pr.inputs, pr.input_bboxes,
        cache_eval.use_cache,
        cache_eval.key,
        cache_eval.result,
        ctx
    );

    // ── Telemetry ───────────────────────────────────────────────────────
    emit_node_records(
        ctx, node,
        cache_eval.key,
        cache_eval.result,
        cache_eval.cache_status,
        cache_eval.is_cacheable,
        static_cast<int>(input_ids.size()),
        duration_ms
    );

    // ── Update shared state ─────────────────────────────────────────────
    state.temp[id] = cache_eval.result;
    state.resolved_key_digest[id] = cache_eval.key.digest();
    state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;
    state.resolved_cache_hit[id] = (cache_eval.cache_status == "hit") ? 1 : 0;
    state.resolved_bboxes[id] = predicted_bbox;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
//  GraphExecutor public API
// ══════════════════════════════════════════════════════════════════════════════

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
        const size_t node_count = graph.size();
        ExecutionState state;
        state.temp.resize(node_count);
        state.resolved_key_digest.assign(node_count, 0);
        state.resolved_frame_dependent.assign(node_count, 0);
        state.resolved_cache_hit.assign(node_count, 0);
        state.resolved_bboxes.resize(node_count);

        std::vector<std::atomic_size_t> consumer_remaining(node_count);
        for (size_t i = 0; i < plan.consumer_counts.size(); ++i) {
            consumer_remaining[i].store(plan.consumer_counts[i], std::memory_order_relaxed);
        }

        for (const auto& level : plan.levels) {
            CHRONON_ZONE_C("execute_level", trace_category::kGraph);

            // ── Sequential pre-resolve phase ─────────────────────────────
            std::vector<PreResolvedNode> level_resolved(level.size());
            for (size_t i = 0; i < level.size(); ++i) {
                level_resolved[i] = resolve_inputs(graph, level[i], state);
            }

            // ── Decrement consumer counts & release dead references ──────
            for (size_t i = 0; i < level.size(); ++i) {
                const GraphNodeId id = level[i];
                for (GraphNodeId input_id : graph.inputs(id)) {
                    if (!contains_index(consumer_remaining, input_id)) {
                        continue;
                    }
                    if (consumer_remaining[input_id].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        state.temp[input_id].reset();
                        state.resolved_key_digest[input_id] = 0;
                        state.resolved_frame_dependent[input_id] = 0;
                        state.resolved_cache_hit[input_id] = 0;
                        state.resolved_bboxes[input_id].reset();
                    }
                }
            }

            // ── Parallel execution phase ─────────────────────────────────
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, level.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t level_index = range.begin(); level_index != range.end(); ++level_index) {
                        execute_single_node(state, graph, ctx, level_resolved, level[level_index], level_index);
                    }
                }
            );
        }

        return state.temp[output];
    });
}

} // namespace chronon3d::graph
