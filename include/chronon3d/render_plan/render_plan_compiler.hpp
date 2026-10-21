#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_fingerprint.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <functional>
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

/// Optional runtime-only source of fully resolved camera values. `sample`
/// receives an absolute fractional presentation frame and must return the
/// complete camera state for that instant. Its stable identity participates in
/// the render-job fingerprint; the callback itself is never serialized.
struct RuntimeCameraSampleProvider {
    std::string fingerprint;
    std::function<CameraPlan(double)> sample;
};

Result<PreparedRenderPlan, PlanDecodeError>
compile_render_plan(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options = {},
    const compiler::CompiledProgram* canonical_program = nullptr,
    const RuntimeCameraSampleProvider* runtime_camera = nullptr);

/// Build the temporary video compatibility view from an already canonical
/// CompiledProgram. This overload performs asset/materialization preparation
/// needed by the legacy video executor, but deliberately does not invoke
/// compile_composition or any second compiler.
Result<CompiledComposition, PlanDecodeError>
make_video_compatibility_view(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const compiler::CompiledProgram& program,
    const RenderPlanFingerprintOptions& fingerprint_options = {});

}  // namespace chronon3d::render_plan
