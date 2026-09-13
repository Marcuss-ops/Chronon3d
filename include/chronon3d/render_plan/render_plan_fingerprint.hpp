#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <string>

namespace chronon3d::render_plan {

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

/// Identity of a render job. `content_digest` names what is rendered;
/// `request_digest` is the content identity plus the output/codec request.
///
/// This type is the single vocabulary for job identity: it is what a program
/// cache, a render cache and a certification receipt must all key on.
struct RenderJobFingerprint {
    chronon3d::assets::ContentDigest content_digest{};
    chronon3d::assets::ContentDigest request_digest{};
};

/// THE canonical job-identity authority (audit FP-1).
///
/// The render-plan path (`compile_render_plan`) and the compiler path
/// (`compiler::compile`) both call this function. There is deliberately no
/// second implementation: when two producers computed these digests
/// independently, a program/binary cache keyed on them could see two different
/// values for the same plan + assets + options — a wrong cache hit, i.e. a
/// correctness bug, not a performance one.
///
/// This is one of the two job-identity inputs (the other is
/// `compute_render_plan_content_fingerprint`, the pre-fingerprint plan hash).
[[nodiscard]] RenderJobFingerprint compute_render_job_fingerprint(
    const RenderPlan& plan,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const RenderPlanFingerprintOptions& options);

}  // namespace chronon3d::render_plan
