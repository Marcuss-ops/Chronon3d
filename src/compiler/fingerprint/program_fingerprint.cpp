#include "../compiler_stages.hpp"

#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

#include <string>
#include <string_view>

namespace chronon3d::compiler::stages {

namespace {

std::uint64_t render_settings_fingerprint(
    const render_plan::RenderPlanFingerprintSettings& settings) {
    return chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-settings.fingerprint.v1")
        .add(settings.width)
        .add(settings.height)
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

std::uint64_t fingerprint(const render_plan::RenderPlan& plan, const CompiledProgramData& program) {
    std::uint64_t value = plan.content_fingerprint;
    value ^= static_cast<std::uint64_t>(program.nodes.size()) * 0x9e3779b97f4a7c15ULL;
    value ^= static_cast<std::uint64_t>(program.animations.size()) +
             (static_cast<std::uint64_t>(program.passes.size()) << 32U);
    value ^= static_cast<std::uint64_t>(program.timelines.size()) << 7U;
    value ^= static_cast<std::uint64_t>(program.animation_tracks.size()) << 13U;
    value ^= static_cast<std::uint64_t>(program.curve_keyframes.size()) << 19U;
    value ^= static_cast<std::uint64_t>(program.texts.size()) << 23U;
    value ^= static_cast<std::uint64_t>(program.text_values.size()) << 29U;
    return value;
}

ProgramDigests compute_digests(
    const render_plan::RenderPlan& plan,
    const assets::PreparedAssetManifest& assets,
    const render_plan::RenderPlanFingerprintOptions& options) {
    auto content_plan = plan;
    content_plan.job_id.clear();
    content_plan.output = {};
    auto request_plan = plan;
    request_plan.job_id.clear();
    request_plan.output.path.clear();

    const auto content_hash = render_plan::compute_render_plan_content_fingerprint(content_plan);
    const auto request_hash = render_plan::compute_render_plan_content_fingerprint(request_plan);
    const auto settings_hash = render_settings_fingerprint(options.render_settings);
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

}  // namespace chronon3d::compiler::stages
