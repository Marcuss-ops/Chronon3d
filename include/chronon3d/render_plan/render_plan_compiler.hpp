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

/// Backend-neutral settings identity used by the prepared-plan fingerprint.
/// Callers map their effective SDK/backend settings into this value at the
/// canonical compile boundary; paths and diagnostic destinations are omitted.
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

/// Stable inputs that define a prepared render-plan fingerprint.  The
/// defaults are compatibility identifiers, not build timestamps or machine
/// paths, so the same logical plan remains reproducible across hosts.
struct RenderPlanFingerprintOptions {
    std::string schema_version{"chronon.render-plan.v1"};
    std::string engine_compatibility_version{"chronon3d.engine.v1"};
    RenderPlanFingerprintSettings render_settings{};
};

/// Immutable output of render-plan preparation.
///
/// `compiled_composition` is the canonical execution value.  The
/// `composition` member is a temporary source-compatibility view for the
/// legacy CLI/runtime boundary; its scene callback delegates back to this
/// immutable compiled value and has no independent compilation path.
struct PreparedRenderPlan {
    CompiledComposition compiled_composition;

    // Transitional adapter.  New consumers must use compiled_composition.
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
