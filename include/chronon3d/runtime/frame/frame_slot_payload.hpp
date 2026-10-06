#pragma once

// Single indexed payload authority for a frame slot. All mutable frame
// execution state is leased here; authoring/evaluated composition objects do
// not cross this boundary.

#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/render_graph/executor/execution_workspace.hpp>
#include <chronon3d/runtime/frame/frame_error_slot.hpp>

#include <memory>
#include <optional>

namespace chronon3d::runtime {

struct FrameSlotPayload final {
    explicit FrameSlotPayload(
        std::size_t arena_capacity_bytes = 4u * 1024u * 1024u)
        : arena(arena_capacity_bytes, true) {}

    FrameArena arena;
    // Typed compiler output is frame-slot owned.  This is the handoff point
    // for TypedProgramSampler -> GraphExecutor; it must not live in
    // JobSession or an authoring compatibility payload.
    compiler::FrameParameterBlock parameters{};
    std::shared_ptr<Framebuffer> rendered{};
    // Per-slot execution scratch and memory telemetry. These resources are
    // leased with the slot, so concurrent frames never contend on a
    // JobSession-owned workspace/tracker.
    graph::ExecutionWorkspaceRing execution_workspaces{};
    graph::NodeMemoryTracker memory_tracker{};
    RenderErrorSlot frame_error{};

    void reset() noexcept {
        parameters.layers.clear();
        rendered.reset();
        memory_tracker.reset();
        frame_error.clear();
        arena.reset();
    }
};

}  // namespace chronon3d::runtime
