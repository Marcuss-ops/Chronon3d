#include "test_packet_assembler_contract_test_support.hpp"

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

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
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
#endif

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
