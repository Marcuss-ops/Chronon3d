#include <doctest/doctest.h>

#include <chronon3d/media/video/media_probe.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

using chronon3d::media::video::MediaProbeErrorCode;
using chronon3d::media::video::MediaStreamKind;
using chronon3d::media::video::MediaStreamProbe;
using chronon3d::media::video::probe_media;

std::string temp_path(const char* suffix) {
    static std::atomic<unsigned> counter{0};
    return "/tmp/chronon3d_media_probe_" +
           std::to_string(counter.fetch_add(1)) + "_" + suffix;
}

bool ffmpeg_available() {
    return std::system("ffmpeg -version >/dev/null 2>&1") == 0;
}

} // namespace

TEST_CASE("probe_media maps backend/open failures explicitly") {
    const auto result = probe_media(
        "/tmp/chronon3d_media_probe_definitely_missing.mp4");
    REQUIRE_FALSE(result);

#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    CHECK(result.error().code == MediaProbeErrorCode::OpenInput);
    CHECK(result.error().native_code < 0);
#else
    CHECK(result.error().code == MediaProbeErrorCode::BackendUnavailable);
    CHECK(result.error().native_code == 0);
#endif
    CHECK_FALSE(result.error().message.empty());
}

#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
TEST_CASE("probe_media exposes canonical container and stream metadata") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg CLI required to create probe fixture");
        return;
    }

    const auto artifact = temp_path("av.mp4");
    const std::string make =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i color=c=black:s=64x48:r=30000/1001 "
        "-f lavfi -i sine=frequency=440:sample_rate=48000 "
        "-t 1 -c:v mpeg4 -pix_fmt yuv420p -c:a aac -shortest " + artifact;
    REQUIRE(std::system(make.c_str()) == 0);

    auto result = probe_media(artifact);
    REQUIRE(result);
    const auto& info = result.value();

    CHECK_FALSE(info.format_name.empty());
    REQUIRE(info.duration.has_value());
    CHECK(info.duration->ticks() > 0);
    CHECK(info.duration->time_base.numerator > 0);
    CHECK(info.duration->time_base.denominator > 0);

    const MediaStreamProbe* video = nullptr;
    const MediaStreamProbe* audio = nullptr;
    std::size_t video_count = 0;
    std::size_t audio_count = 0;
    for (const auto& stream : info.streams) {
        if (stream.kind == MediaStreamKind::Video) {
            ++video_count;
            if (video == nullptr) video = &stream;
        } else if (stream.kind == MediaStreamKind::Audio) {
            ++audio_count;
            if (audio == nullptr) audio = &stream;
        }
    }

    REQUIRE(video_count == 1);
    REQUIRE(audio_count == 1);
    REQUIRE(video != nullptr);
    REQUIRE(audio != nullptr);

    CHECK(video->index >= 0);
    CHECK(video->codec == "mpeg4");
    CHECK(video->width == 64);
    CHECK(video->height == 48);
    CHECK(video->pixel_format == "yuv420p");
    CHECK(video->time_base.numerator > 0);
    CHECK(video->time_base.denominator > 0);
    CHECK(video->frame_rate.numerator > 0);
    CHECK(video->frame_rate.denominator > 0);

    CHECK(audio->index >= 0);
    CHECK(audio->codec == "aac");
    CHECK(audio->sample_rate == 48000);
    CHECK(audio->channels == 1);
    CHECK(audio->time_base.numerator > 0);
    CHECK(audio->time_base.denominator > 0);

    std::error_code ec;
    std::filesystem::remove(artifact, ec);
}
#endif
