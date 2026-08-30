#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_budget.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>

namespace chronon3d {

enum class SceneExecutionMode : std::uint8_t {
    DynamicCallback,
    StaticScene,
    StaticTopologySlots
};

enum class DynamicSlotKind : std::uint8_t {
    Transform, Opacity, Paint, Visibility, Resource, TextRun, EffectParameters
};

struct DynamicSlotDesc {
    std::uint32_t slot_id{0};
    DynamicSlotKind kind{DynamicSlotKind::Transform};
    std::uint32_t offset{0};
    std::uint32_t size{0};
    std::uint32_t owner_instance{0};
};

struct CompiledFrameProgram {
    runtime::CommandPlan command_plan{};
    std::vector<DynamicSlotDesc> slots{};
    std::shared_ptr<const graph::FrameParameterTable> parameters{};
};

/// Immutable runtime artifact retaining the one canonical Composition value.
struct CompiledComposition {
    std::shared_ptr<const Composition> composition{};
    std::shared_ptr<const camera_v1::CameraProgram> camera_program{};
    std::shared_ptr<const assets::PreparedAssetManifest> asset_manifest{};
    render_plan::RenderBudget render_budget{};
    std::uint64_t fingerprint{0};
    std::shared_ptr<const Scene> static_scene{};
    std::shared_ptr<const CompiledFrameProgram> frame_program{};
    SceneExecutionMode execution_mode{SceneExecutionMode::DynamicCallback};
    mutable std::shared_ptr<const Scene> template_scene{};
    bool is_static_topology{false};
};

} // namespace chronon3d
