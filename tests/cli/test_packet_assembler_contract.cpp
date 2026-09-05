#include <doctest/doctest.h>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <array>
#include <string>

#include <chronon3d/media/video/packet_assembler.hpp>
#include "utils/video/gop_smart_copy.hpp"
#include "utils/video/variant_batch.hpp"

using chronon3d::media::AudioExecutionPath;
using chronon3d::media::resolve_audio_execution;
using chronon3d::media::VideoExecutionPath;
using chronon3d::media::resolve_video_execution;

TEST_CASE("VideoExecutionResolver: semantic GPU contract selects direct composition") {
    const auto decision = resolve_video_execution(
        chronon3d::media::ExecutionRequirements{
            .gpu_required = true, .cpu_fallback_allowed = false,
            .composition_required = false, .packet_copy_allowed = false},
        chronon3d::media::OutputSpec{.codec = "h264"},
        chronon3d::media::VideoCapabilities{
            .nvdec = true, .nvenc = true, .vulkan_graph = true, .cuda_native = true});
    CHECK(decision.valid);
    CHECK(decision.plan.decode == chronon3d::media::DecodePath::Nvdec);
    CHECK(decision.plan.composite == chronon3d::media::CompositePath::DirectYuv);
    CHECK(decision.plan.encode == chronon3d::media::EncodePath::Nvenc);
    CHECK(decision.plan.interop == chronon3d::media::InteropPath::CudaNative);
    CHECK(decision.plan.handoff == chronon3d::media::SurfaceHandoffPath::Direct);
}

TEST_CASE("VideoExecutionResolver: semantic GPU graph is orthogonal") {
    const auto decision = resolve_video_execution(
        chronon3d::media::ExecutionRequirements{
            .gpu_required = true, .cpu_fallback_allowed = false,
            .composition_required = true, .packet_copy_allowed = false},
        chronon3d::media::OutputSpec{.codec = "h264"},
        chronon3d::media::VideoCapabilities{
            .nvdec = true, .nvenc = true, .vulkan_graph = true, .cuda_native = true});
    CHECK(decision.plan.decode == chronon3d::media::DecodePath::Nvdec);
    CHECK(decision.plan.composite == chronon3d::media::CompositePath::VulkanGraph);
    CHECK(decision.plan.encode == chronon3d::media::EncodePath::Nvenc);
    CHECK(decision.plan.interop == chronon3d::media::InteropPath::VulkanCuda);
    CHECK(decision.plan.handoff == chronon3d::media::SurfaceHandoffPath::VulkanCopy);
    CHECK(decision.plan.uses_gpu());
}

