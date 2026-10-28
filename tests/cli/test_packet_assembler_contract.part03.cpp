#include "test_packet_assembler_contract_test_support.hpp"

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

    // The opened encoder states the stream it will produce, from its OWN
    // configuration record — zero frames encoded, and the compressed source is
    // never consulted. The scalars libavcodec cannot derive from parameter sets
    // alone (profile/level/pixel format) stay explicitly unstated rather than
    // being guessed, which is what keeps the gate fail-closed when they are the
    // only thing two streams differ in.
    {
        const auto& pending = encoder.encoder_target_contract();
        CHECK(pending.producer == chronon3d::cli::BitstreamTargetProducer::Encoder);
        CHECK(pending.codec == AV_CODEC_ID_H264);
        CHECK(pending.width == 64);
        CHECK(pending.height == 64);
        CHECK(pending.encoder_materialized);
        CHECK_FALSE(pending.parameter_sets.empty());
        CHECK(pending.profile == chronon3d::cli::ffmpeg_profile::h264_high);
        CHECK(pending.level < 0);
        // The container the encoder owns is declared from this same context, so
        // the parameter sets are already the ones a demuxer will read back.
        CHECK(pending.parameter_sets.size() > 10);
    }

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
    // FALSELY materialized. An encoder that has produced no parameter sets (or
    // publishes them in-band only, without a configuration record) stays
    // unmaterialized and the gate stays fail-closed; a materialized contract
    // must describe the stream a demuxer reads back from the produced file.
    REQUIRE(contract.encoder_materialized);
    CHECK(contract.profile == chronon3d::cli::ffmpeg_profile::h264_high);
    CHECK(contract.level < 0);
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
    CHECK(contract.width == static_cast<std::uint32_t>(params->width));
    CHECK(contract.height == static_cast<std::uint32_t>(params->height));
    // The container is self-describing: its configuration record carries the
    // encoder's own parameter sets. The comparison is on the NAL payloads, the
    // stream's real identity, because a demuxer reports an avcC wrapper while
    // the encoder publishes Annex-B — comparing the wrappers would reject a
    // stream that was reproduced exactly.
    const auto encoder_nals = chronon3d::cli::parameter_set_nals(
        contract.codec, contract.parameter_sets.data(),
        contract.parameter_sets.size());
    const auto demuxed_nals = chronon3d::cli::parameter_set_nals(
        params->codec_id, params->extradata,
        static_cast<std::size_t>(params->extradata_size));
    CHECK_FALSE(encoder_nals.empty());
    CHECK(encoder_nals == demuxed_nals);

    // The stream the container carries is the stream the encoder stated: the
    // ONE reproduction authority accepts it, and it is the same authority the
    // splice gate uses (never a raw byte comparison of an avcC against the
    // encoder's Annex-B parameter sets, which would reject a stream that was
    // reproduced exactly).
    chronon3d::cli::BitstreamTargetContract declared{};
    declared.producer = chronon3d::cli::BitstreamTargetProducer::Remux;
    declared.codec = params->codec_id;
    declared.profile = params->profile;
    declared.level = params->level;
    declared.pixel_format = static_cast<chronon3d::cli::BitstreamPixelFormat>(
        params->format);
    declared.width = static_cast<std::uint32_t>(params->width);
    declared.height = static_cast<std::uint32_t>(params->height);
    declared.parameter_sets.assign(
        params->extradata,
        params->extradata + static_cast<std::size_t>(params->extradata_size));
    declared.color_range = params->color_range;
    declared.color_space = params->color_space;
    declared.color_primaries = params->color_primaries;
    declared.color_trc = params->color_trc;
    CHECK(chronon3d::cli::encoder_reproduces_declared_stream(declared, contract));
    // And it fails closed: a stream this encoder did NOT produce can never
    // certify, which is what keeps a false-positive splice impossible.
    auto foreign = declared;
    foreign.width = declared.width / 2;
    CHECK_FALSE(
        chronon3d::cli::encoder_reproduces_declared_stream(foreign, contract));
    avformat_close_input(&format);
    std::filesystem::remove(output, cleanup_error);
}

