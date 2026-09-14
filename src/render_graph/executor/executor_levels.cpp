#include "executor_levels.hpp"

#include "executor_level_dispatch.hpp"
#include "framebuffer_lifetime.hpp"
#include "input_resolver.hpp"
#include "level_timings.hpp"
#include <spdlog/spdlog.h>
#include <string>
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
    // PreResolvedNode owns only level-local scratch. A pool resource backed
    // by the frame arena returns each level's vectors to reusable bins, so a
    // deep graph consumes roughly the largest level's scratch instead of the
    // sum of every level's allocations.
    std::pmr::unsynchronized_pool_resource level_scratch{res};

    for (std::size_t level_index = 0; level_index < levels.size(); ++level_index) {
        const auto& level = levels[level_index];
        CHRONON_TRACE_SCOPE("chronon.graph", "execute_level");

        const auto t_schedule0 = profiling::now();

        std::pmr::vector<PreResolvedNode> level_resolved(&level_scratch);
        level_resolved.reserve(level.size());

        const auto t_input0 = profiling::now();
        for (size_t i = 0; i < level.size(); ++i) {
            level_resolved.emplace_back(&level_scratch);
            resolve_inputs(graph, level[i], state, consumer_remaining, level_resolved[i]);
        }
        // An input whose producer temp is already gone is a scheduling defect,
        // not an optional input: the node then reads a null framebuffer and
        // silently degenerates (a Composite with a null top is a pass-through,
        // which is how a whole layer disappeared from a finished clip while
        // every node still reported success). Bounded to the first frames, as
        // the sibling [exec-*-diag] lines are, so the hot loop stays clean.
        if (ctx.frame_input.frame <= 2) {
            for (size_t i = 0; i < level.size(); ++i) {
                const auto& input_ids = graph.inputs(level[i]);
                for (size_t j = 0; j < input_ids.size(); ++j) {
                    std::size_t producer_level = levels.size();
                    for (std::size_t pl = 0; pl < levels.size(); ++pl) {
                        if (std::find(levels[pl].begin(), levels[pl].end(),
                                      input_ids[j]) != levels[pl].end()) {
                            producer_level = pl;
                            break;
                        }
                    }
                    if (level_resolved[i].inputs[j]) {
                        spdlog::info(
                            "[graph-edge] frame={} level={} consumer_id={} "
                            "input_index={} producer_id={} producer_level={}",
                            static_cast<int>(ctx.frame_input.frame), level_index,
                            level[i], j, input_ids[j], producer_level);
                        continue;
                    }
                    spdlog::warn(
                        "[input-unresolved] frame={} level={} consumer_id={} "
                        "input_index={} producer_id={} producer_level={}",
                        static_cast<int>(ctx.frame_input.frame), level_index,
                        level[i], j, input_ids[j], producer_level);
                }
            }
        }
        const auto t_input1 = profiling::now();
        const auto t_schedule1 = profiling::now();

        dispatch_level_nodes(
            graph, ctx, state, scheduler, level, level_resolved,
            consumer_remaining, parent_counters, parent_pool,
            compiled);

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

        // Which resource temps are still alive after this level, and which
        // ones this level's schedule retired. A producer that disappears
        // before its consumer runs is a lifetime defect; the output alone
        // cannot show it (the consumer just reads a null framebuffer).
        if (ctx.frame_input.frame <= 2) {
            std::string live;
            for (std::size_t id = 0; id < state.temp.size(); ++id) {
                if (state.temp[id]) {
                    live += std::to_string(id);
                    live += ",";
                }
            }
            std::string retired;
            for (const GraphNodeId id : release_schedule) {
                retired += std::to_string(id);
                retired += ",";
            }
            spdlog::info(
                "[temp-lifetime] frame={} after_level={} live=[{}] retired=[{}]",
                static_cast<int>(ctx.frame_input.frame), level_index, live,
                retired);
        }

        const double dur_dispatch = profiling::duration_ms(t_schedule1, t_dispatch1);
        const double dur_input = profiling::duration_ms(t_input0, t_input1);
        const double dur_schedule = profiling::duration_ms(t_schedule0, t_schedule1);
        const double dur_fb = profiling::duration_ms(t_fb0, t_fb1);

        if (ctx.frame_input.frame <= 2) {
            spdlog::info("[exec-level-diag] frame={} level={} nodes={} input_ms={:.3f} dispatch_ms={:.3f} fb_release_ms={:.3f}",
                         static_cast<int>(ctx.frame_input.frame), level_index, level.size(),
                         dur_input, dur_dispatch, dur_fb);
        }

        if (parent_counters) {
            roll_up_level_durations(
                *parent_counters,
                dur_dispatch,
                dur_input,
                dur_schedule,
                dur_fb
            );
        }
    }
}

} // namespace chronon3d::graph