TEST_CASE("VideoExecutionResolver: public contract hides backend selection") {
    using namespace chronon3d::media;
    const auto gpu = resolve_video_execution(
        ExecutionRequirements{.gpu_required = true, .cpu_fallback_allowed = false,
                              .composition_required = true},
        OutputSpec{.codec = "h264", .width = 1920, .height = 1080,
                   .fps_num = 30, .fps_den = 1},
        VideoCapabilities{.nvdec = true, .nvenc = true,
                          .vulkan_graph = true, .cuda_native = true});
    CHECK(gpu.valid);
    CHECK(gpu.plan.composite == CompositePath::VulkanGraph);
    CHECK(gpu.plan.interop == InteropPath::VulkanCuda);

    const auto rejected = resolve_video_execution(
        ExecutionRequirements{.gpu_required = true, .cpu_fallback_allowed = false,
                              .composition_required = true},
        OutputSpec{}, VideoCapabilities{.nvdec = true, .nvenc = false});
    CHECK_FALSE(rejected.valid);
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
// assemble_segments lives in chronon3d_media_native (libavformat MuxSession
// authority) which is only linked when native FFmpeg is enabled.
TEST_CASE("Segment assembly request rejects empty input") {
    const auto result = chronon3d::media::assemble_segments({{}, "/tmp/final.mp4"});
    CHECK_FALSE(result.success);
    CHECK(result.reason.find("requires inputs") != std::string::npos);
}
#endif

TEST_CASE("PacketAssembler audio resolver copies unchanged compatible packets") {
    CHECK(resolve_audio_execution(false, false, true) == AudioExecutionPath::CopyPackets);
    CHECK(resolve_audio_execution(false, true, true) == AudioExecutionPath::TrimPackets);
    CHECK(resolve_audio_execution(true, true, true) == AudioExecutionPath::BoundaryReencode);
    CHECK(resolve_audio_execution(false, false, false) == AudioExecutionPath::FullReencode);
}

TEST_CASE("PacketAssembler exposes one mux finalization boundary") {
    CHECK(requires(chronon3d::media::PacketAssembler& assembler) {
        assembler.finalize();
    });
}

TEST_CASE("PacketAssembler exposes a distinct copied-video submission path") {
    CHECK(requires(chronon3d::media::PacketAssembler& assembler, AVPacket& packet,
                   AVRational time_base) {
        assembler.submit_copied_video(packet, time_base);
    });
}

TEST_CASE("GOP planner copies only a closed safe untouched GOP") {
    const std::array<chronon3d::cli::CompressedPacketInfo, 2> packets{{
        {.pts = 100, .dts = 100, .keyframe = true},
        {.pts = 101, .dts = 101}}};
    const auto analysis = chronon3d::cli::analyze_gop(packets, true, false);
    CHECK(analysis.closed);
    CHECK(analysis.safe_random_access);

    const std::array<chronon3d::cli::CompressedPacketInfo, 1> dependent{{
        {.pts = 200, .dts = 200, .keyframe = true, .references_prior_gop = true}}};
    CHECK_FALSE(chronon3d::cli::analyze_gop(dependent, true, false).safe_random_access);

    const auto safe = chronon3d::cli::plan_gop({
        .first_pts = 100,
        .last_pts = 199,
        .codec_parameters_match = true,
        .compatibility = {
            .codec_match = true,
            .profile_match = true,
            .level_compatible = true,
            .dimensions_match = true,
            .pixel_format_match = true,
            .parameter_sets_compatible = true,
            .color_params_match = true,
            .random_access_safe = true},
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = false});
    CHECK(safe.copy_packets());
    CHECK_FALSE(safe.reencode_packets());
    CHECK(safe.first_pts == 100);
    CHECK(safe.last_pts == 199);
    CHECK(safe.ordinal == 0);

    auto touched = safe;
    touched.mode = chronon3d::cli::GopExecutionMode::Reencode;
    CHECK_FALSE(touched.copy_packets());
    CHECK(chronon3d::cli::plan_gop({
        .codec_parameters_match = true,
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = true}).mode ==
          chronon3d::cli::GopExecutionMode::Reencode);

    const auto hybrid_copy = chronon3d::cli::plan_gop({
        .codec_parameters_match = true,
        .compatibility = {
            .codec_match = true,
            .profile_match = true,
            .level_compatible = true,
            .dimensions_match = true,
            .pixel_format_match = true,
            .parameter_sets_compatible = true,
            .color_params_match = true,
            .random_access_safe = true},
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = false});
    const auto hybrid_reencode = chronon3d::cli::plan_gop({
        .codec_parameters_match = true,
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = true});
    CHECK(hybrid_copy.copy_packets());
    CHECK(hybrid_reencode.reencode_packets());

    chronon3d::cli::BitstreamCompatibility incompatible;
    incompatible.codec_match = true;
    incompatible.profile_match = true;
    incompatible.level_compatible = true;
    incompatible.dimensions_match = true;
    incompatible.pixel_format_match = true;
    incompatible.parameter_sets_compatible = false;
    incompatible.color_params_match = true;
    incompatible.random_access_safe = true;
    CHECK_FALSE(incompatible.safe_to_splice());
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
TEST_CASE("BitstreamCompatibility compares codec parameters fail-closed") {
    AVCodecParameters source{};
    AVCodecParameters output{};
    source.codec_id = output.codec_id = AV_CODEC_ID_H264;
    source.profile = output.profile = 100;
    source.level = output.level = 40;
    source.width = output.width = 1920;
    source.height = output.height = 1080;
    source.format = output.format = AV_PIX_FMT_YUV420P;
    source.color_range = output.color_range = AVCOL_RANGE_MPEG;
    source.color_space = output.color_space = AVCOL_SPC_BT709;
    source.color_primaries = output.color_primaries = AVCOL_PRI_BT709;
    source.color_trc = output.color_trc = AVCOL_TRC_BT709;

    const std::array<std::uint8_t, 3> extradata{{1, 2, 3}};
    source.extradata = const_cast<std::uint8_t*>(extradata.data());
    output.extradata = const_cast<std::uint8_t*>(extradata.data());
    source.extradata_size = output.extradata_size = static_cast<int>(extradata.size());

    const auto target_from_output = [&] {
        chronon3d::cli::BitstreamTargetContract target;
        target.codec = output.codec_id;
        target.profile = output.profile;
        target.level = output.level;
        target.width = output.width;
        target.height = output.height;
        target.pixel_format = static_cast<chronon3d::cli::BitstreamPixelFormat>(output.format);
        if (output.extradata && output.extradata_size > 0) {
            target.parameter_sets.assign(output.extradata,
                                         output.extradata + output.extradata_size);
        }
        target.color_range = output.color_range;
        target.color_space = output.color_space;
        target.color_primaries = output.color_primaries;
        target.color_trc = output.color_trc;
        return target;
    };

    CHECK(chronon3d::cli::compare_bitstream_compatibility(source, target_from_output(), true)
              .safe_to_splice());
    CHECK_FALSE(chronon3d::cli::compare_bitstream_compatibility(source, target_from_output(), false)
                    .safe_to_splice());

    output.profile++;
    CHECK_FALSE(chronon3d::cli::compare_bitstream_compatibility(source, target_from_output(), true)
                    .safe_to_splice());
    output.profile--;
    output.width++;
    CHECK_FALSE(chronon3d::cli::compare_bitstream_compatibility(source, target_from_output(), true)
                    .safe_to_splice());
    output.width--;
    output.format = AV_PIX_FMT_NV12;
    CHECK_FALSE(chronon3d::cli::compare_bitstream_compatibility(source, target_from_output(), true)
                    .safe_to_splice());
    output.format = source.format;
    output.extradata = nullptr;
    CHECK_FALSE(chronon3d::cli::compare_bitstream_compatibility(source, target_from_output(), true)
                    .safe_to_splice());
}
#endif

TEST_CASE("Smart GOP compatible source produces copy candidates") {
    const auto compatible = chronon3d::cli::plan_gop({
        .first_pts = 0,
        .last_pts = 29,
        .codec_parameters_match = true,
        .compatibility = {
            .codec_match = true,
            .profile_match = true,
            .level_compatible = true,
            .dimensions_match = true,
            .pixel_format_match = true,
            .parameter_sets_compatible = true,
            .color_params_match = true,
            .random_access_safe = true},
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = false});

    chronon3d::cli::GopSourceAnalysis result;
    result.plans.push_back(compatible);
    result.copy_count = compatible.copy_packets() ? 1 : 0;
    result.reencode_count = compatible.reencode_packets() ? 1 : 0;
    result.all_copy_eligible = result.valid() && result.copy_count == result.plans.size();

    CHECK(compatible.copy_packets());
    CHECK(result.valid());
    CHECK(result.copy_count == 1);
    CHECK(result.reencode_count == 0);
    CHECK(result.all_copy_eligible);
}

TEST_CASE("Smart GOP hybrid source produces copy and reencode candidates") {
    const auto copied = chronon3d::cli::plan_gop({
        .first_pts = 0,
        .last_pts = 29,
        .codec_parameters_match = true,
        .compatibility = {
            .codec_match = true,
            .profile_match = true,
            .level_compatible = true,
            .dimensions_match = true,
            .pixel_format_match = true,
            .parameter_sets_compatible = true,
            .color_params_match = true,
            .random_access_safe = true},
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = false});
    const auto reencoded = chronon3d::cli::plan_gop({
        .first_pts = 30,
        .last_pts = 59,
        .codec_parameters_match = true,
        .compatibility = copied.compatibility,
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = true});

    chronon3d::cli::GopSourceAnalysis result;
    result.plans = {copied, reencoded};
    result.copy_count = copied.copy_packets() + reencoded.copy_packets();
    result.reencode_count = copied.reencode_packets() + reencoded.reencode_packets();

    CHECK(result.valid());
    CHECK(result.copy_count == 1);
    CHECK(result.reencode_count == 1);
    CHECK(result.is_hybrid());
    CHECK_FALSE(result.all_copy_eligible);
}

TEST_CASE("Smart GOP incompatible source produces no copy candidates") {
    const auto incompatible = chronon3d::cli::plan_gop({
        .first_pts = 0,
        .last_pts = 29,
        .codec_parameters_match = false,
        .compatibility = {},
        .closed = true,
        .safe_random_access = true,
        .intersects_edit = false});

    chronon3d::cli::GopSourceAnalysis result;
    result.plans.push_back(incompatible);
    result.copy_count = incompatible.copy_packets() ? 1 : 0;
    result.reencode_count = incompatible.reencode_packets() ? 1 : 0;
    result.all_copy_eligible = result.valid() && result.copy_count == result.plans.size();

    CHECK_FALSE(incompatible.copy_packets());
    CHECK(incompatible.reencode_packets());
    CHECK(result.valid());
    CHECK(result.copy_count == 0);
    CHECK(result.reencode_count == 1);
    CHECK_FALSE(result.is_hybrid());
    CHECK_FALSE(result.all_copy_eligible);
}

TEST_CASE("GOP source analyzer exposes packet-level demux planning") {
    CHECK(requires(const std::string& path) {
        chronon3d::cli::inspect_gop_source(
            path, chronon3d::cli::BitstreamTargetContract{}, 0.0, 100.0);
    });
}

TEST_CASE("VariantBatch shares preparation and chooses scale or recompose") {
    const std::vector<chronon3d::cli::OutputVariant> landscape{
        {1920, 1080, "nv12", "h264"},
        {1280, 720, "nv12", "h264"},
        {854, 480, "nv12", "h264"}};
    const auto scale = chronon3d::cli::plan_variant_batch(landscape);
    CHECK(scale.mode == chronon3d::cli::VariantReuseMode::MasterScale);
    CHECK(scale.share_decode);
    CHECK(scale.share_shaping);
    CHECK(scale.share_compilation);
    REQUIRE(scale.executions.size() == landscape.size());
    CHECK(scale.executions[0].source_variant_index == 0);
    CHECK_FALSE(scale.executions[0].scale_master);
    CHECK(scale.executions[1].scale_master);
    CHECK_FALSE(scale.executions[1].recompose_layout);

    const std::vector<chronon3d::cli::OutputVariant> mixed{
        {1920, 1080, "nv12", "h264"},
        {1080, 1920, "nv12", "h264"}};
    const auto recompose = chronon3d::cli::plan_variant_batch(mixed);
    CHECK(recompose.mode ==
          chronon3d::cli::VariantReuseMode::SharedResourcesRecompose);
    REQUIRE(recompose.executions.size() == mixed.size());
    CHECK(recompose.executions[1].source_variant_index == 0);
    CHECK(recompose.executions[1].recompose_layout);
}

TEST_CASE("VariantBatch renders one master and submits deterministic variants") {
    const std::vector<chronon3d::cli::OutputVariant> variants{
        {1920, 1080, "nv12", "h264"}, {1280, 720, "nv12", "h264"}};
    const auto plan = chronon3d::cli::plan_variant_batch(variants);
    int render_calls = 0;
    int scale_calls = 0;
    int recompose_calls = 0;
    std::vector<int> submitted;
    const bool ok = chronon3d::cli::execute_variant_batch<int>(
        plan,
        [&](std::size_t) { ++render_calls; return 7; },
        [&](int master, std::size_t index) {
            ++scale_calls; return master + static_cast<int>(index);
        },
        [&](int master, std::size_t index) {
            ++recompose_calls; return master - static_cast<int>(index);
        },
        [&](std::size_t index, int) { submitted.push_back(static_cast<int>(index)); return true; });
    CHECK(ok);
    CHECK(render_calls == 1);
    CHECK(scale_calls == 1);
    CHECK(recompose_calls == 0);
    CHECK(submitted == std::vector<int>{0, 1});
}
