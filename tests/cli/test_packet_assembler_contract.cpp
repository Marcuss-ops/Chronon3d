#include <doctest/doctest.h>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/gop_execution_executor.hpp>
#include "utils/video/gop_smart_copy.hpp"
#include "utils/video/encoder_bitstream_contract.hpp"
#include "utils/video/variant_batch.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "utils/video/native_av_encoder.hpp"
#endif

using chronon3d::media::AudioExecutionPath;
using chronon3d::media::resolve_audio_execution;
using chronon3d::media::resolve_video_execution;
using chronon3d::media::resolve_video_execution_path;
using chronon3d::media::resolve_video_execution_path_from_gop_analysis;
using chronon3d::media::VideoExecutionPath;

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
    CHECK(decision.plan.handoff == chronon3d::media::SurfaceHandoffPath::Direct);
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

TEST_CASE("VideoExecutionResolver: output contract is validated before planning") {
    using namespace chronon3d::media;
    const ExecutionRequirements requirements{
        .gpu_required = true, .cpu_fallback_allowed = false,
        .composition_required = false, .video_source_required = true,
        .packet_copy_allowed = false};
    const VideoCapabilities capabilities{
        .nvdec = true, .nvenc = true, .vulkan_graph = true, .cuda_native = true};

    CHECK_FALSE(resolve_video_execution(
        requirements, OutputSpec{.codec = "not-a-codec"}, capabilities).valid);
    CHECK_FALSE(resolve_video_execution(
        requirements, OutputSpec{.width = 1920, .height = 0}, capabilities).valid);
    CHECK_FALSE(resolve_video_execution(
        requirements, OutputSpec{.fps_num = 30, .fps_den = 0}, capabilities).valid);
}

TEST_CASE("VideoExecutionResolver: strict image/text composition uses native contract") {
    using namespace chronon3d::media;
    const ExecutionRequirements requirements{
        .gpu_required = true, .cpu_fallback_allowed = false,
        .composition_required = true, .video_source_required = false,
        .packet_copy_allowed = false};
    const VideoCapabilities capabilities{.vulkan_graph = true};
    const auto decision = resolve_video_execution(
        requirements, OutputSpec{.codec = "h264"}, capabilities);
    REQUIRE(decision.valid);
    CHECK(decision.plan.composite == CompositePath::VulkanGraph);
    CHECK(decision.plan.encode == EncodePath::Pipe);
    CHECK(decision.plan.handoff == SurfaceHandoffPath::HostUpload);

    const auto parameters = resolve_canonical_execution_parameters(requirements);
    // A strict GPU request is a native contract even when the composition
    // has no source video. Capability discovery decides whether the request
    // can run; canonical parameter resolution must not silently turn it into
    // a host-frame pipe job.
    CHECK(parameters.hardware_encoder == "nvenc");
    CHECK(parameters.encoder_backend == "native");
    CHECK(parameters.native_nvenc);
    CHECK_FALSE(parameters.direct_yuv);
}