TEST_CASE("NativeAvEncoder: a missing output directory is created, not fatal") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    // RenderingGen creates `<workspace>/output/` before the render, but that
    // workspace can legitimately be swept between prepare and render (the
    // worker's stale-cleanup removes a workspace whose lease lapsed). The mux
    // session must therefore create the output's parent directory itself
    // instead of dying on an undiagnosable "failed to open output sink".
    const auto root = std::filesystem::temp_directory_path() /
        "chronon3d_native_encoder_missing_output_dir";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    REQUIRE_FALSE(std::filesystem::exists(root));
    const auto nested = root / "output" / "deep";
    const auto output = nested / "result.mp4";
    REQUIRE_FALSE(std::filesystem::exists(nested));

    chronon3d::cli::NativeAvEncoder encoder;
    REQUIRE(encoder.open(p0_5a_pipe_options(output.string())));
    chronon3d::Framebuffer frame(64, 64);
    REQUIRE(encoder.write_frame(frame));
    REQUIRE(encoder.close());
    CHECK(std::filesystem::exists(output));
    CHECK(std::filesystem::file_size(output) > 0);

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("A1 CPU native export publishes MP4 sidecar receipt and fails closed") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }

    const auto output = std::filesystem::temp_directory_path() /
        "chronon3d_a1_cpu_native_export.mp4";
    const auto sidecar = std::filesystem::path(output.string() + ".timing.json");
    const auto receipt = std::filesystem::path(output.string() + ".receipt.json");
    std::error_code cleanup_error;
    std::filesystem::remove(output, cleanup_error);
    std::filesystem::remove(sidecar, cleanup_error);
    std::filesystem::remove(receipt, cleanup_error);

    {
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(p0_5a_pipe_options(output.string())));
        for (int index = 0; index < 10; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
    }

    REQUIRE(std::filesystem::exists(output));
    auto contract = chronon3d::media::video::resolve_output_contract(
        "youtube_overlay_v1");
    REQUIRE(contract.has_value());
    contract->width = 64;
    contract->height = 64;
    contract->fps = chronon3d::FrameRate{10, 1};
    contract->frame_count = 10;
    contract->audio_required = false;
    contract->audio_streams = 0;

    const auto verification = chronon3d::media::video::verify_output_contract(
        output, *contract);
    REQUIRE(verification.passed);
    REQUIRE(verification.copy_eligible);
    REQUIRE(verification.sha256.size() == 64);

    chronon3d::telemetry::FrameTelemetry frame;
    frame.frame_number = 0;
    frame.duration_ms = 1.0;
    chronon3d::cli::pipe_timing::JobTimings timings;
    timings.execution_path = "native_cpu";
    timings.target_fps = 10.0;
    timings.target_fps_num = 10;
    timings.target_fps_den = 1;
    chronon3d::cli::write_frame_timing_sidecar(
        output.string(), {frame}, {}, 10.0, 10.0, 0.0, timings, true);
    REQUIRE(std::filesystem::exists(sidecar));
    {
        std::ifstream input(sidecar);
        const auto document = nlohmann::json::parse(input);
        CHECK(document.at("schema") ==
              chronon3d::cli::pipe_timing::kTimingSidecarSchema);
        CHECK(document.at("frames_total") == 1);
        CHECK(document.at("job").at("execution_path") == "native_cpu");
    }

    chronon3d::verification::RenderReceiptInput receipt_input;
    receipt_input.job_id = "a1-cpu-native-export";
    receipt_input.chronon_version = "a1-test";
    receipt_input.git_sha = "same-sha-test";
    receipt_input.backend = "software";
    receipt_input.width = 64;
    receipt_input.height = 64;
    receipt_input.fps_num = 10;
    receipt_input.fps_den = 1;
    receipt_input.frames = 10;
    receipt_input.requested_codec = "h264";
    receipt_input.has_audio_tracks = false;
    receipt_input.receipt_verify_policy = "fast";
    const auto built_receipt = chronon3d::verification::build_render_receipt(
        receipt_input, output, true);
    CHECK(built_receipt.verification.ffprobe == "pass");
    CHECK(built_receipt.verification.frame_count == "pass");
    CHECK(built_receipt.verification.codec == "pass");
    CHECK(built_receipt.verification.pixel_format == "pass");
    CHECK(built_receipt.verification.resolution == "pass");
    CHECK(built_receipt.verification.fps == "pass");
    REQUIRE(built_receipt.copy_eligible);
    CHECK(built_receipt.sha256 == verification.sha256);
    REQUIRE(chronon3d::verification::write_render_receipt(
                 built_receipt, output) == receipt);
    REQUIRE(std::filesystem::exists(receipt));

    // Fix 2 (TICKET-OVERLAY-THROUGHPUT, dedup): the production path hands the
    // export boundary's probe + digest to the receipt builder. The reused
    // receipt must be verdict- and digest-identical to the freshly probed one
    // and must carry the caller-measured timings instead of running its own
    // probe/hash on the same bytes.
    REQUIRE(verification.probe_facts.has_value());
    auto reused_input = receipt_input;
    reused_input.precomputed_media = chronon3d::cli::media_contract_from_probe(
        *verification.probe_facts);
    reused_input.precomputed_sha256 = verification.sha256;
    reused_input.precomputed_probe_ms = verification.ffprobe_ms;
    reused_input.precomputed_sha256_ms = verification.sha256_ms;
    const auto reused_receipt = chronon3d::verification::build_render_receipt(
        reused_input, output, true);
    CHECK(reused_receipt.sha256 == built_receipt.sha256);
    CHECK(reused_receipt.copy_eligible == built_receipt.copy_eligible);
    CHECK(reused_receipt.verification.ffprobe == built_receipt.verification.ffprobe);
    CHECK(reused_receipt.verification.frame_count ==
          built_receipt.verification.frame_count);
    CHECK(reused_receipt.verification.codec == built_receipt.verification.codec);
    CHECK(reused_receipt.verification.pixel_format ==
          built_receipt.verification.pixel_format);
    CHECK(reused_receipt.verification.resolution ==
          built_receipt.verification.resolution);
    CHECK(reused_receipt.verification.fps == built_receipt.verification.fps);
    CHECK(reused_receipt.timing.ffprobe_ms == verification.ffprobe_ms);
    CHECK(reused_receipt.timing.sha256_ms == verification.sha256_ms);

    // The failure path is part of this same artifact run: a missing output can
    // never become distributable or acquire a receipt by accident.
    const auto missing = chronon3d::media::video::verify_output_contract(
        output.string() + ".missing", *contract);
    CHECK_FALSE(missing.passed);
    CHECK_FALSE(missing.copy_eligible);

    std::filesystem::remove(output, cleanup_error);
    std::filesystem::remove(sidecar, cleanup_error);
    std::filesystem::remove(receipt, cleanup_error);
}

