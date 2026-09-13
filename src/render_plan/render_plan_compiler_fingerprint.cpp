#include <chronon3d/render_plan/render_plan_fingerprint.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

#include <string>
#include <string_view>

namespace chronon3d::render_plan {
namespace {

// Render settings participate in BOTH digests, so they are hashed once here.
// The field order is part of the fingerprint contract: reordering it changes
// every published digest, which is why this sequence is written once, in the
// one authority, instead of being mirrored per consumer (audit FP-1).
//
// A zero dimension means "unset": identity then follows the canvas the job is
// actually rendered at. That defaulting rule belongs to the identity itself,
// not to a caller — keeping it here is what stops the plan path and the
// compiler path from disagreeing about the same job (audit FP-1).
std::uint64_t render_settings_fingerprint(
    const RenderPlanFingerprintSettings& settings, const RenderPlan& plan) {
    const int width = settings.width == 0 ? plan.canvas.width : settings.width;
    const int height = settings.height == 0 ? plan.canvas.height : settings.height;
    return chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-settings.fingerprint.v1")
        .add(width)
        .add(height)
        .add(settings.antialiasing_samples)
        .add(settings.ssaa_factor)
        .add(settings.motion_blur)
        .add(settings.dirty_rects)
        .add(settings.deterministic)
        .add(settings.force_scalar_normal_blend)
        .add(settings.dirty_bitmask)
        .add(settings.dirty_tiles)
        .add(settings.parallel_tiles)
        .add(settings.tile_size)
        .add(settings.tile_dirty_ratio_threshold)
        .add(settings.optimize_compositing)
        .finish();
}

}  // namespace

RenderJobFingerprint compute_render_job_fingerprint(
    const RenderPlan& plan,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const RenderPlanFingerprintOptions& options) {
    // job_id is a label, not content: the content digest must not move when a
    // caller renames the same job. The request digest additionally ignores the
    // output path (where the bytes land) but keeps format/codec/rate control
    // (what the bytes are).
    auto content_plan = plan;
    content_plan.job_id.clear();
    content_plan.output = {};
    auto request_plan = plan;
    request_plan.job_id.clear();
    request_plan.output.path.clear();

    const auto content_hash = compute_render_plan_content_fingerprint(content_plan);
    const auto request_hash = compute_render_plan_content_fingerprint(request_plan);
    const auto settings_hash =
        render_settings_fingerprint(options.render_settings, plan);
    const auto material = [&](std::string_view domain, std::uint64_t plan_hash,
                              bool include_output_settings) {
        auto hash = chronon3d::core::hash::HashBuilder{}
            .add(domain)
            .add(options.schema_version)
            .add(options.engine_compatibility_version)
            .add(plan_hash)
            .add(settings_hash)
            .add_bytes(assets.manifest_digest().bytes.data(),
                       assets.manifest_digest().bytes.size());
        if (include_output_settings) {
            hash.add_enum(request_plan.output.format)
                .add_enum(request_plan.output.codec)
                .add(static_cast<int>(request_plan.output.rate_control))
                .add(request_plan.output.bitrate)
                .add(request_plan.output.crf)
                .add(request_plan.output.qp)
                .add(request_plan.output.profile_id);
        }
        return std::to_string(hash.finish());
    };
    return {
        chronon3d::assets::sha256_string(
            material("chronon.render-content.v3", content_hash, false)),
        chronon3d::assets::sha256_string(
            material("chronon.render-request.v3", request_hash, true))};
}

}  // namespace chronon3d::render_plan