TEST_CASE("VideoExecutionResolver: final GOP decisions are equivalent to semantic inputs") {
    using namespace chronon3d::media;
    const ExecutionRequirements video_only{
        .gpu_required = false, .cpu_fallback_allowed = true,
        .composition_required = false, .video_source_required = true,
        .packet_copy_allowed = true};
    const OutputSpec output{.codec = "h264", .width = 1920, .height = 1080,
                            .fps_num = 30, .fps_den = 1};
    const VideoCapabilities capable{
        .nvdec = true, .nvenc = true, .vulkan_graph = true, .cuda_native = true};

    const auto all_copy = resolve_video_execution_with_gop_analysis(
        video_only, output, capable, false, true,
        GopAnalysisSummary{.available = true, .total_gops = 2,
                           .copy_gops = 2, .reencode_gops = 0,
                           .all_copy = true, .hybrid = false,
                           .safe_to_splice = true});
    REQUIRE(all_copy.valid);
    REQUIRE(all_copy.gop_plan.has_value());
    CHECK(all_copy.reason == "gop_all_copy_eligible");
    CHECK(all_copy.plan.decode == DecodePath::PacketCopy);
    CHECK(all_copy.plan.composite == CompositePath::None);
    CHECK(all_copy.plan.encode == EncodePath::PacketCopy);
    CHECK(all_copy.gop_plan->all_copy);

    const auto unsafe_hybrid = resolve_video_execution_with_gop_analysis(
        video_only, output, capable, false, true,
        GopAnalysisSummary{.available = true, .total_gops = 2,
                           .copy_gops = 1, .reencode_gops = 1,
                           .all_copy = false, .hybrid = true,
                           .safe_to_splice = false});
    CHECK_FALSE(unsafe_hybrid.valid);
    CHECK(unsafe_hybrid.reason == "gop_splice_unsafe");
    CHECK(unsafe_hybrid.plan.composite == CompositePath::DirectYuv);
    CHECK(unsafe_hybrid.plan.encode == EncodePath::Nvenc);
    REQUIRE(unsafe_hybrid.gop_plan.has_value());
    CHECK(unsafe_hybrid.gop_plan->work_units.empty());

    const GopAnalysisSummary scheduled_hybrid{
        .available = true, .total_gops = 2, .copy_gops = 1,
        .reencode_gops = 1, .all_copy = false, .hybrid = true,
        .safe_to_splice = true,
        .work_units = {{.ordinal = 0, .first_pts = 0, .last_pts = 29,
                        .kind = GopWorkKind::CopyPackets},
                       {.ordinal = 1, .first_pts = 30, .last_pts = 59,
                        .kind = GopWorkKind::Reencode}}};
    const auto scheduled = resolve_video_execution_with_gop_analysis(
        video_only, output, capable, false, true, scheduled_hybrid);
    CHECK(scheduled.valid);
    CHECK(scheduled.reason == "hybrid_gop_boundary_reencode");
    CHECK(scheduled.codec_path == VideoExecutionPath::BoundaryReencode);
    REQUIRE(scheduled.gop_plan.has_value());
    REQUIRE(scheduled.gop_plan->work_units.size() == 2);
    CHECK(scheduled.gop_plan->work_units[0].copies_packets());
    CHECK_FALSE(scheduled.gop_plan->work_units[1].copies_packets());

    // A visual composition may require the Vulkan graph for the changed GOP,
    // while untouched GOPs are still safe to copy. The graph requirement must
    // not erase the compiler-owned hybrid schedule.
    auto gpu_composition = video_only;
    gpu_composition.gpu_required = true;
    gpu_composition.composition_required = true;
    const auto graph_hybrid = resolve_video_execution_with_gop_analysis(
        gpu_composition, output, capable, false, true, scheduled_hybrid);
    CHECK(graph_hybrid.valid);
    CHECK(graph_hybrid.reason == "hybrid_gop_full_graph");
    CHECK(graph_hybrid.codec_path == VideoExecutionPath::BoundaryReencode);
    CHECK(graph_hybrid.plan.composite == CompositePath::VulkanGraph);
    REQUIRE(graph_hybrid.gop_plan.has_value());
    CHECK(graph_hybrid.gop_plan->hybrid);
    CHECK(graph_hybrid.gop_plan->copy_gops == 1);
    CHECK(graph_hybrid.gop_plan->reencode_gops == 1);

    auto malformed = scheduled_hybrid;
    malformed.copy_gops = 2;
    CHECK_FALSE(malformed.valid_schedule());
    const auto rejected_schedule = resolve_video_execution_with_gop_analysis(
        video_only, output, capable, false, true, malformed);
    CHECK_FALSE(rejected_schedule.valid);
    CHECK(rejected_schedule.reason == "gop_schedule_invalid");

    const auto unavailable = resolve_video_execution_with_gop_analysis(
        video_only, output, capable, true, false, GopAnalysisSummary{});
    CHECK_FALSE(unavailable.valid);
    CHECK(unavailable.reason == "gop_copy_preflight_unavailable");
    CHECK(unavailable.downgrade_count == 1);
    CHECK(unavailable.plan.decode == DecodePath::Nvdec);
    CHECK(unavailable.plan.composite == CompositePath::DirectYuv);
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

TEST_CASE("VideoExecutionResolver: codec boundary mirrors the audio decision table") {
    // Same four states as AudioExecutionPath, plus the splice-safety fact that
    // only video has: an unsafe splice forbids a boundary re-encode even when
    // the change is confined to the boundary.
    CHECK(resolve_video_execution_path(false, false, true, true) ==
          VideoExecutionPath::CopyPackets);
    CHECK(resolve_video_execution_path(false, true, true, true) ==
          VideoExecutionPath::TrimPackets);
    CHECK(resolve_video_execution_path(true, true, true, true) ==
          VideoExecutionPath::BoundaryReencode);
    CHECK(resolve_video_execution_path(false, false, false, true) ==
          VideoExecutionPath::FullReencode);
    // Interior change: the copied interior would reference rebuilt pixels.
    CHECK(resolve_video_execution_path(true, false, true, true) ==
          VideoExecutionPath::FullReencode);
    // Unsafe splice: concatenating copied + encoded packets is undecodable.
    CHECK(resolve_video_execution_path(true, true, true, false) ==
          VideoExecutionPath::FullReencode);
    // Codec mismatch always forces a full re-encode.
    CHECK(resolve_video_execution_path(false, true, false, true) ==
          VideoExecutionPath::FullReencode);
}

TEST_CASE("VideoExecutionResolver: GOP analysis is the codec-boundary authority") {
    using chronon3d::media::GopAnalysisSummary;
    using chronon3d::media::ExecutionRequirements;
    using chronon3d::media::OutputSpec;
    using chronon3d::media::VideoCapabilities;

    const ExecutionRequirements video_only{
        .gpu_required = false, .cpu_fallback_allowed = true,
        .composition_required = false, .video_source_required = true,
        .packet_copy_allowed = true};
    const OutputSpec output{.codec = "h264", .width = 1920, .height = 1080,
                            .fps_num = 30, .fps_den = 1};
    const VideoCapabilities capable{
        .nvdec = true, .nvenc = true, .vulkan_graph = true, .cuda_native = true};

    const GopAnalysisSummary all_copy{.available = true, .total_gops = 3,
                                      .copy_gops = 3, .reencode_gops = 0,
                                      .all_copy = true, .hybrid = false,
                                      .safe_to_splice = true};
    // An untouched, codec-compatible segment is copied, whether the intent was
    // an explicit copy or an eligible hybrid.
    CHECK(resolve_video_execution_path_from_gop_analysis(true, false, all_copy) ==
          VideoExecutionPath::CopyPackets);
    CHECK(resolve_video_execution_path_from_gop_analysis(false, true, all_copy) ==
          VideoExecutionPath::CopyPackets);
    // No reuse intent: the source is not a copy candidate at all.
    CHECK(resolve_video_execution_path_from_gop_analysis(false, false, all_copy) ==
          VideoExecutionPath::FullReencode);

    // A proven unchanged sub-range is packet-trimmable, but must not be
    // reported as a whole-clip copy. The distinction drives the executor's
    // random-access boundary handling.
    auto trimmed_copy = all_copy;
    trimmed_copy.trim_only = true;
    trimmed_copy.source_time_base_num = 1;
    trimmed_copy.source_time_base_den = 1000;
    trimmed_copy.work_units = {
        chronon3d::media::GopWorkUnit{
            .ordinal = 0, .first_pts = 1000, .last_pts = 1999,
            .first_dts = 1000, .last_dts = 1999,
            .kind = chronon3d::media::GopWorkKind::CopyPackets}};
    CHECK(resolve_video_execution_path_from_gop_analysis(true, false, trimmed_copy) ==
          VideoExecutionPath::TrimPackets);
    const auto trimmed_decision = resolve_video_execution_with_gop_analysis(
        video_only, output, capable, true, false, trimmed_copy);
    REQUIRE(trimmed_decision.valid);
    CHECK(trimmed_decision.codec_path == VideoExecutionPath::TrimPackets);
    REQUIRE(trimmed_decision.gop_plan.has_value());
    CHECK(trimmed_decision.gop_plan->trim_only);
    CHECK(trimmed_decision.gop_plan->source_time_base_num == 1);
    CHECK(trimmed_decision.gop_plan->source_time_base_den == 1000);
    REQUIRE(trimmed_decision.gop_plan->work_units.size() == 1);
    CHECK(trimmed_decision.gop_plan->work_units.front().first_dts == 1000);
    CHECK(trimmed_decision.gop_plan->work_units.front().last_dts == 1999);

    const GopAnalysisSummary hybrid_safe{.available = true, .total_gops = 3,
                                         .copy_gops = 1, .reencode_gops = 2,
                                         .all_copy = false, .hybrid = true,
                                         .safe_to_splice = true};
    CHECK(resolve_video_execution_path_from_gop_analysis(false, true, hybrid_safe) ==
          VideoExecutionPath::BoundaryReencode);

    auto hybrid_unsafe = hybrid_safe;
    hybrid_unsafe.safe_to_splice = false;
    CHECK(resolve_video_execution_path_from_gop_analysis(false, true, hybrid_unsafe) ==
          VideoExecutionPath::FullReencode);

    const GopAnalysisSummary all_reencode{.available = true, .total_gops = 2,
                                          .copy_gops = 0, .reencode_gops = 2,
                                          .all_copy = false, .hybrid = false,
                                          .safe_to_splice = true};
    CHECK(resolve_video_execution_path_from_gop_analysis(false, true, all_reencode) ==
          VideoExecutionPath::FullReencode);

    // Fail closed: an explicit copy intent the preflight cannot prove, and an
    // uninspectable source, both collapse to FullReencode.
    CHECK(resolve_video_execution_path_from_gop_analysis(true, false, GopAnalysisSummary{}) ==
          VideoExecutionPath::FullReencode);
    CHECK(resolve_video_execution_path_from_gop_analysis(false, true, GopAnalysisSummary{}) ==
          VideoExecutionPath::FullReencode);
}

TEST_CASE("VideoExecutionResolver: decisions report a plan-consistent codec boundary") {
    using namespace chronon3d::media;
    const ExecutionRequirements video_only{
        .gpu_required = false, .cpu_fallback_allowed = true,
        .composition_required = false, .video_source_required = true,
        .packet_copy_allowed = true};
    const OutputSpec output{.codec = "h264", .width = 1920, .height = 1080,
                            .fps_num = 30, .fps_den = 1};
    const VideoCapabilities capable{
        .nvdec = true, .nvenc = true, .vulkan_graph = true, .cuda_native = true};
    const GopAnalysisSummary all_copy{.available = true, .total_gops = 2,
                                      .copy_gops = 2, .reencode_gops = 0,
                                      .all_copy = true, .hybrid = false,
                                      .safe_to_splice = true};

    // Intent-only resolution already speaks the codec-boundary language.
    CHECK(resolve_video_execution(video_only, output, capable, true, true, false)
              .codec_path == VideoExecutionPath::CopyPackets);
    CHECK(resolve_video_execution(video_only, output, capable, true, false, true)
              .codec_path == VideoExecutionPath::BoundaryReencode);

    // Final resolution: a proven whole-clip copy stays a copy.
    CHECK(resolve_video_execution_with_gop_analysis(
              video_only, output, capable, false, true, all_copy)
              .codec_path == VideoExecutionPath::CopyPackets);

    // A downgraded plan re-encodes every frame, so it must not claim reuse.
    auto hybrid_unsafe = all_copy;
    hybrid_unsafe.hybrid = true;
    hybrid_unsafe.all_copy = false;
    hybrid_unsafe.copy_gops = 1;
    hybrid_unsafe.reencode_gops = 1;
    hybrid_unsafe.safe_to_splice = false;
    const auto downgraded = resolve_video_execution_with_gop_analysis(
        video_only, output, capable, false, true, hybrid_unsafe);
    CHECK(downgraded.downgrade_count == 1);
    CHECK(downgraded.codec_path == VideoExecutionPath::FullReencode);

    // A composition is re-rendered end to end even with a copy-capable source.
    const auto composed = resolve_video_execution_with_gop_analysis(
        ExecutionRequirements{.gpu_required = true, .cpu_fallback_allowed = false,
                              .composition_required = true,
                              .packet_copy_allowed = true},
        output, capable, false, true, all_copy);
    CHECK(composed.codec_path == VideoExecutionPath::FullReencode);
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

TEST_CASE("GOP executor dispatches the verified schedule through one MuxSession") {
    using namespace chronon3d::media;
    MuxSession mux;
    const auto compatible = BitstreamCompatibility{
        .codec_match = true,
        .profile_match = true,
        .level_compatible = true,
        .dimensions_match = true,
        .pixel_format_match = true,
        .parameter_sets_compatible = true,
        .color_params_match = true,
        .random_access_safe = true};
    const auto encoded_range = TimestampRange{30, 59, true};
    const GopExecutionPlan plan{
        .total_gops = 2,
        .copy_gops = 1,
        .reencode_gops = 1,
        .all_copy = false,
        .hybrid = true,
        .safe_to_splice = true,
        .work_units = {
            GopWorkUnit{
                .ordinal = 0,
                .first_pts = 0,
                .last_pts = 29,
                .first_dts = 0,
                .last_dts = 29,
                .kind = GopWorkKind::CopyPackets,
                .source_packets = TimestampRange{0, 29, true},
                .compatibility = compatible},
            GopWorkUnit{
                .ordinal = 1,
                .first_pts = 30,
                .last_pts = 59,
                .first_dts = 30,
                .last_dts = 59,
                .kind = GopWorkKind::Reencode,
                .source_packets = encoded_range,
                .decode_range = encoded_range,
                .render_range = encoded_range,
                .encode_range = encoded_range,
                .force_idr_at_start = true}},
    };

    std::array<GopWorkKind, 2> seen{};
    std::size_t seen_count = 0;
    GopExecutionCallbacks callbacks;
    callbacks.copy_packets = [&](const GopWorkUnit& unit, MuxSession& sink) {
        CHECK(&sink == &mux);
        seen[seen_count++] = unit.kind;
        return true;
    };
    callbacks.reencode = [&](const GopWorkUnit& unit, MuxSession& sink) {
        CHECK(&sink == &mux);
        seen[seen_count++] = unit.kind;
        return true;
    };

    GopExecutionResult result;
    std::string reason;
    CHECK(GopExecutionExecutor::execute(plan, mux, callbacks, result, reason));
    CHECK(reason.empty());
    CHECK(result.copied_gops == 1);
    CHECK(result.reencoded_gops == 1);
    CHECK(seen_count == 2);
    CHECK(seen[0] == GopWorkKind::CopyPackets);
    CHECK(seen[1] == GopWorkKind::Reencode);
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

    // H.264/H.265 B-frames are commonly demuxed with presentation-order
    // reordering while decode timestamps remain monotonic. That is safe for
    // packet copy; rejecting it would turn valid copy candidates into a full
    // render/encode.
    const std::array<chronon3d::cli::CompressedPacketInfo, 3> reordered{ {
        {.pts = 0, .dts = 0, .keyframe = true},
        {.pts = 2, .dts = 1},
        {.pts = 1, .dts = 2}}};
    const auto reordered_analysis =
        chronon3d::cli::analyze_gop(reordered, true, false);
    CHECK(reordered_analysis.closed);
    CHECK(reordered_analysis.safe_random_access);
    CHECK(reordered_analysis.first_pts == 0);
    CHECK(reordered_analysis.last_pts == 2);
    CHECK(reordered_analysis.first_dts == 0);
    CHECK(reordered_analysis.last_dts == 2);

    // Intersection must use the presentation-time envelope, not the first
    // and last packet in decode order. The packet with PTS=2 is inside this
    // edit even though the final packet has PTS=1.
    CHECK(chronon3d::cli::gop_intersects_edit(reordered, 1, 3));
    CHECK_FALSE(chronon3d::cli::gop_intersects_edit(reordered, 3, 4));

    const std::array<chronon3d::cli::CompressedPacketInfo, 2> bad_decode_order{{
        {.pts = 0, .dts = 1, .keyframe = true},
        {.pts = 1, .dts = 0}}};
    CHECK_FALSE(chronon3d::cli::analyze_gop(
        bad_decode_order, true, false).safe_random_access);

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

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
TEST_CASE("CPU GOP trim snaps to complete random-access groups") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto source = std::filesystem::temp_directory_path() /
        "chronon3d_gop_copy_contract_source.mp4";
    const auto output = std::filesystem::temp_directory_path() /
        "chronon3d_gop_copy_contract_output.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(source, cleanup_error);
    std::filesystem::remove(output, cleanup_error);

    const std::string source_command =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i testsrc2=size=64x64:rate=10:duration=3 "
        "-an -c:v libx264 -preset ultrafast -pix_fmt yuv420p "
        "-g 10 -keyint_min 10 -sc_threshold 0 " + source.string();
    if (std::system(source_command.c_str()) != 0) {
        MESSAGE("Skipping — ffmpeg cannot create the H.264 fixture");
        std::filesystem::remove(source, cleanup_error);
        return;
    }

    const auto copied = chronon3d::cli::copy_gop_source(
        source.string(), output.string(), 0.4, 2.4);
    REQUIRE(copied.has_value());
    CHECK(copied->video_packets > 0);
    CHECK(copied->video_packets < 30);

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, output.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    int video_index = -1;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = static_cast<int>(index);
            break;
        }
    }
    REQUIRE(video_index >= 0);
    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    bool first_video_packet = true;
    bool first_video_is_keyframe = false;
    std::uint64_t video_packets = 0;
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_index) {
            if (first_video_packet) {
                first_video_packet = false;
                first_video_is_keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            }
            ++video_packets;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&format);
    CHECK(first_video_is_keyframe);
    CHECK(video_packets == copied->video_packets);

    // A short authored cue must only dirty the GOP that contains it. The
    // analyzer receives the same source codec contract that the executor
    // would use, so this exercises the real CPU preflight rather than only
    // the pure resolver table.
    chronon3d::cli::BitstreamTargetContract target;
    format = nullptr;
    REQUIRE(avformat_open_input(&format, source.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    const AVCodecParameters* source_params = nullptr;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        const auto* params = format->streams[index]->codecpar;
        if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
            source_params = params;
            break;
        }
    }
    REQUIRE(source_params != nullptr);
    target.codec = source_params->codec_id;
    target.profile = source_params->profile;
    target.level = source_params->level;
    target.width = static_cast<std::uint32_t>(source_params->width);
    target.height = static_cast<std::uint32_t>(source_params->height);
    target.pixel_format = static_cast<chronon3d::cli::BitstreamPixelFormat>(
        source_params->format);
    if (source_params->extradata && source_params->extradata_size > 0) {
        target.parameter_sets.assign(
            source_params->extradata,
            source_params->extradata + source_params->extradata_size);
    }
    target.color_range = source_params->color_range;
    target.color_space = source_params->color_space;
    target.color_primaries = source_params->color_primaries;
    target.color_trc = source_params->color_trc;
    avformat_close_input(&format);

    const std::array<chronon3d::media::ChangedInterval, 1> cue{{
        {.start_seconds = 1.0,
         .end_seconds = 1.1,
         .kind = chronon3d::media::ChangedIntervalKind::Content}}};
    const auto analysis = chronon3d::cli::inspect_gop_source(
        source.string(), target, 0.0, 3.0, true, cue);
    REQUIRE(analysis.has_value());
    REQUIRE(analysis->plans.size() == 3);
    CHECK(analysis->copy_count == 2);
    CHECK(analysis->reencode_count == 1);
    CHECK(analysis->plans[0].copy_packets());
    CHECK_FALSE(analysis->plans[1].copy_packets());
    CHECK(analysis->plans[2].copy_packets());

    std::filesystem::remove(source, cleanup_error);
    std::filesystem::remove(output, cleanup_error);
}
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
// Regression: the NAL length size lives at a CODEC-SPECIFIC offset in the
// configuration record (avcC byte 4, hvcC byte 21). Reading the AVC offset out
// of an HEVC record produced a bogus length (1 instead of 4), every HEVC packet
// failed random-access certification, and the entire HEVC packet-copy lane was
// silently downgraded to a full re-encode.
TEST_CASE("HEVC length-prefixed GOPs are certified copy-eligible (hvcC)") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto source = std::filesystem::temp_directory_path() /
        "chronon3d_hevc_gop_copy_source.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(source, cleanup_error);
    const std::string command =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i testsrc2=size=64x64:rate=10:duration=3 "
        "-an -c:v libx265 -pix_fmt yuv420p "
        "-x265-params keyint=10:min-keyint=10:scenecut=0:open-gop=0 " +
        source.string();
    if (std::system(command.c_str()) != 0) {
        MESSAGE("Skipping — ffmpeg cannot create the HEVC fixture");
        std::filesystem::remove(source, cleanup_error);
        return;
    }

    const auto remux = chronon3d::cli::make_remux_target_contract(source.string());
    REQUIRE(remux.has_value());
    REQUIRE(remux->codec == AV_CODEC_ID_HEVC);
    REQUIRE(remux->parameter_sets.size() > 21);

    const auto analysis = chronon3d::cli::inspect_gop_source(
        source.string(), *remux, 0.0, 3.0, /*content_changed=*/false);
    REQUIRE(analysis.has_value());
    REQUIRE(analysis->plans.size() == 3);
    // Every untouched HEVC GOP must be a copy candidate. Zero copy-eligible
    // GOPs here is the regression this case exists to catch.
    CHECK(analysis->copy_count == analysis->plans.size());
    CHECK(analysis->reencode_count == 0);
    CHECK(analysis->all_copy_eligible);

    std::filesystem::remove(source, cleanup_error);
}
#endif

