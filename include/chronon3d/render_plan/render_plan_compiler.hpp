#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

struct RenderJobFingerprint {
    chronon3d::assets::ContentDigest content_digest{};
    chronon3d::assets::ContentDigest request_digest{};
};

struct RenderPlanFingerprintSettings {
    int width{1920};
    int height{1080};
    int antialiasing_samples{1};
    float ssaa_factor{1.0f};
    bool motion_blur{false};
    bool dirty_rects{false};
    bool deterministic{false};
    bool force_scalar_normal_blend{true};
    bool dirty_bitmask{true};
    bool dirty_tiles{true};
    bool parallel_tiles{true};
    int tile_size{0};
    double tile_dirty_ratio_threshold{0.30};
    bool optimize_compositing{true};
};

struct RenderPlanFingerprintOptions {
    std::string schema_version{"chronon.render-plan.v2"};
    std::string engine_compatibility_version{"chronon3d.engine.v1"};
    RenderPlanFingerprintSettings render_settings{};
};

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
    const RenderPlanFingerprintOptions& fingerprint_options = {});

}  // namespace chronon3d::render_plan
