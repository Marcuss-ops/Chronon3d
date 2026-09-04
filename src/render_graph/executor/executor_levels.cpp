#include "executor_levels.hpp"

#include "executor_level_dispatch.hpp"
#include "framebuffer_lifetime.hpp"
#include "input_resolver.hpp"
#include "level_timings.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace chronon3d::graph {

void execute_levels(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    ExecutionScheduler& scheduler,
    const std::vector<std::vector<GraphNodeId>>& levels,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::memory_resource* res,
    const CompiledFrameGraph& compiled
) {
    const auto& resource_table = compiled.resource_table();

    for (std::size_t level_index = 0; level_index < levels.size(); ++level_index) {
        const auto& level = levels[level_index];
        CHRONON_TRACE_SCOPE("chronon.graph", "execute_level");

        const auto t_schedule0 = profiling::now();

        std::pmr::vector<PreResolvedNode> level_resolved(res);
        level_resolved.reserve(level.size());

        const auto t_input0 = profiling::now();
        for (size_t i = 0; i < level.size(); ++i) {
            level_resolved.emplace_back(res);
            resolve_inputs(graph, level[i], state, consumer_remaining, level_resolved[i]);
        }
        const auto t_input1 = profiling::now();
        const auto t_schedule1 = profiling::now();

        LevelTimings timings(res);
        timings.resize(level.size());

        dispatch_level_nodes(
            graph, ctx, state, scheduler, level, level_resolved,
            consumer_remaining, parent_counters, parent_pool,
            timings, compiled);

        const auto t_dispatch1 = profiling::now();

        const auto t_fb0 = profiling::now();
        const auto& release_schedule = resource_table.release_schedule(level_index);
        release_consumed_framebuffers(
            state,
            graph,
            level,
            consumer_remaining,
            std::span<const GraphNodeId>(release_schedule));
        const auto t_fb1 = profiling::now();

        if (parent_counters) {
            timings.roll_up(
                *parent_counters,
                profiling::duration_ms(t_schedule1, t_dispatch1),
                profiling::duration_ms(t_input0, t_input1),
                profiling::duration_ms(t_schedule0, t_schedule1),
                profiling::duration_ms(t_fb0, t_fb1)
            );
        }
    }
}

} // namespace chronon3d::graph
