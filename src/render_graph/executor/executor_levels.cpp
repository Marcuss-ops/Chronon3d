#include "executor_levels.hpp"

#include "framebuffer_lifetime.hpp"
#include "input_resolver.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

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

        std::pmr::vector<PreResolvedNode> level_resolved(res);
        level_resolved.reserve(level.size());
        for (size_t i = 0; i < level.size(); ++i) {
            level_resolved.emplace_back(res);
            level_resolved[i] = resolve_inputs(graph, level[i], state, consumer_remaining, res);
        }

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

        release_consumed_framebuffers(state, graph, level, consumer_remaining);
    }
}

} // namespace chronon3d::graph
