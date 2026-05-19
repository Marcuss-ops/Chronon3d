#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/counters.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <iostream>
#include <future>
#include <mutex>
#include <chrono>

namespace chronon3d::graph {

GraphExecutor::GraphExecutor() {}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx
) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_temp.clear();
        m_resolved_key_digest.clear();
        m_resolved_frame_dependent.clear();
        m_pending.clear();
    }
    return execute_node(graph, output, ctx);
}

std::shared_ptr<Framebuffer> GraphExecutor::execute_node(
    RenderGraph& graph,
    GraphNodeId id,
    RenderGraphContext& ctx
) {
    std::shared_ptr<std::promise<std::shared_ptr<Framebuffer>>> promise;
    bool already_executing = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_temp.find(id); it != m_temp.end()) {
            return it->second;
        }
        
        if (auto it = m_pending.find(id); it != m_pending.end()) {
            promise = it->second;
            already_executing = true;
        } else {
            promise = std::make_shared<std::promise<std::shared_ptr<Framebuffer>>>();
            m_pending[id] = promise;
        }
    }

    if (already_executing) {
        return promise->get_future().get();
    }

    try {
        if (id >= graph.size()) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pending.erase(id);
            }
            promise->set_value(nullptr);
            return nullptr;
        }

        auto& node = graph.node(id);
        const auto& input_ids = graph.inputs(id);
        std::vector<std::shared_ptr<Framebuffer>> inputs(input_ids.size());

        if (!input_ids.empty()) {
            std::vector<std::future<void>> futures;
            futures.reserve(input_ids.size());
            for (size_t i = 0; i < input_ids.size(); ++i) {
                futures.emplace_back(std::async(std::launch::async, [&, i, trace = profiling::g_current_trace, frame = profiling::g_current_frame, counters = profiling::g_current_counters] {
                    profiling::g_current_trace = trace;
                    profiling::g_current_frame = frame;
                    profiling::g_current_counters = counters;
                    inputs[i] = execute_node(graph, input_ids[i], ctx);
                }));
            }
            for (auto& f : futures) {
                f.get();
            }
        }

        std::shared_ptr<Framebuffer> result;
        const bool is_cacheable = node.cacheable();
        cache::NodeCacheKey key;
        std::string cache_status;

        // ── Determine frame dependency ──────────────────────────────────────
        bool inputs_frame_dependent = false;
        bool has_cacheable_inputs = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto input_id : input_ids) {
                if (auto it = m_resolved_frame_dependent.find(input_id);
                    it != m_resolved_frame_dependent.end()) {
                    inputs_frame_dependent |= it->second;
                    has_cacheable_inputs = true;
                }
            }
        }

        const bool node_frame_dependent =
            node.frame_dependent() ||
            (has_cacheable_inputs && inputs_frame_dependent) ||
            node.cache_frame_policy() == CacheFramePolicy::FrameDependent;

        // ── Cache key ──────────────────────────────────────────────────────
        if (is_cacheable && ctx.node_cache) {
            key = node.cache_key(ctx);

            if (!node_frame_dependent) {
                key.frame = 0;
            }

            u64 input_hash = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto input_id : input_ids) {
                    input_hash = hash_combine(input_hash, m_resolved_key_digest[input_id]);
                }
            }
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
            cache_status = !ctx.node_cache ? "bypass_no_cache" : "bypass_not_cacheable";
        }

        // ── Execute (or skip if cache hit) ────────────────────────────────────
        const auto exec_t0 = std::chrono::steady_clock::now();

        if (!result) {
            TraceScope scope(ctx.trace, node.name(), "node_execute", ctx.frame);
            result = node.execute(ctx, inputs);
            if (ctx.counters) {
                ctx.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
            }
            if (is_cacheable && ctx.node_cache && result) {
                ctx.node_cache->store(key, result);
            }
        }
        const auto exec_t1 = std::chrono::steady_clock::now();
        const double duration_ms = std::chrono::duration<double, std::milli>(exec_t1 - exec_t0).count();

        // ── Record cache telemetry ─────────────────────────────────────────────
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

        // ── Record node telemetry ─────────────────────────────────────────────
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

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_temp[id] = result;
            m_resolved_key_digest[id] = key.digest();
            if (is_cacheable) {
                m_resolved_frame_dependent[id] = node_frame_dependent;
            }
            m_pending.erase(id);
        }

        promise->set_value(result);
        return result;
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending.erase(id);
        }
        promise->set_exception(std::current_exception());
        throw;
    }
}

} // namespace chronon3d::graph