// ── P0-5A — the splice target is ENCODER-owned ──────────────────────────
//
// Before this gate the compressed-source preflight opened `source_path` and
// overwrote every target field with the source's own values, so
// inspect_gop_source() compared the source against itself and always reported
// "compatible". These cases pin the replacement contract: the encoder owns the
// target, the remux lane is explicitly separated, and an unmaterialized
// encoder target can never certify a splice.

namespace {

chronon3d::cli::FfmpegPipeOptions p0_5a_pipe_options(const std::string& output) {
    chronon3d::cli::FfmpegPipeOptions options;
    options.width = 64;
    options.height = 64;
    options.fps_num = 10;
    options.fps_den = 1;
    options.codec = "libx264";
    options.hardware_encoder = "none";
    options.preset = "ultrafast";
    options.rate_control_mode = "crf";
    options.rate_control_mode_explicit = true;
    options.crf = 23;
    options.crf_explicit = true;
    options.output_path = output;
    return options;
}

} // namespace

TEST_CASE("P0-5A: the declared splice target is encoder-owned and fail-closed") {
    const auto declared = chronon3d::cli::resolve_encoder_bitstream_contract(
        p0_5a_pipe_options("unused-p0-5a.mp4"));
    REQUIRE(declared.has_value());
    CHECK(declared->producer == chronon3d::cli::BitstreamTargetProducer::Encoder);
    CHECK(declared->width == 64);
    CHECK(declared->height == 64);
    // The encoder's OUTPUT facts do not exist before it runs. They stay
    // explicitly unknown so the gate fails closed rather than assuming parity
    // with the source.
    CHECK(declared->profile < 0);
    CHECK(declared->level < 0);
    CHECK(declared->parameter_sets.empty());
    CHECK_FALSE(declared->encoder_materialized);
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    CHECK(declared->codec == AV_CODEC_ID_H264);
    CHECK(declared->color_range == AVCOL_RANGE_MPEG);
    CHECK(declared->color_space == AVCOL_SPC_BT709);
    CHECK(declared->color_primaries == AVCOL_PRI_BT709);
    CHECK_FALSE(declared->valid());
#endif
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
TEST_CASE("P0-5A: an unmaterialized encoder target cannot certify a splice") {
    AVCodecParameters source{};
    source.codec_id = AV_CODEC_ID_H264;
    source.profile = 100;
    source.level = 40;
    source.width = 64;
    source.height = 64;
    source.format = AV_PIX_FMT_YUV420P;
    const std::array<std::uint8_t, 3> extradata{{1, 2, 3}};
    source.extradata = const_cast<std::uint8_t*>(extradata.data());
    source.extradata_size = static_cast<int>(extradata.size());
    source.color_range = AVCOL_RANGE_MPEG;
    source.color_space = AVCOL_SPC_BT709;
    source.color_primaries = AVCOL_PRI_BT709;
    source.color_trc = AVCOL_TRC_BT709;

    const auto declared = chronon3d::cli::resolve_encoder_bitstream_contract(
        p0_5a_pipe_options("unused-p0-5a.mp4"));
    REQUIRE(declared.has_value());

    // The old implementation produced a target from these very parameters and
    // therefore reported safe_to_splice() == true. The encoder-owned target is
    // not materialized, so the splice must be refused.
    CHECK_FALSE(chronon3d::cli::compare_bitstream_compatibility(
                    source, *declared, /*random_access_safe=*/true)
                    .safe_to_splice());
}
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
TEST_CASE("P0-5A: remux lane keeps the source contract, encoder lane fails closed") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto source = std::filesystem::temp_directory_path() /
        "chronon3d_p0_5a_splice_source.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(source, cleanup_error);
    const std::string command =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i testsrc2=size=64x64:rate=10:duration=3 "
        "-an -c:v libx264 -preset ultrafast -pix_fmt yuv420p "
        "-g 10 -keyint_min 10 -sc_threshold 0 " + source.string();
    if (std::system(command.c_str()) != 0) {
        MESSAGE("Skipping — ffmpeg cannot create the H.264 fixture");
        std::filesystem::remove(source, cleanup_error);
        return;
    }

    // No-encoder lane: the output container's stream IS the source stream, so
    // the source's own codec parameters are by definition the target.
    const auto remux = chronon3d::cli::make_remux_target_contract(source.string());
    REQUIRE(remux.has_value());
    CHECK(remux->producer == chronon3d::cli::BitstreamTargetProducer::Remux);
    CHECK_FALSE(remux->encoder_materialized);

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, source.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    const AVCodecParameters* params = nullptr;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            params = format->streams[index]->codecpar;
            break;
        }
    }
    REQUIRE(params != nullptr);
    CHECK(remux->codec == params->codec_id);
    CHECK(remux->profile == params->profile);
    CHECK(remux->level == params->level);
    CHECK(remux->width == static_cast<std::uint32_t>(params->width));
    CHECK(remux->height == static_cast<std::uint32_t>(params->height));
    CHECK(remux->parameter_sets.size() ==
          static_cast<std::size_t>(params->extradata_size));
    avformat_close_input(&format);

    // Pure time-range reuse on the remux lane still plans copies: behaviour on
    // the no-encoder lane is unchanged by the P0-5A ownership fix.
    const auto remux_analysis = chronon3d::cli::inspect_gop_source(
        source.string(), *remux, 0.0, 3.0, /*content_changed=*/false);
    REQUIRE(remux_analysis.has_value());
    CHECK(remux_analysis->copy_count == remux_analysis->plans.size());

    // Encoder lane: the target is the encoder's contract. Until the encoder is
    // materialized it can never certify a copy — no false-positive splice.
    const auto declared = chronon3d::cli::resolve_encoder_bitstream_contract(
        p0_5a_pipe_options("unused-p0-5a.mp4"));
    REQUIRE(declared.has_value());
    const auto encoder_analysis = chronon3d::cli::inspect_gop_source(
        source.string(), *declared, 0.0, 3.0, /*content_changed=*/false);
    REQUIRE(encoder_analysis.has_value());
    CHECK(encoder_analysis->copy_count == 0);
    CHECK(encoder_analysis->reencode_count == encoder_analysis->plans.size());
    CHECK_FALSE(encoder_analysis->all_copy_eligible);

    std::filesystem::remove(source, cleanup_error);
}

