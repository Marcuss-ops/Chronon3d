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

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_budget.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>

namespace chronon3d {

/// Immutable evaluate-ready handle shared by all render execution paths.
struct CompiledComposition {
    std::shared_ptr<const CompositionDefinition> definition{};
    std::shared_ptr<const camera_v1::CameraProgram> camera_program{};

    // Optional immutable asset identity attached by PreparedRenderPlan.
    // Generic compile_composition() callers have no manifest and retain the
    // legacy resolver contract; prepared-plan consumers get render-boundary
    // integrity verification without a second compilation path.
    std::shared_ptr<const assets::PreparedAssetManifest> asset_manifest{};

    // The resource policy is part of the immutable execution value, so every
    // consumer (SDK, C ABI, CLI, and file rendering) observes the same budget.
    render_plan::RenderBudget render_budget{};
    std::uint64_t fingerprint{0};
};

} // namespace chronon3d