TEST_CASE("NativeAvEncoder: EAGAIN flush drains every accepted frame at production dimensions") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }

    // Exercise the CLI's NativeAvEncoder (including its frame-threaded x264
    // flush protocol), not only the lower-level NativeAvSink. The old close
    // path ignored a null-frame EAGAIN and silently left buffered frames out.
    const int durations[] = {60, 86, 87, 96, 120, 137, 150, 180, 300};
    const auto dir = std::filesystem::temp_directory_path() /
        "chronon3d_native_encoder_frame_loss_matrix";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    auto options = p0_5a_pipe_options((dir / "unused.mp4").string());
    options.width = 1920;
    options.height = 1080;
    options.fps_num = 30;
    options.fps_den = 1;

    for (const int count : durations) {
        const auto output = dir / ("frames-" + std::to_string(count) + ".mp4");
        options.output_path = output.string();
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(options));
        chronon3d::Framebuffer frame(options.width, options.height);
        for (int index = 0; index < count; ++index) {
            REQUIRE(encoder.write_frame(frame));
        }
        CHECK(encoder.frames_written() == static_cast<std::uint64_t>(count));
        REQUIRE(encoder.close());

        auto contract = chronon3d::media::video::resolve_output_contract(
            "youtube_overlay_v1");
        REQUIRE(contract.has_value());
        contract->width = options.width;
        contract->height = options.height;
        contract->fps = chronon3d::FrameRate{30, 1};
        contract->frame_count = count;
        contract->audio_required = false;
        contract->audio_streams = 0;
        const auto verification =
            chronon3d::media::video::verify_output_contract(output, *contract);
        CHECK(verification.passed);
        CHECK(verification.frame_count == count);
        std::filesystem::remove(output, ec);
        REQUIRE_FALSE(ec);
    }
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("A1 direct native export route renders a software composition") {
    chronon3d::CompositionRegistry registry;
    registry.add(chronon3d::TypedCompositionDescriptor<int>{
        .id = "a1-native-route",
        .category = "test",
        .defaults = 0,
        .resolve_metadata = [](int) {
            return chronon3d::CompositionMetadata{
                64, 64, chronon3d::FrameRate{10, 1}, chronon3d::Frame{10}};
        },
        .factory = [](int) {
            return chronon3d::composition({.name = "a1-native-route",
                                            .width = 64,
                                            .height = 64,
                                            .duration = chronon3d::Frame{10}},
                                           [](const chronon3d::FrameContext&) {
                                               return chronon3d::Scene{};
                                           });
        }
    }.to_descriptor());

    const auto output = std::filesystem::temp_directory_path() /
        "chronon3d_a1_native_route.mp4";
    const auto sidecar = std::filesystem::path(output.string() + ".timing.json");
    const auto receipt = std::filesystem::path(output.string() + ".receipt.json");
    std::error_code cleanup_error;
    std::filesystem::remove(output, cleanup_error);
    std::filesystem::remove(sidecar, cleanup_error);
    std::filesystem::remove(receipt, cleanup_error);

    chronon3d::RenderRequest request;
    request.comp_id = "a1-native-route";
    request.mode = chronon3d::RenderMode::Video;
    request.first_frame = chronon3d::Frame{0};
    request.last_frame = chronon3d::Frame{9};
    request.output = output.string();
    request.video_settings.fps_num = 10;
    request.video_settings.fps_den = 1;
    request.video_settings.codec = "h264";
    request.video_settings.hardware_encoder = "none";
    request.video_settings.encoder_backend =
        chronon3d::media::kEncoderTransportNative;
    request.video_settings.sink_type = "ffmpeg";
    request.frame_range_explicit = true;

    const auto job = chronon3d::cli::resolve_render_request(
        registry, std::move(request));
    REQUIRE(job.has_value());
    REQUIRE(job->compiled != nullptr);
    auto options = chronon3d::cli::make_ffmpeg_export_options(*job);
    options.backend_preference = chronon3d::graph::BackendPreference::Software;
    options.output.output = output.string();
    options.output.fps_num = 10;
    options.output.fps_den = 1;
    options.encoder.codec = "h264";
    options.encoder.hardware_encoder = "none";
    options.encoder.encoder_backend = chronon3d::media::kEncoderTransportNative;

    const auto exported = chronon3d::cli::render_and_encode_native(
        registry, *job->compiled, job->comp_id, job->settings,
        chronon3d::Frame{0}, chronon3d::Frame{9}, options,
        job->execution.cpu_budget,
        std::make_shared<chronon3d::media::VideoRuntimeRegistry>());
    REQUIRE(exported.success);
    REQUIRE(exported.output_published);
    REQUIRE(exported.copy_eligible);
    CHECK(exported.sidecar_written_at != std::chrono::steady_clock::time_point{});
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(std::filesystem::exists(sidecar));
    REQUIRE(exported.sha256.size() == 64);

    auto route_contract = chronon3d::media::video::resolve_output_contract(
        "youtube_overlay_v1");
    REQUIRE(route_contract.has_value());
    route_contract->width = 64;
    route_contract->height = 64;
    route_contract->fps = chronon3d::FrameRate{10, 1};
    route_contract->audio_required = false;
    route_contract->audio_streams = 0;
    const auto route_verification =
        chronon3d::media::video::verify_output_contract(output, *route_contract);
    REQUIRE(route_verification.passed);
    REQUIRE(route_verification.copy_eligible);

    chronon3d::verification::RenderReceiptInput receipt_input;
    receipt_input.job_id = "a1-native-route";
    receipt_input.chronon_version = "a1-test";
    receipt_input.git_sha = "same-tree-test";
    receipt_input.backend = "software";
    receipt_input.width = 64;
    receipt_input.height = 64;
    receipt_input.fps_num = 10;
    receipt_input.fps_den = 1;
    receipt_input.frames = route_verification.frame_count;
    receipt_input.requested_codec = "h264";
    receipt_input.receipt_verify_policy = "fast";
    const auto built_receipt = chronon3d::verification::build_render_receipt(
        receipt_input, output, true);
    CHECK(built_receipt.verification.ffprobe == "pass");
    CHECK(built_receipt.verification.frame_count == "pass");
    CHECK(built_receipt.verification.codec == "pass");
    CHECK(built_receipt.verification.pixel_format == "pass");
    CHECK(built_receipt.verification.resolution == "pass");
    CHECK(built_receipt.verification.fps == "pass");
    REQUIRE(built_receipt.copy_eligible);
    CHECK(built_receipt.sha256 == exported.sha256);
    REQUIRE(chronon3d::verification::write_render_receipt(
                 built_receipt, output) == receipt);
    REQUIRE(std::filesystem::exists(receipt));

    const auto wrapped_output = std::filesystem::temp_directory_path() /
        "chronon3d_a1_native_route_wrapped.mp4";
    const auto wrapped_sidecar = std::filesystem::path(
        wrapped_output.string() + ".timing.json");
    std::filesystem::remove(wrapped_output, cleanup_error);
    std::filesystem::remove(wrapped_sidecar, cleanup_error);
    auto wrapped_options = options;
    wrapped_options.output.output = wrapped_output.string();
    auto wrapped_execution =
        std::make_shared<chronon3d::media::VideoJobExecutionContext>();
    wrapped_execution->video_runtimes =
        std::make_shared<chronon3d::media::VideoRuntimeRegistry>();
    auto sidecar_written_at = std::chrono::steady_clock::time_point{};
    CHECK(chronon3d::cli::render_and_encode_ffmpeg(
        registry, *job->compiled, job->comp_id, job->settings,
        chronon3d::Frame{0}, chronon3d::Frame{10}, wrapped_options,
        job->execution.cpu_budget,
        wrapped_execution, &sidecar_written_at) == 0);
    CHECK(sidecar_written_at != std::chrono::steady_clock::time_point{});
    CHECK(std::filesystem::exists(wrapped_sidecar));
    std::filesystem::remove(wrapped_output, cleanup_error);
    std::filesystem::remove(wrapped_sidecar, cleanup_error);

    std::filesystem::remove(output, cleanup_error);
    std::filesystem::remove(sidecar, cleanup_error);
    std::filesystem::remove(receipt, cleanup_error);
}

