#include "test_packet_assembler_contract_test_support.hpp"

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

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
// GopExecutionExecutor is owned by the native media authority together with
// MuxSession. The policy/plan tests above remain valid without FFmpeg, but
// this dispatch test must not make the no-native CLI test binary depend on
// symbols from the deliberately absent native media library.
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
#endif

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
