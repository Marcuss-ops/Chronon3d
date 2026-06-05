#include "executor_levels.hpp"

#include "framebuffer_lifetime.hpp"
#include "input_resolver.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <chrono>
#include <cmath>
#include <tbb/tbb.h>

namespace chronon3d::graph {

void execute_levels(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    const std::vector<std::vector<GraphNodeId>>& levels,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::memory_resource* res
) {
    for (const auto& level : levels) {
        CHRONON_ZONE_C("execute_level", trace_category::kGraph);

        const auto t_schedule0 = std::chrono::steady_clock::now();

        std::pmr::vector<PreResolvedNode> level_resolved(res);
        level_resolved.reserve(level.size());

        const auto t_input0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < level.size(); ++i) {
            level_resolved.emplace_back(res);
            level_resolved[i] = resolve_inputs(graph, level[i], state, consumer_remaining, res);
        }
        const auto t_input1 = std::chrono::steady_clock::now();

        const auto t_schedule1 = std::chrono::steady_clock::now();

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, level.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t level_index = range.begin(); level_index != range.end(); ++level_index) {
                    execute_single_node(
                        state,
                        graph,
                        ctx,
                        level_resolved,
                        level[level_index],
                        level_index,
                        parent_counters,
                        parent_pool,
                        consumer_remaining
                    );
                }
            }
        );

        const auto t_dispatch1 = std::chrono::steady_clock::now();

        const auto t_fb0 = std::chrono::steady_clock::now();
        release_consumed_framebuffers(state, graph, level, consumer_remaining);
        const auto t_fb1 = std::chrono::steady_clock::now();

        if (parent_counters) {
            parent_counters->input_resolve_ms.fetch_add(
                static_cast<uint64_t>(std::llround(std::chrono::duration<double, std::milli>(t_input1 - t_input0).count())),
                std::memory_order_relaxed);
            parent_counters->node_schedule_ms.fetch_add(
                static_cast<uint64_t>(std::llround(std::chrono::duration<double, std::milli>(t_schedule1 - t_schedule0).count())),
                std::memory_order_relaxed);
            parent_counters->node_dispatch_ms.fetch_add(
                static_cast<uint64_t>(std::llround(std::chrono::duration<double, std::milli>(t_dispatch1 - t_schedule1).count())),
                std::memory_order_relaxed);
            parent_counters->framebuffer_lifetime_ms.fetch_add(
                static_cast<uint64_t>(std::llround(std::chrono::duration<double, std::milli>(t_fb1 - t_fb0).count())),
                std::memory_order_relaxed);
        }
    }
}

} // namespace chronon3d::graph