TEST_CASE("B3 CPU native segment assembly normalizes timestamps and checksums output") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto first = directory / "chronon3d_b3_segment_first.mp4";
    const auto second = directory / "chronon3d_b3_segment_second.mp4";
    const auto output = directory / "chronon3d_b3_segment_assembled.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(first, cleanup_error);
    std::filesystem::remove(second, cleanup_error);
    std::filesystem::remove(output, cleanup_error);

    for (const auto& path : {first, second}) {
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(p0_5a_pipe_options(path.string())));
        for (int index = 0; index < 10; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
    }

    const auto result = chronon3d::media::assemble_segments(
        {{first.string(), second.string()}, output.string()});
    REQUIRE(result.success);
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(result.output_checksum.size() == 64);

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
    std::int64_t previous_dts = AV_NOPTS_VALUE;
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_index) {
            ++video_packets;
            const bool monotonic_dts = previous_dts == AV_NOPTS_VALUE ||
                packet->dts >= previous_dts;
            CHECK(monotonic_dts);
            previous_dts = packet->dts;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&format);
    CHECK(video_packets > 0);

    std::filesystem::remove(first, cleanup_error);
    std::filesystem::remove(second, cleanup_error);
    std::filesystem::remove(output, cleanup_error);
}

TEST_CASE("Segment assembly rejects a truncated packet payload instead of publishing success") {
    const auto root = std::filesystem::temp_directory_path();
    const auto input = root / "chronon3d_assembly_truncated.mp4";
    const auto output = root / "chronon3d_assembly_truncated_output.mp4";
    chronon3d::cli::NativeAvEncoder encoder;
    REQUIRE(encoder.open(p0_5a_pipe_options(input.string())));
    for (int i = 0; i < 10; ++i) {
        chronon3d::Framebuffer frame(64, 64);
        REQUIRE(encoder.write_frame(frame));
    }
    REQUIRE(encoder.close());
    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, input.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    REQUIRE(av_read_frame(format, packet) >= 0);
    const auto cut = packet->pos + packet->size / 2;
    REQUIRE(packet->pos >= 0);
    av_packet_free(&packet); avformat_close_input(&format);
    // Move moov to the front so a partial payload still has readable metadata.
    const auto fast = root / "chronon3d_assembly_truncated_fast.mp4";
    const auto command = "ffmpeg -hide_banner -loglevel error -y -i " + input.string() +
        " -c copy -movflags +faststart " + fast.string();
    REQUIRE(std::system(command.c_str()) == 0);
    REQUIRE(avformat_open_input(&format, fast.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    packet = av_packet_alloc(); REQUIRE(packet != nullptr);
    REQUIRE(av_read_frame(format, packet) >= 0);
    const auto fast_cut = packet->pos + packet->size / 2;
    REQUIRE(packet->pos >= 0);
    av_packet_free(&packet); avformat_close_input(&format);
    (void)cut;
    std::filesystem::resize_file(fast, static_cast<std::uintmax_t>(fast_cut));
    const auto result = chronon3d::media::assemble_segments({{fast.string()}, output.string()});
    CHECK_FALSE(result.success);
    CHECK_FALSE(result.reason.empty());
    std::error_code ec;
    for (const auto& path : {input, fast, output}) std::filesystem::remove(path, ec);
}

TEST_CASE("Segment assembly retains audio delay on a common timeline") {
    const auto root = std::filesystem::temp_directory_path();
    const auto input = root / "chronon3d_assembly_delayed_audio.mp4";
    const auto output = root / "chronon3d_assembly_delayed_audio_output.mp4";
    const auto command = "ffmpeg -hide_banner -loglevel error -y -f lavfi -i color=c=red:s=64x64:r=10:d=1 "
        "-itsoffset 0.25 -f lavfi -i sine=frequency=440:sample_rate=48000:duration=0.5 "
        "-c:v libx264 -bf 0 -g 10 -c:a aac " + input.string();
    REQUIRE(std::system(command.c_str()) == 0);
    const auto delay = [](const std::filesystem::path& path) {
        AVFormatContext* format = nullptr;
        REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) >= 0);
        REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
        double video = 0, audio = 0;
        for (unsigned i = 0; i < format->nb_streams; ++i) {
            const auto* stream = format->streams[i];
            const double start = stream->start_time * av_q2d(stream->time_base);
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) video = start;
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) audio = start;
        }
        avformat_close_input(&format);
        return audio - video;
    };
    const auto expected = delay(input);
    REQUIRE(expected > .2);
    const auto result = chronon3d::media::assemble_segments({{input.string(), input.string()}, output.string()});
    REQUIRE(result.success);
    CHECK(delay(output) == doctest::Approx(expected).epsilon(.001));
    std::error_code ec;
    std::filesystem::remove(input, ec); std::filesystem::remove(output, ec);
}

