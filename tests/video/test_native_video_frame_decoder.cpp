// Regression coverage for the native FFmpeg decoder's per-source locking.
// Two stateful AVCodecContext instances must be usable concurrently; a
// decoder-wide lock around seek/decode would serialize this path.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>

#ifdef _WIN32
#  include <process.h>
#  define CHRONON3D_GETPID _getpid
#else
#  include <unistd.h>
#  define CHRONON3D_GETPID getpid
#endif

namespace {

struct FixtureDirectory {
    FixtureDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("chronon3d-native-decoder-" + std::to_string(CHRONON3D_GETPID()))) {
        std::filesystem::create_directories(path);
    }
    ~FixtureDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::filesystem::path path;
};

void write_y4m(const std::filesystem::path& path, std::array<unsigned char, 6> pixels) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "YUV4MPEG2 W2 H2 F30:1 Ip A1:1 C420jpeg\nFRAME\n";
    output.write(reinterpret_cast<const char*>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size()));
    REQUIRE(output.good());
}

} // namespace

TEST_CASE("NativeVideoFrameDecoder decodes independent sources concurrently") {
    FixtureDirectory fixtures;
    const auto source_a = fixtures.path / "a.y4m";
    const auto source_b = fixtures.path / "b.y4m";

    // Red-biased and blue-biased 2x2 YUV420 frames. The contents differ so a
    // future accidental shared decode buffer cannot pass the assertions.
    write_y4m(source_a, {76, 76, 76, 76, 84, 255});
    write_y4m(source_b, {29, 29, 29, 29, 255, 107});

    chronon3d::media::NativeVideoFrameDecoder decoder;
    auto decode = [&decoder](const std::filesystem::path& source) {
        return decoder.decode_frame(source.string(), chronon3d::Frame{0}, 2, 2, 30.0f);
    };

    auto future_a = std::async(std::launch::async, decode, source_a);
    auto future_b = std::async(std::launch::async, decode, source_b);
    const auto frame_a = future_a.get();
    const auto frame_b = future_b.get();

    REQUIRE(frame_a);
    REQUIRE(frame_b);
    CHECK(frame_a->width() == 2);
    CHECK(frame_a->height() == 2);
    CHECK(frame_b->width() == 2);
    CHECK(frame_b->height() == 2);
    CHECK(frame_a->get_pixel(0, 0).r != doctest::Approx(frame_b->get_pixel(0, 0).r));
}

