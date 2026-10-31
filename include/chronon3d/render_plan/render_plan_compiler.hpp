#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/camera_sample_provider.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_fingerprint.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

// RenderJobFingerprint lives in render_plan_fingerprint.hpp: it is the single
// job-identity type shared by this path and the canonical compiler (audit
// FP-1), not a second definition local to the plan compiler.

struct PreparedRenderPlan {
    CompiledComposition compiled_composition;
    std::shared_ptr<const Composition> composition;
    chronon3d::assets::PreparedAssetManifest assets;
    chronon3d::assets::PreparedAssetStore resources;
    RenderJobFingerprint fingerprint;
    std::string job_id;
    CanvasSpec canvas;
    OutputSpec output;
    RenderBudget render_budget{};
};

Result<PreparedRenderPlan, PlanDecodeError>
compile_render_plan(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options = {},
    const compiler::CompiledProgram* canonical_program = nullptr,
    const RuntimeCameraSampleProvider* runtime_camera = nullptr);

}  // namespace chronon3d::render_plan