TEST_CASE("Audio-aware assembly muxes one source stream over video-only segments") {
    const auto root = std::filesystem::temp_directory_path();
    const auto first = root / "chronon3d_anchor_child_first.mp4";
    const auto second = root / "chronon3d_anchor_child_second.mp4";
    const auto audio_source = root / "chronon3d_anchor_audio_source.m4a";
    const auto output = root / "chronon3d_anchor_assembled.mp4";
    std::error_code cleanup_error;
    for (const auto& path : {first, second, audio_source, output})
        std::filesystem::remove(path, cleanup_error);

    // Children: silent, copy-eligible segments — the video-only chunk shape.
    for (const auto& path : {first, second}) {
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(p0_5a_pipe_options(path.string())));
        for (int index = 0; index < 10; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
    }
    // The anchor's audio owner: the source stream, copied exactly once.
    const auto command = "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i sine=frequency=440:sample_rate=48000:duration=1 "
        "-c:a aac " + audio_source.string();
    REQUIRE(std::system(command.c_str()) == 0);

    chronon3d::media::SegmentAssemblyRequest request;
    request.input_paths = {first.string(), second.string()};
    request.output_path = output.string();
    request.audio_source_path = audio_source.string();
    const auto result = chronon3d::media::assemble_segments(request);
    REQUIRE(result.success);
    REQUIRE(std::filesystem::exists(output));

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, output.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    int video_index = -1, audio_index = -1;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        const auto type = format->streams[index]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && video_index < 0) video_index = static_cast<int>(index);
        if (type == AVMEDIA_TYPE_AUDIO && audio_index < 0) audio_index = static_cast<int>(index);
    }
    REQUIRE(video_index >= 0);
    REQUIRE(audio_index >= 0);
    CHECK(format->streams[audio_index]->codecpar->codec_id == AV_CODEC_ID_AAC);

    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    std::uint64_t video_packets = 0, audio_packets = 0;
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_index) ++video_packets;
        if (packet->stream_index == audio_index) ++audio_packets;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&format);
    CHECK(video_packets == 20);   // 10 + 10 child frames, nothing invented
    CHECK(audio_packets > 0);     // the source stream copied into the parent

    // "Exactly once" is a COUNT, not a presence: the parent carries the
    // source's nonnegative-PTS audio packets verbatim, matching single-job
    // mux semantics (the AAC priming packet lies before the stream origin).
    AVFormatContext* audio_format = nullptr;
    REQUIRE(avformat_open_input(&audio_format, audio_source.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(audio_format, nullptr) >= 0);
    int source_audio_index = -1;
    for (unsigned int index = 0; index < audio_format->nb_streams; ++index) {
        if (audio_format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            source_audio_index = static_cast<int>(index);
            break;
        }
    }
    REQUIRE(source_audio_index >= 0);
    AVPacket* source_packet = av_packet_alloc();
    REQUIRE(source_packet != nullptr);
    std::uint64_t source_audio_packets = 0;
    std::uint64_t priming_packets = 0;
    while (av_read_frame(audio_format, source_packet) >= 0) {
        if (source_packet->stream_index == source_audio_index) {
            const auto pts = source_packet->pts != AV_NOPTS_VALUE
                ? source_packet->pts : source_packet->dts;
            if (pts != AV_NOPTS_VALUE && pts >= 0) ++source_audio_packets;
            else ++priming_packets;
        }
        av_packet_unref(source_packet);
    }
    av_packet_free(&source_packet);
    avformat_close_input(&audio_format);
    CHECK(priming_packets > 0); // fixture exercises the AAC-origin regression
    CHECK(audio_packets == source_audio_packets);

    for (const auto& path : {first, second, audio_source, output})
        std::filesystem::remove(path, cleanup_error);
}

