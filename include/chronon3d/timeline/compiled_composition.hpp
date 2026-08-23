#pragma once

// ============================================================================
// include/chronon3d/timeline/compiled_composition.hpp
//
// P3-C (V0.2 timeline) — `CompiledComposition` is the immutable
// handle for a ready-to-evaluate composition: the static recipe
// (`CompositionDefinition`) PLUS its compiled `camera_v1::CameraProgram`,
// keyed by a stable `fingerprint`.
// ============================================================================

#include <cstdint>
#include <memory>
#include <vector>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_budget.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>

namespace chronon3d {

/// Canonical execution mode for a compiled composition.
enum class SceneExecutionMode : std::uint8_t {
    DynamicCallback,       ///< Fallback: re-execute authored Scene callback per frame (always correct)
    StaticScene,           ///< Fast path: entire scene is static / frame-invariant (zero rebuild)
    StaticTopologySlots    ///< Target: static topology with dynamic per-frame POD parameter slots
};

/// Categories of per-instance dynamic property slots.
enum class DynamicSlotKind : std::uint8_t {
    Transform,
    Opacity,
    Paint,
    Visibility,
    Resource,
    TextRun,
    EffectParameters
};

/// Descriptor for a single dynamic property slot inside the frame parameter table.
struct DynamicSlotDesc {
    std::uint32_t slot_id{0};
    DynamicSlotKind kind{DynamicSlotKind::Transform};

    std::uint32_t offset{0};
    std::uint32_t size{0};

    std::uint32_t owner_instance{0};
};

/// Backend-neutral compiled frame program with static command plan and dynamic parameter table.
struct CompiledFrameProgram {
    runtime::CommandPlan command_plan{};
    std::vector<DynamicSlotDesc> slots{};
    std::shared_ptr<const graph::FrameParameterTable> parameters{};
};

/// Immutable evaluate-ready handle shared by all render execution paths.
struct CompiledComposition {
    std::shared_ptr<const CompositionDefinition> definition{};
    std::shared_ptr<const camera_v1::CameraProgram> camera_program{};

    // Optional immutable asset identity attached by PreparedRenderPlan.
    std::shared_ptr<const assets::PreparedAssetManifest> asset_manifest{};

    // The resource policy is part of the immutable execution value, so every
    // consumer (SDK, C ABI, CLI, and file rendering) observes the same budget.
    render_plan::RenderBudget render_budget{};
    std::uint64_t fingerprint{0};

    // Fast-path static scene materialized once at prepare time when
    // execution_mode == SceneExecutionMode::StaticScene.
    std::shared_ptr<const Scene> static_scene{};

    // Compiled frame program containing static command plan and dynamic parameter slots.
    std::shared_ptr<const CompiledFrameProgram> frame_program{};

    SceneExecutionMode execution_mode{SceneExecutionMode::DynamicCallback};

    // Transition compatibility alias for template_scene
    mutable std::shared_ptr<const Scene> template_scene{};
    bool is_static_topology{false};
};

} // namespace chronon3d
