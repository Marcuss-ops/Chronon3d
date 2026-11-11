// Regression contract: a native NVENC frame after earlier frames must match
// the same frame rendered in isolation (within independent H.264 quantization).

#include <doctest/doctest.h>

#include <algorithm>
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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#endif

#ifndef CHRONON3D_SOURCE_DIR
#define CHRONON3D_SOURCE_DIR "."
#endif

namespace {

#ifndef _WIN32
std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    quoted += "'";
    return quoted;
}
#endif

#ifdef _WIN32
std::wstring widen(const std::string& value) {
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1,
                                              nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(),
                            required) <= 0) return {};
    result.pop_back();
    return result;
}

std::wstring quote_windows_argument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(ch);
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
#endif

int run_command(const std::vector<std::string>& arguments,
                const std::optional<std::filesystem::path>& output = std::nullopt,
                unsigned timeout_ms = 60000) {
    if (arguments.empty()) return 127;
#ifdef _WIN32
    std::wstring command_line;
    for (const auto& argument : arguments) {
        if (!command_line.empty()) command_line.push_back(L' ');
        const auto wide_argument = widen(argument);
        if (wide_argument.empty() && !argument.empty()) return 127;
        command_line += quote_windows_argument(wide_argument);
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    HANDLE output_file = INVALID_HANDLE_VALUE;
    if (output) {
        const auto wide_path = widen(output->string());
        if (wide_path.empty()) return 127;
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        output_file = CreateFileW(wide_path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output_file == INVALID_HANDLE_VALUE) return 127;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (output_file != INVALID_HANDLE_VALUE) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = output_file;
        startup.hStdError = output_file;
    }
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, mutable_command.data(), nullptr,
        nullptr, output_file != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process);
    if (output_file != INVALID_HANDLE_VALUE) CloseHandle(output_file);
    if (!started) return 127;

    const DWORD wait_result = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    DWORD exit_code = 127;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return wait_result == WAIT_TIMEOUT ? 124 : static_cast<int>(exit_code);
#else
    std::ostringstream command;
    command << "timeout " << (timeout_ms / 1000) << ' ';
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i != 0) command << ' ';
        command << shell_quote(arguments[i]);
    }
    if (output) command << " > " << shell_quote(output->string()) << " 2>&1";
    return std::system(command.str().c_str());
#endif
}

std::optional<std::filesystem::path> cli_path() {
    if (const char* configured = std::getenv("CHRONON3D_CLI_BINARY");
        configured && *configured && std::filesystem::is_regular_file(configured)) {
        return std::filesystem::path(configured);
    }
    const auto root = std::filesystem::path(CHRONON3D_SOURCE_DIR);
#ifdef _WIN32
    for (const auto& relative : {
             "build/chronon/win-release/apps/chronon3d_cli/chronon3d_cli.exe",
             "build/chronon/win-release-clang/apps/chronon3d_cli/chronon3d_cli.exe"}) {
#else
    for (const auto& relative : {
             "build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli",
             "build/chronon/linux-content-dev/chronon3d_cli",
             "build/chronon3d_cli"}) {
#endif
        const auto candidate = root / relative;
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return std::nullopt;
}

int render_native_mp4(const std::filesystem::path& cli,
                      const std::filesystem::path& plan,
                      const std::filesystem::path& output,
                      int first, int last) {
    return run_command({cli.string(), "render", "--plan", plan.string(),
        "--assets-root", CHRONON3D_SOURCE_DIR, "--backend", "vulkan",
        "--hardware", "nvenc", "--gpu-hot-path-mode", "require_gpu_native",
        "--codec", "h264", "--start-frame", std::to_string(first),
        "--end-frame", std::to_string(last), "-o", output.string()});
}

int decode_frame_rgba(const std::filesystem::path& input,
                      const std::filesystem::path& output,
                      int frame_index) {
    return run_command({"ffmpeg", "-v", "error", "-i", input.string(),
        "-vf", "select=eq(n\\," + std::to_string(frame_index) + ")",
        "-frames:v", "1", "-pix_fmt", "rgba", "-f", "rawvideo",
        output.string(), "-y"});
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
    std::size_t visible_pixels{};
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
        constexpr unsigned char kVisibleThreshold = 64;
        const auto a_peak = std::max({a[i], a[i + 1], a[i + 2]});
        const auto b_peak = std::max({b[i], b[i + 1], b[i + 2]});
        if (a_peak >= kVisibleThreshold && b_peak >= kVisibleThreshold) {
            ++result.visible_pixels;
        }
    }
    result.mean_absolute_error = samples
        ? static_cast<double>(error_sum) / (255.0 * static_cast<double>(samples))
        : 1.0;
    return result;
}

}  // namespace

