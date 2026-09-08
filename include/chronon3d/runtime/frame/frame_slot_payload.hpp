#pragma once

// Temporary compatibility payload for the prepared-job pipeline.
//
// This is the single indexed payload authority for a frame slot while the
// evaluated-program representation is being lowered.  The composition-shaped
// value is intentionally kept behind this frame boundary; Wave 8 will replace
// it with the plain-data evaluated program state.

#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/render_graph/executor/execution_workspace.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <memory>
#include <optional>

namespace chronon3d::runtime {

struct FrameSlotPayload final {
    FrameArena arena{4u * 1024u * 1024u, true};
    // Typed compiler output is frame-slot owned.  This is the handoff point
    // for TypedProgramSampler -> GraphExecutor; it must not live in
    // JobSession or in the legacy EvaluatedCompositionFrame compatibility
    // payload.
    compiler::FrameParameterBlock parameters{};
    // Frame-local evaluated state belongs to the lease.  Keep the scene and
    // camera as separate typed values so the slot does not own the timeline
    // compatibility wrapper (`EvaluatedCompositionFrame`).
    std::optional<Scene> sampled_scene{};
    std::optional<Camera2_5D> sampled_camera{};
    std::shared_ptr<Framebuffer> rendered{};
    // Per-slot execution scratch and memory telemetry. These resources are
    // leased with the slot, so concurrent frames never contend on a
    // JobSession-owned workspace/tracker.
    graph::ExecutionWorkspaceRing execution_workspaces{};
    graph::NodeMemoryTracker memory_tracker{};
    std::optional<graph::NodeExecutionError> frame_error{};

    void reset() noexcept {
        parameters.layers.clear();
        sampled_scene.reset();
        sampled_camera.reset();
        rendered.reset();
        memory_tracker.reset();
        frame_error.reset();
        arena.reset();
    }
};

}  // namespace chronon3d::runtime