TEST_CASE("Audio-aware assembly refuses a second audio owner and a silent audio source") {
    const auto root = std::filesystem::temp_directory_path();
    const auto with_audio = root / "chronon3d_anchor_audio_child.mp4";
    const auto silent = root / "chronon3d_anchor_silent_child.mp4";
    const auto audio_source = root / "chronon3d_anchor_audio_source2.m4a";
    const auto output = root / "chronon3d_anchor_refused.mp4";
    std::error_code cleanup_error;
    for (const auto& path : {with_audio, silent, audio_source, output})
        std::filesystem::remove(path, cleanup_error);

    const auto media = "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i color=c=red:s=64x64:r=10:d=1 "
        "-f lavfi -i sine=frequency=440:sample_rate=48000:duration=0.5 "
        "-c:v libx264 -bf 0 -g 10 -c:a aac ";
    REQUIRE(std::system((media + with_audio.string()).c_str()) == 0);
    {
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(p0_5a_pipe_options(silent.string())));
        for (int index = 0; index < 10; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
    }
    REQUIRE(std::system(("ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i sine=frequency=440:sample_rate=48000:duration=1 "
        "-c:a aac " + audio_source.string()).c_str()) == 0);

    // One artifact carries exactly ONE audio owner: audio-bearing segments
    // plus an anchor audio source must fail closed, never double-mux.
    {
        chronon3d::media::SegmentAssemblyRequest request;
        request.input_paths = {with_audio.string()};
        request.output_path = output.string();
        request.audio_source_path = audio_source.string();
        const auto result = chronon3d::media::assemble_segments(request);
        CHECK_FALSE(result.success);
        CHECK_FALSE(result.reason.empty());
    }
    // An "audio source" without an audio stream is a typed refusal too.
    {
        chronon3d::media::SegmentAssemblyRequest request;
        request.input_paths = {silent.string()};
        request.output_path = output.string();
        request.audio_source_path = silent.string();
        const auto result = chronon3d::media::assemble_segments(request);
        CHECK_FALSE(result.success);
        CHECK_FALSE(result.reason.empty());
    }

    for (const auto& path : {with_audio, silent, audio_source, output})
        std::filesystem::remove(path, cleanup_error);
}

