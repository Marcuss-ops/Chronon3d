#include "executor_internal.hpp"

#include "executor_compiled_program.hpp"
#include "executor_levels.hpp"
#include "framebuffer_lifetime.hpp"

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/runtime/frame/frame_error_slot.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> execute_internal(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    runtime::JobSession& session,
    FrameArena& arena,
    ExecutionScheduler& scheduler)
{
    auto& graph = compiled.graph;
    const auto& levels = compiled.program.empty()
        ? compiled.levels
        : compiled.program.levels;
    const auto output = compiled.output;

    if (compiled.empty()) {
        session.publish_last_frame_error(NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            "frame_graph",
            "compiled frame graph is empty"
        });
        return nullptr;
    }

    // Slot-owned resources are the canonical frame execution owner. Standalone
    // graph diagnostics have no lease, so they use short-lived local storage.
    ExecutionWorkspaceRing standalone_workspaces;
    NodeMemoryTracker standalone_memory;
    auto* payload = ctx.execution ? ctx.execution->payload() : nullptr;
    auto& workspaces = payload ? payload->execution_workspaces
                               : standalone_workspaces;
    auto* memory_tracker = payload ? &payload->memory_tracker : &standalone_memory;
    auto workspace_lease = workspaces.acquire(
        static_cast<std::uint64_t>(std::max<std::int64_t>(0, ctx.frame_input.frame.integral())));
    auto& workspace = workspace_lease.workspace();
    auto* res = arena.resource();
    struct ArenaGuard {
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{arena};

    const size_t node_count = graph.size();
    ExecutionState state(workspace, memory_tracker);
    state.temp.resize(node_count);
    state.resolved_key_digest.assign(node_count, 0);
    state.resolved_frame_dependent.assign(node_count, 0);
    state.resolved_cache_hit.assign(node_count, 0);
    state.resolved_bboxes.resize(node_count);
    state.static_bake_member_skip.assign(node_count, 0);
    state.static_bake_root_index.assign(node_count, UINT32_MAX);
    std::vector<std::uint32_t> bake_membership(node_count, 0);
    std::vector<std::uint32_t> ready_membership(node_count, 0);
    for (std::uint32_t i = 0; i < compiled.program.static_bakes.size(); ++i) {
        const auto& bake = compiled.program.static_bakes[i];
        if (bake.root_node < node_count) state.static_bake_root_index[bake.root_node] = i;
        bool ready = false;
        if (bake.runtime) {
            std::lock_guard lock(bake.runtime->mutex);
            ready = static_cast<bool>(bake.runtime->baked_surface);
        }
        for (const auto member : bake.members) {
            if (member >= node_count) continue;
            ++bake_membership[member];
            if (ready) ++ready_membership[member];
        }
    }
    for (std::size_t i = 0; i < node_count; ++i) {
        state.static_bake_member_skip[i] = bake_membership[i] != 0 &&
            bake_membership[i] == ready_membership[i];
    }

    const auto& resource_table = compiled.resource_table();
    state.physical_slots.resize(resource_table.physical_slot_count);
    for (auto& slot : state.physical_slots) {
        slot.reset();
    }

    // Z3 — bind the terminal graph allocation before any node executes.  A
    // late handle replacement would leave the renderer's original allocation
    // intact and merely move a descriptor; it cannot eliminate the handoff
    // copy.  Persistent graph outputs do not normally receive a transient
    // physical slot, so reserve one executor-owned slot for this native
    // encode frame when needed.
    OwnedFB* terminal_encode_slot = nullptr;
    if (ctx.policy.native_video_encode_surface !=
        runtime::kInvalidRenderSurfaceHandle) {
        // Prefer the compiled output resource's physical allocation. This is
        // the actual executor allocation point; the fallback slot is only for
        // persistent outputs that deliberately have no transient allocation.
        if (const auto* output_resource = resource_table.resource_for(output);
            output_resource &&
            output_resource->physical_slot != kInvalidPhysicalAllocationId &&
            output_resource->physical_slot < state.physical_slots.size()) {
            terminal_encode_slot =
                &state.physical_slots[output_resource->physical_slot];
        } else {
            // REQUIRE_GPU_NATIVE owns the terminal image directly.  Do not
            // allocate a CPU shadow framebuffer for every frame: the encoder
            // surface is already the authoritative destination and the
            // native nodes write it through Vulkan.
            state.physical_slots.emplace_back(
                new Framebuffer(ctx.frame_input.width, ctx.frame_input.height,
                                static_cast<Color*>(nullptr)),
                PoolFbDeleter(DeleteFramebuffer{}));
            terminal_encode_slot = &state.physical_slots.back();
        }
        auto& slot = *terminal_encode_slot;
        if (!slot) {
            slot = OwnedFB(
                new Framebuffer(ctx.frame_input.width, ctx.frame_input.height,
                                static_cast<Color*>(nullptr)),
                PoolFbDeleter(DeleteFramebuffer{}));
        }
        slot->set_surface_handle(ctx.policy.native_video_encode_surface);
        slot->mark_gpu_authoritative();
    }
    ctx.node_exec.terminal_video_encode_physical_slot = terminal_encode_slot;
    if (terminal_encode_slot) {
        // The physical slot is now the source of truth.  Leave the external
        // handle fallback unset so acquire_* cannot create a second wrapper.
        ctx.node_exec.terminal_video_encode_surface =
            runtime::kInvalidRenderSurfaceHandle;
    }

    const auto t_fb0 = profiling::now();
    init_shared_transparent_fb(state, ctx, res);

    auto consumer_remaining = init_consumer_remaining(
        node_count, compiled.resource_table().resources, res);
    const auto t_fb1 = profiling::now();
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->framebuffer_lifetime_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::duration_ms(t_fb0, t_fb1))),
            std::memory_order_relaxed);
    }

    auto* parent_counters = ctx.node_exec.counters;
    auto parent_pool_owner = ctx.execution_framebuffer_pool();
    auto* parent_pool = parent_pool_owner.get();

    if (parent_counters) {
        const auto logical_count = static_cast<std::uint64_t>(
            resource_table.logical_resource_count);
        const auto physical_count = static_cast<std::uint64_t>(
            resource_table.physical_slot_count);
        const auto logical_bytes = static_cast<std::uint64_t>(
            resource_table.logical_bytes);
        const auto physical_bytes = static_cast<std::uint64_t>(
            resource_table.planned_physical_bytes);

        parent_counters->logical_resource_count.store(
            logical_count, std::memory_order_relaxed);
        parent_counters->physical_resource_slot_count.store(
            physical_count, std::memory_order_relaxed);
        parent_counters->logical_resource_bytes.store(
            logical_bytes, std::memory_order_relaxed);
        parent_counters->physical_resource_bytes.store(
            physical_bytes, std::memory_order_relaxed);
        parent_counters->alias_saved_bytes.store(
            logical_bytes > physical_bytes ? logical_bytes - physical_bytes : 0,
            std::memory_order_relaxed);
        parent_counters->alias_reuse_count.store(
            logical_count > physical_count ? logical_count - physical_count : 0,
            std::memory_order_relaxed);
        parent_counters->new_resource_slot_count.store(
            physical_count, std::memory_order_relaxed);
        parent_counters->arena_peak_bytes.store(
            static_cast<std::uint64_t>(resource_table.peak_live_bytes),
            std::memory_order_relaxed);
    }

    // The frame-scoped error channel synchronizes first-error publication
    // across parallel graph workers. Clones share this slot; its lifetime is
    // bounded by the payload lease or this synchronous executor call.
    runtime::RenderErrorSlot frame_error_storage;
    auto* frame_error = payload ? &payload->frame_error : &frame_error_storage;
    frame_error->clear();
    ctx.frame_error = frame_error;

    if (compiled.program.fully_recorded) {
        if (!execute_compiled_program(
                compiled.program, ctx, state, parent_counters) &&
            !frame_error->has_error()) {
            frame_error->publish_first(NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                "compiled-executor",
                "compiled program execution failed"});
        }
    } else {
        execute_levels(graph, ctx, state, scheduler, levels, consumer_remaining,
                       parent_counters, parent_pool, res, compiled);
    }

    if (parent_pool && state.node_memory_tracker) {
        const auto pool = parent_pool->stats();
        state.node_memory_tracker->record_pool(NodeMemoryPoolSnapshot{
            .current_bytes = pool.current_bytes,
            .retained_bytes = pool.retained_bytes,
            .peak_retained_bytes = pool.peak_retained_bytes,
            .total_allocations = pool.total_allocations,
            .total_reuses = pool.total_reuses,
            .total_returns = pool.total_returns,
            .evicted_bytes = pool.evicted_bytes,
        });
    }

    // Promote the completed frame report to the job aggregate before the slot
    // is recycled. This keeps job telemetry available without making the job
    // own frame scratch or frame-local counters.
    if (state.node_memory_tracker) {
        session.memory_aggregate().merge_report(state.node_memory_tracker->snapshot());
    }

    if (ctx.frame_error) {
        if (const auto err = ctx.frame_error->load()) {
            session.publish_last_frame_error(*err);
            spdlog::error(
                "[executor] frame {} failed: node '{}' error [{}] {}",
                static_cast<int>(ctx.frame_input.frame),
                err->node_name,
                render_backend_error_code_name(err->backend_code),
                err->message);
            return nullptr;
        }
    }

    CachedFB result = state.temp[output];
    if (terminal_encode_slot && result &&
        result.get() == terminal_encode_slot->get()) {
        // The workspace owns physical slots only for the duration of graph
        // execution.  Transfer the completed terminal wrapper to the return
        // value so it remains valid after the workspace lease is recycled.
        state.temp[output].reset();
        auto owned = std::move(*terminal_encode_slot);
        return promote_to_cached(std::move(owned));
    }
    return result;
}

} // namespace chronon3d::graph