TEST_CASE("Typed native shutter submits each sample before encoder handoff") {
    const auto cli = cli_path();
    REQUIRE_MESSAGE(cli.has_value(), "chronon3d_cli not found; set CHRONON3D_CLI_BINARY");
    REQUIRE_MESSAGE(run_command({"ffmpeg", "-version"}) == 0,
                    "ffmpeg is required to decode the native NVENC frame");
    const auto root = std::filesystem::path(CHRONON3D_SOURCE_DIR);
    const auto dir = root / "build/typed-native-shutter-regression";
    std::filesystem::create_directories(dir);
    const auto plan = dir / "plan.json";
    const auto video = dir / "frame.mp4";
    const auto raw = dir / "frame.rgba";
    const auto log = dir / "render.log";
    {
        std::ofstream file(plan);
        REQUIRE(file);
        file << R"({"schema":"chronon.render-plan.v3","version":3,
          "job_id":"native-shutter-solid",
          "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":3},
          "layers":[{"id":"background","type":"color","color":[0.8,0.2,0.1,1.0],
                     "size":[320,180],"start_frame":0,"duration_frames":3}],
          "temporal_motion_blur":{"shutter_angle":180.0,"samples":8},
          "output":{"path":"frame.mp4","format":"mp4"}})";
        REQUIRE(file.good());
    }
#ifdef _WIN32
    _putenv_s("CHRONON3D_VULKAN_VALIDATION", "1");
    _putenv_s("VK_INSTANCE_LAYERS", "VK_LAYER_KHRONOS_validation");
#else
    setenv("CHRONON3D_VULKAN_VALIDATION", "1", 1);
    setenv("VK_INSTANCE_LAYERS", "VK_LAYER_KHRONOS_validation", 1);
#endif
    REQUIRE(run_command({cli->string(), "render", "--plan", plan.string(),
        "--assets-root", root.string(), "--backend", "vulkan",
        "--gpu-hot-path-mode", "require_gpu_native", "--hardware", "nvenc",
        "--encoder-backend", "native", "--start-frame", "1", "--end-frame",
        "1", "-o", video.string()}, log) == 0);
    const auto log_bytes = read_bytes(log);
    REQUIRE_FALSE(log_bytes.empty());
    const std::string log_text(log_bytes.begin(), log_bytes.end());
    const bool validation_enabled = log_text.find(
        "Enabled validation layer: VK_LAYER_KHRONOS_validation") != std::string::npos;
    const bool validation_unavailable = log_text.find(
        "Validation layer 'VK_LAYER_KHRONOS_validation' requested but not found")
        != std::string::npos;
    CHECK((validation_enabled || validation_unavailable));
    CHECK(log_text.find("Vulkan Validation ERROR") == std::string::npos);
    REQUIRE(decode_frame_rgba(video, raw, 0) == 0);
    const auto pixels = read_bytes(raw);
    REQUIRE(pixels.size() == 320u * 180u * 4u);
    std::uint64_t red_sum = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) red_sum += pixels[i];
    CHECK(red_sum > 20u * 320u * 180u);
}

TEST_CASE("Text surfaces: E1 frames 8 and 48 match cold and warm renders") {
    const auto cli = cli_path();
    REQUIRE_MESSAGE(cli.has_value(),
                    "chronon3d_cli not found; set CHRONON3D_CLI_BINARY");
    REQUIRE_MESSAGE(run_command({"ffmpeg", "-version"}) == 0,
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
    CHECK(comparison.visible_pixels >= 100);
    CHECK(comparison.mean_absolute_error <= 0.01);

    const auto peak_comparison = compare_rgba(cold_peak, warm_peak);
    INFO("decoded RGBA mean absolute error at peak-motion frame 8="
         << peak_comparison.mean_absolute_error);
    CHECK(peak_comparison.mean_absolute_error <= 0.01);
}