// P0-5C — the encoder can write through a CALLER-OWNED MuxSession.
//
// The direct-mux lane needs one container for copied packets and re-encoded
// packets alike. When the container is borrowed the encoder must flush only
// its codec: finalizing it (or writing audio) after the first re-encoded GOP
// would close the container and drop every later work unit of the schedule.
TEST_CASE("P0-5C: an encoder writes through a caller-owned MuxSession") {
    const auto source = std::filesystem::temp_directory_path() /
        "chronon3d_p0_5c_source.mp4";
    const auto output = std::filesystem::temp_directory_path() /
        "chronon3d_p0_5c_shared_mux.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(source, cleanup_error);
    std::filesystem::remove(output, cleanup_error);

    // The compressed SOURCE whose stream the copied GOPs carry. The re-encode
    // lane must reproduce exactly this stream, which is why the caller declares
    // the borrowed container from it (open_source_declared_mux) instead of
    // inventing a codec context of its own: a declaration that does not match
    // the packets already in the container produces a file a decoder cannot
    // follow across the splice.
    {
        chronon3d::cli::NativeAvEncoder writer;
        REQUIRE(writer.open(p0_5a_pipe_options(source.string())));
        for (int index = 0; index < 10; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(writer.write_frame(frame));
        }
        REQUIRE(writer.close());
    }

    const auto declared =
        chronon3d::cli::make_remux_target_contract(source.string());
    REQUIRE(declared.has_value());

    chronon3d::media::MuxSession shared_mux;
    const auto declaration = chronon3d::cli::open_source_declared_mux(
        source.string(), output.string(), shared_mux, /*include_audio=*/false);
    REQUIRE(declaration.has_value());

    chronon3d::cli::NativeAvEncoder encoder;
    auto options = p0_5a_pipe_options(output.string());
    options.borrowed_mux = &shared_mux;
    options.borrowed_declared_stream = *declared;
    REQUIRE(encoder.open(options));
    CHECK(encoder.borrows_caller_mux());

    chronon3d::Framebuffer frame(64, 64);
    REQUIRE(encoder.write_frame(frame));

    // close() must flush the encoder, certify that it reproduced the declared
    // stream, and leave the container open.
    REQUIRE(encoder.close());
    CHECK(encoder.borrowed_stream_reproduced());
    CHECK_FALSE(encoder.borrows_caller_mux());

    // The caller finalizes exactly once, after every work unit.
    REQUIRE(shared_mux.finalize());
    std::filesystem::remove(source, cleanup_error);

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
