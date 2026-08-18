#include <doctest/doctest.h>

#include <chronon3d/core/cancellation_token.hpp>
#include "src/media/video/mux_plan.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using chronon3d::media::video::MuxAudioTrack;
using chronon3d::media::video::MuxError;
using chronon3d::media::video::MuxErrorCode;
using chronon3d::media::video::MuxPlan;

std::string describe_mux_error(const MuxError& error) {
    std::ostringstream out;
    out << "code=" << static_cast<int>(error.code)
        << " exit=" << error.process_exit_code
        << " message=\"" << error.message << "\"";
    return out.str();
}

std::string temp_path(const char* suffix) {
    static std::atomic<unsigned> counter{0};
    return "/tmp/chronon3d_mux_" + std::to_string(counter.fetch_add(1)) + "_" + suffix;
}

bool ffmpeg_available() {
    return std::system("ffmpeg -version >/dev/null 2>&1") == 0 &&
           std::system("ffprobe -version >/dev/null 2>&1") == 0;
}

struct Fixture {
    std::string video{temp_path("video.mp4")};
    std::string audio{temp_path("audio.wav")};
    std::string output{temp_path("output.mp4")};

    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove(video, ec);
        std::filesystem::remove(audio, ec);
        std::filesystem::remove(output, ec);
        std::filesystem::remove(output + ".chronon.audio.partial.mp4", ec);
        std::filesystem::remove(output + ".chronon.audio.partial.mp4.probe.streams.txt", ec);
    }

    bool make() const {
        const std::string video_command =
            "ffmpeg -hide_banner -loglevel error -y "
            "-f lavfi -i color=c=black:s=32x32:d=0.4 "
            "-c:v mpeg4 -pix_fmt yuv420p " + video;
        const std::string audio_command =
            "ffmpeg -hide_banner -loglevel error -y "
            "-f lavfi -i sine=frequency=440:duration=0.4 "
            "-c:a pcm_s16le " + audio;
        return std::system(video_command.c_str()) == 0 &&
               std::system(audio_command.c_str()) == 0;
    }
};

MuxPlan plan_for(const Fixture& fixture) {
    MuxPlan plan;
    plan.video_input = fixture.video;
    plan.output = fixture.output;
    plan.tracks.push_back(MuxAudioTrack{
        .source = fixture.audio,
        .volume = 0.5,
        .duration_seconds = 0.4,
        .role = "transition_sfx",
    });
    plan.process_timeout = std::chrono::seconds(20);
    plan.graceful_cancel_timeout = std::chrono::seconds(2);
    return plan;
}

}  // namespace

TEST_CASE("MuxPlan rejects missing inputs before launching FFmpeg") {
    MuxPlan plan;
    plan.video_input = "/tmp/chronon3d_missing_video.mp4";
    plan.output = temp_path("missing_output.mp4");
    plan.tracks.push_back(MuxAudioTrack{.source = "/tmp/missing_audio.wav"});

    const auto result = chronon3d::media::video::ExternalAudioMuxer{}.run(plan);
    if (!result) {
        INFO("mux error: " << describe_mux_error(result.error()));
    }
    REQUIRE_FALSE(result);
    CHECK(result.error().code == MuxErrorCode::InputMissing);
}

TEST_CASE("MuxPlan rejects an existing output when overwrite is disabled") {
    const auto video = temp_path("existing_video.mp4");
    const auto output = temp_path("existing_output.mp4");
    std::ofstream(video).put('x');
    std::ofstream(output).put('x');
    MuxPlan plan;
    plan.video_input = video;
    plan.output = output;
    plan.overwrite = false;
    plan.tracks.push_back(MuxAudioTrack{.source = video});

    const auto result = chronon3d::media::video::ExternalAudioMuxer{}.run(plan);
    if (!result) {
        INFO("mux error: " << describe_mux_error(result.error()));
    }
    REQUIRE_FALSE(result);
    CHECK(result.error().code == MuxErrorCode::OutputExists);

    std::error_code ec;
    std::filesystem::remove(video, ec);
    std::filesystem::remove(output, ec);
}

TEST_CASE("MuxPlan rejects an empty track list") {
    const auto video = temp_path("empty_tracks.mp4");
    std::ofstream(video).put('x');
    MuxPlan plan;
    plan.video_input = video;
    plan.output = temp_path("empty_tracks_output.mp4");

    const auto result = chronon3d::media::video::ExternalAudioMuxer{}.run(plan);
    if (!result) {
        INFO("mux error: " << describe_mux_error(result.error()));
    }
    REQUIRE_FALSE(result);
    CHECK(result.error().code == MuxErrorCode::InvalidPlan);

    std::error_code ec;
    std::filesystem::remove(video, ec);
    std::filesystem::remove(plan.output, ec);
}

TEST_CASE("ExternalAudioMuxer produces and verifies one mixed audio stream") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg and ffprobe are required");
        return;
    }
    Fixture fixture;
    REQUIRE(fixture.make());

    const auto result = chronon3d::media::video::ExternalAudioMuxer{}.run(
        plan_for(fixture));
    if (!result) {
        INFO("mux error: " << describe_mux_error(result.error()));
    }
    REQUIRE(result);
    CHECK(result->output == fixture.output);
    CHECK(result->audio_track_count == 1);
    CHECK(std::filesystem::is_regular_file(fixture.output));
    CHECK(std::filesystem::file_size(fixture.output) > 0);

    const std::string probe =
        "ffprobe -v error -select_streams a -show_entries stream=codec_type "
        "-of default=nw=1:nk=1 " + fixture.output;
    CHECK(std::system((probe + " | grep -qx audio").c_str()) == 0);
}

TEST_CASE("ExternalAudioMuxer parses ffprobe stream facts (ffprobe 4.4 -o regression)") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg and ffprobe are required");
        return;
    }
    Fixture fixture;
    REQUIRE(fixture.make());

    const auto result = chronon3d::media::video::ExternalAudioMuxer{}.run(
        plan_for(fixture));
    if (!result) {
        INFO("mux error: " << describe_mux_error(result.error()));
    }
    REQUIRE(result);

    // verify_muxed_artifact returns the parsed video duration on success.
    // On ffprobe 4.4 the removed `-o` option made the probe write nothing,
    // so verification failed with VerificationFailed (empty stream facts)
    // instead of producing a positive, matching duration.
    CHECK(result->duration_seconds > 0.0);
    CHECK(std::abs(result->duration_seconds - 0.4) < 0.2);
}

TEST_CASE("ExternalAudioMuxer honours cancellation and leaves no partial artifact") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg and ffprobe are required");
        return;
    }
    Fixture fixture;
    REQUIRE(fixture.make());
    auto plan = plan_for(fixture);
    chronon3d::CancellationToken cancellation;
    cancellation.cancel();

    const auto result = chronon3d::media::video::ExternalAudioMuxer{}.run(
        plan, &cancellation);
    if (!result) {
        INFO("mux error: " << describe_mux_error(result.error()));
    }
    REQUIRE_FALSE(result);
    CHECK(result.error().code == MuxErrorCode::Cancelled);
    CHECK_FALSE(std::filesystem::exists(fixture.output));
    CHECK_FALSE(std::filesystem::exists(fixture.output + ".chronon.audio.partial.mp4"));
}