TEST_CASE("P0-5A: the opened encoder materializes its own bitstream contract") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto output = std::filesystem::temp_directory_path() /
        "chronon3d_p0_5a_encoder_contract.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(output, cleanup_error);

    chronon3d::cli::NativeAvEncoder encoder;
    REQUIRE(encoder.open(p0_5a_pipe_options(output.string())));

    // Zero frames encoded: no bitstream exists yet, so the contract is
    // deliberately incomplete and the splice gate stays fail-closed.
    {
        const auto& pending = encoder.encoder_target_contract();
        CHECK(pending.producer == chronon3d::cli::BitstreamTargetProducer::Encoder);
        CHECK(pending.codec == AV_CODEC_ID_H264);
        CHECK(pending.width == 64);
        CHECK(pending.height == 64);
        CHECK_FALSE(pending.encoder_materialized);
        CHECK(pending.parameter_sets.empty());
    }

    // The encoder publishes its parameter sets with the first packet; the
    // contract is then materialized from the encoder's OWN output.
    chronon3d::Framebuffer frame(64, 64);
    REQUIRE(encoder.write_frame(frame));

    // Copy: the contract describes a live session, and close() clears it.
    const auto contract = encoder.encoder_target_contract();
    CHECK(contract.producer == chronon3d::cli::BitstreamTargetProducer::Encoder);
    CHECK(contract.codec == AV_CODEC_ID_H264);
    CHECK(contract.width == 64);
    CHECK(contract.height == 64);
    const bool closed = encoder.close();

    // The invariant that matters for splice safety: the contract is never
    // FALSELY materialized. libx264 emits its SPS/PPS with the first packet
    // (in-band side data) rather than into the codec context, so a contract
    // that cannot see the encoder's real parameter sets stays unmaterialized
    // and the gate stays fail-closed. Capturing them from the produced packet
    // is the direct-mux step (P0-5D).
    if (!contract.encoder_materialized) {
        MESSAGE("Encoder contract not materialized from the codec context: "
                "the splice gate stays fail-closed (no false-positive splice)");
        CHECK(contract.parameter_sets.empty());
        std::filesystem::remove(output, cleanup_error);
        return;
    }

    // Materialized: it must describe the stream a demuxer reads back from the
    // produced file — the exact facts the splice gate compares against.
    if (!closed || !std::filesystem::exists(output)) {
        MESSAGE("Skipping demux comparison — encoder produced no container");
        std::filesystem::remove(output, cleanup_error);
        return;
    }
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, output.c_str(), nullptr, nullptr) < 0) {
        MESSAGE("Skipping demux comparison — produced container not readable");
        return;
    }
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    const AVCodecParameters* params = nullptr;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            params = format->streams[index]->codecpar;
            break;
        }
    }
    REQUIRE(params != nullptr);
    CHECK(contract.codec == params->codec_id);
    CHECK(contract.profile == params->profile);
    CHECK(contract.level == params->level);
    CHECK(contract.width == static_cast<std::uint32_t>(params->width));
    CHECK(contract.height == static_cast<std::uint32_t>(params->height));
    REQUIRE(static_cast<std::size_t>(params->extradata_size) ==
            contract.parameter_sets.size());
    CHECK(std::memcmp(params->extradata, contract.parameter_sets.data(),
                      contract.parameter_sets.size()) == 0);
    // A demuxed stream is decoder-visible by construction, so the same
    // compatibility gate must accept it against its own contract.
    CHECK(chronon3d::cli::compare_bitstream_compatibility(
              *params, contract, /*random_access_safe=*/true)
              .safe_to_splice());
    avformat_close_input(&format);
    std::filesystem::remove(output, cleanup_error);
}

