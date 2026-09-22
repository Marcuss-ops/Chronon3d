// Regression contract: a native NVENC frame after earlier frames must match
// the same frame rendered in isolation (within independent H.264 quantization).

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef CHRONON3D_SOURCE_DIR
#define CHRONON3D_SOURCE_DIR "."
#endif

namespace {

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    quoted += "'";
    return quoted;
}

std::optional<std::filesystem::path> cli_path() {
    if (const char* configured = std::getenv("CHRONON3D_CLI_BINARY");
        configured && *configured && std::filesystem::is_regular_file(configured)) {
        return std::filesystem::path(configured);
    }
    const auto root = std::filesystem::path(CHRONON3D_SOURCE_DIR);
    for (const auto& relative : {
             "build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli",
             "build/chronon/linux-content-dev/chronon3d_cli",
             "build/chronon3d_cli"}) {
        const auto candidate = root / relative;
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return std::nullopt;
}

int render_native_mp4(const std::filesystem::path& cli,
                      const std::filesystem::path& plan,
                      const std::filesystem::path& output,
                      int first, int last) {
    std::ostringstream command;
    command << "timeout 60 " << shell_quote(cli.string())
            << " render --plan " << shell_quote(plan.string())
            << " --assets-root " << shell_quote(CHRONON3D_SOURCE_DIR)
            << " --backend vulkan --hardware nvenc"
            << " --gpu-hot-path-mode require_gpu_native --codec h264"
            << " --start-frame " << first << " --end-frame " << last
            << " -o " << shell_quote(output.string());
    return std::system(command.str().c_str());
}

int decode_frame_rgba(const std::filesystem::path& input,
                      const std::filesystem::path& output,
                      int frame_index) {
    std::ostringstream command;
    command << "ffmpeg -v error -i " << shell_quote(input.string())
            << " -vf 'select=eq(n\\," << frame_index << ")' -frames:v 1"
            << " -pix_fmt rgba -f rawvideo " << shell_quote(output.string())
            << " -y";
    return std::system(command.str().c_str());
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto length = input.tellg();
    if (length < 0) return {};
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) return {};
    return bytes;
}

struct PixelComparison {
    double mean_absolute_error{};
    std::size_t bright_pixels{};
};

PixelComparison compare_rgba(const std::vector<unsigned char>& a,
                             const std::vector<unsigned char>& b) {
    PixelComparison result;
    if (a.size() != b.size() || a.size() % 4 != 0) {
        result.mean_absolute_error = 1.0;
        return result;
    }
    std::uint64_t error_sum = 0;
    std::size_t samples = 0;
    for (std::size_t i = 0; i < a.size(); i += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            error_sum += static_cast<std::uint64_t>(std::abs(
                static_cast<int>(a[i + channel]) -
                static_cast<int>(b[i + channel])));
            ++samples;
        }
        if (a[i] >= 192 && a[i + 1] >= 192 && a[i + 2] >= 192 &&
            b[i] >= 192 && b[i + 1] >= 192 && b[i + 2] >= 192) {
            ++result.bright_pixels;
        }
    }
    result.mean_absolute_error = samples
        ? static_cast<double>(error_sum) / (255.0 * static_cast<double>(samples))
        : 1.0;
    return result;
}

}  // namespace

TEST_CASE("Text surfaces: E1 frames 8 and 48 match cold and warm renders") {
    const auto cli = cli_path();
    REQUIRE_MESSAGE(cli.has_value(),
                    "chronon3d_cli not found; set CHRONON3D_CLI_BINARY");
    REQUIRE_MESSAGE(std::system("command -v ffmpeg >/dev/null 2>&1") == 0,
                    "ffmpeg is required to decode the native NVENC frames");

    const auto root = std::filesystem::path(CHRONON3D_SOURCE_DIR);
    const auto plan = root / "tests/fixtures/3d_traps/E1_text_surface_reuse.plan.json";
    REQUIRE(std::filesystem::is_regular_file(plan));
    const auto temp = std::filesystem::temp_directory_path() /
        "chronon3d_text_warm_cold_nvenc";
    std::filesystem::create_directories(temp);
    const auto cold_mp4 = temp / "cold-frame48.mp4";
    const auto cold_peak_mp4 = temp / "cold-frame8.mp4";
    const auto warm_mp4 = temp / "warm-frames0-48.mp4";
    const auto cold_rgba = temp / "cold-frame48.rgba";
    const auto cold_peak_rgba = temp / "cold-frame8.rgba";
    const auto warm_rgba = temp / "warm-frame48.rgba";
    const auto warm_peak_rgba = temp / "warm-frame8.rgba";
    for (const auto& path : {cold_mp4, cold_peak_mp4, warm_mp4, cold_rgba,
                             cold_peak_rgba, warm_rgba, warm_peak_rgba}) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    REQUIRE(render_native_mp4(*cli, plan, cold_mp4, 48, 48) == 0);
    REQUIRE(render_native_mp4(*cli, plan, cold_peak_mp4, 8, 8) == 0);
    REQUIRE(render_native_mp4(*cli, plan, warm_mp4, 0, 48) == 0);
    REQUIRE(decode_frame_rgba(cold_mp4, cold_rgba, 0) == 0);
    REQUIRE(decode_frame_rgba(cold_peak_mp4, cold_peak_rgba, 0) == 0);
    REQUIRE(decode_frame_rgba(warm_mp4, warm_rgba, 48) == 0);
    REQUIRE(decode_frame_rgba(warm_mp4, warm_peak_rgba, 8) == 0);

    constexpr std::size_t kRgbaFrameBytes = 1920u * 1080u * 4u;
    const auto cold = read_bytes(cold_rgba);
    const auto cold_peak = read_bytes(cold_peak_rgba);
    const auto warm = read_bytes(warm_rgba);
    const auto warm_peak = read_bytes(warm_peak_rgba);
    REQUIRE(cold.size() == kRgbaFrameBytes);
    REQUIRE(cold_peak.size() == kRgbaFrameBytes);
    REQUIRE(warm.size() == kRgbaFrameBytes);
    REQUIRE(warm_peak.size() == kRgbaFrameBytes);

    const auto comparison = compare_rgba(cold, warm);
    INFO("decoded RGBA mean absolute error=" << comparison.mean_absolute_error);
    CHECK(comparison.bright_pixels >= 1000);
    CHECK(comparison.mean_absolute_error <= 0.01);

    const auto peak_comparison = compare_rgba(cold_peak, warm_peak);
    INFO("decoded RGBA mean absolute error at peak-motion frame 8="
         << peak_comparison.mean_absolute_error);
    CHECK(peak_comparison.bright_pixels >= 1000);
    CHECK(peak_comparison.mean_absolute_error <= 0.01);
}
