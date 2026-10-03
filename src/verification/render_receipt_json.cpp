#include <chronon3d/verification/render_receipt.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace chronon3d::verification {

std::filesystem::path write_render_receipt(const RenderReceipt& receipt,
                                           const std::filesystem::path& output_path) {
    nlohmann::json js;
    js["schema"] = receipt.schema;
    js["render_lane"] = receipt.render_lane;
    js["lane_reason"] = receipt.lane_reason;
    js["identity"] = {
        {"job_id", receipt.job_id},
        {"chronon_version", receipt.chronon_version},
        {"chronon_abi", receipt.chronon_abi},
        {"git_sha", receipt.git_sha},
        {"render_plan_schema", receipt.render_plan_schema},
        {"content_digest", receipt.content_digest},
        {"request_digest", receipt.request_digest},
        {"asset_manifest_digest", receipt.asset_manifest_digest},
    };
    js["render"] = {
        {"backend", receipt.backend},
        {"render_lane", receipt.render_lane},
        {"lane_reason", receipt.lane_reason},
        {"width", receipt.width},
        {"height", receipt.height},
        {"fps_num", receipt.fps_num},
        {"fps_den", receipt.fps_den},
        {"first_frame", receipt.first_frame},
        {"frames", receipt.frames},
        {"execution_downgrade_reason", receipt.execution_downgrade_reason},
        {"execution_downgrade_count", receipt.execution_downgrade_count},
    };
    js["media"] = {
        {"container", receipt.media.container},
        {"codec", receipt.media.codec},
        {"pixel_format", receipt.media.pixel_format},
        {"width", receipt.media.width},
        {"height", receipt.media.height},
        {"fps_num", receipt.media.fps_num},
        {"fps_den", receipt.media.fps_den},
        {"r_fps_num", receipt.media.r_fps_num},
        {"r_fps_den", receipt.media.r_fps_den},
        {"frame_count", receipt.media.frame_count},
        {"duration_ms", receipt.media.duration_ms},
        {"duration_us", static_cast<std::int64_t>(receipt.media.duration_ms * 1000.0)},
        {"has_audio", receipt.media.has_audio},
        {"has_video", receipt.media.has_video},
        {"video_codec", receipt.media.codec},
        {"codec_profile", receipt.media.codec_profile},
        {"video_level", receipt.media.video_level},
        {"video_streams", receipt.media.video_streams},
        {"video_time_base_num", receipt.media.video_time_base_num},
        {"video_time_base_den", receipt.media.video_time_base_den},
        {"sar_num", receipt.media.sar_num},
        {"sar_den", receipt.media.sar_den},
        {"color_range", receipt.media.color_range},
        {"color_space", receipt.media.color_space},
        {"color_transfer", receipt.media.color_transfer},
        {"color_primaries", receipt.media.color_primaries},
        {"hdr_mastering_metadata_present", receipt.media.hdr_mastering_metadata.has_value()},
        {"field_order", receipt.media.field_order},
        {"start_pts", receipt.media.start_pts},
        {"first_frame_keyframe", receipt.media.first_frame_keyframe},
        {"closed_gop", receipt.media.closed_gop},
        {"keyframe_interval", receipt.media.keyframe_interval},
        {"audio_streams", receipt.media.audio_streams},
        {"audio_codec", receipt.media.audio_codec},
        {"audio_profile", receipt.media.audio_profile},
        {"sample_rate", receipt.media.sample_rate},
        {"channels", receipt.media.channels},
        {"channel_layout", receipt.media.channel_layout},
        {"audio_bitrate", receipt.media.audio_bitrate},
        {"audio_time_base_num", receipt.media.audio_time_base_num},
        {"audio_time_base_den", receipt.media.audio_time_base_den},
    };
    const bool verification_ok =
        receipt.verification.ffprobe != "fail" &&
        receipt.verification.decode != "fail" &&
        receipt.verification.frame_count != "fail" &&
        receipt.verification.codec != "fail" &&
        receipt.verification.pixel_format != "fail" &&
        receipt.verification.resolution != "fail" &&
        receipt.verification.fps != "fail" &&
        receipt.verification.audio != "fail" &&
        receipt.verification.frame_images != "fail";
    js["verification"] = {
        {"requested_policy", receipt.verification.requested_policy},
        {"resolved_policy", receipt.verification.resolved_policy},
        {"status", verification_ok ? "pass" : "fail"},
        {"ffprobe", receipt.verification.ffprobe},
        {"decode", receipt.verification.decode},
        {"frame_count", receipt.verification.frame_count},
        {"codec", receipt.verification.codec},
        {"pixel_format", receipt.verification.pixel_format},
        {"resolution", receipt.verification.resolution},
        {"fps", receipt.verification.fps},
        {"audio", receipt.verification.audio},
        {"hdr_mastering_metadata", receipt.verification.hdr_mastering_metadata},
        {"frame_images", receipt.verification.frame_images},
    };
    js["timing_ms"] = {
        {"sha256_ms", receipt.timing.sha256_ms},
        {"probe_ms", receipt.timing.probe_ms},
        {"count_frames_ms", receipt.timing.count_frames_ms},
        {"decode_ms", receipt.timing.decode_ms},
        {"total_ms", receipt.timing.total_ms},
    };
    js["output"] = {
        {"bytes", receipt.bytes},
        {"sha256", receipt.sha256},
        {"color_space", receipt.output_color_space},
        {"reference_white_nits", receipt.reference_white_nits},
        {"ocio_display", receipt.ocio_output_display},
        {"ocio_view", receipt.ocio_output_view},
        {"frame_images", nlohmann::json::array()},
    };
    if (receipt.hdr_mastering_metadata) {
        const auto& metadata = *receipt.hdr_mastering_metadata;
        js["output"]["hdr_mastering_metadata"] = {
            {"display_primaries", metadata.display_primaries},
            {"white_point", metadata.white_point},
            {"min_luminance_nits", metadata.min_luminance_nits},
            {"max_luminance_nits", metadata.max_luminance_nits},
        };
        if (metadata.content_light) {
            js["output"]["hdr_mastering_metadata"]["content_light"] = {
                {"max_cll_nits", metadata.content_light->max_cll_nits},
                {"max_fall_nits", metadata.content_light->max_fall_nits},
            };
        }
    }
    for (const auto& frame_image : receipt.frame_images) {
        js["output"]["frame_images"].push_back({
            {"frame_index", frame_image.frame_index},
            {"bytes", frame_image.bytes},
            {"sha256", frame_image.sha256},
        });
    }
    js["copy_eligible"] = receipt.copy_eligible;

    const std::filesystem::path receipt_path =
        output_path.string() + ".receipt.json";
    std::ofstream out(receipt_path, std::ios::binary);
    out << js.dump(2) << '\n';
    return receipt_path;
}


} // namespace chronon3d::verification