// P0-5C — the encoder can write through a CALLER-OWNED MuxSession.
//
// The direct-mux lane needs one container for copied packets and re-encoded
// packets alike. When the container is borrowed the encoder must flush only
// its codec: finalizing it (or writing audio) after the first re-encoded GOP
// would close the container and drop every later work unit of the schedule.
TEST_CASE("P0-5C: an encoder writes through a caller-owned MuxSession") {
    const auto output = std::filesystem::temp_directory_path() /
        "chronon3d_p0_5c_shared_mux.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(output, cleanup_error);

    // The caller declares the container's video stream — on a splice lane that
    // is the SOURCE contract the splice gate certified.
    const AVCodec* stream_codec = avcodec_find_encoder_by_name("libx264");
    REQUIRE(stream_codec != nullptr);
    AVCodecContext* stream_context = avcodec_alloc_context3(stream_codec);
    REQUIRE(stream_context != nullptr);
    stream_context->width = 64;
    stream_context->height = 64;
    stream_context->time_base = AVRational{1, 10};
    stream_context->framerate = AVRational{10, 1};
    stream_context->pix_fmt = AV_PIX_FMT_YUV420P;
    stream_context->gop_size = 10;
    stream_context->max_b_frames = 0;
    REQUIRE(avcodec_open2(stream_context, stream_codec, nullptr) >= 0);

    chronon3d::media::MuxSession shared_mux;
    std::string reason;
    REQUIRE(shared_mux.open(chronon3d::media::MuxOpenConfig{
        .output_path = output.string(),
        .video_codec = stream_context}, reason));

    chronon3d::cli::NativeAvEncoder encoder;
    auto options = p0_5a_pipe_options(output.string());
    options.borrowed_mux = &shared_mux;
    REQUIRE(encoder.open(options));
    CHECK(encoder.borrows_caller_mux());

    chronon3d::Framebuffer frame(64, 64);
    REQUIRE(encoder.write_frame(frame));

    // close() must flush the encoder and leave the container open.
    REQUIRE(encoder.close());
    CHECK_FALSE(encoder.borrows_caller_mux());

    // The caller finalizes exactly once, after every work unit.
    CHECK(shared_mux.finalize());
    avcodec_free_context(&stream_context);

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, output.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    int video_index = -1;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = static_cast<int>(index);
            break;
        }
    }
    REQUIRE(video_index >= 0);
    CHECK(format->streams[video_index]->codecpar->codec_id == AV_CODEC_ID_H264);
    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    std::uint64_t video_packets = 0;
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_index) ++video_packets;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&format);
    // The borrowed container really did receive the encoder's packet.
    CHECK(video_packets == 1);

    std::filesystem::remove(output, cleanup_error);
}
#endif

TEST_CASE("VariantBatch shares preparation and chooses scale or recompose") {
    const std::vector<chronon3d::cli::OutputVariant> landscape{
        {1920, 1080, "nv12", "h264"},
        {1280, 720, "nv12", "h264"},
        {960, 540, "nv12", "h264"}};
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
