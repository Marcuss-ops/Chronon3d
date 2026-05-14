#include "ffmpeg_runner.hpp"
#include <cstdlib>
#include <fmt/format.h>

namespace chronon3d {
namespace cli {

bool ffmpeg_available() {
#ifdef _WIN32
    return std::system("ffmpeg -version >nul 2>nul") == 0;
#else
    return std::system("ffmpeg -version >/dev/null 2>/dev/null") == 0;
#endif
}

int run_ffmpeg_encode(const std::string& frame_pattern, const std::string& output,
                      int fps, int crf, const std::string& preset) {
    const std::string cmd = fmt::format(
        "ffmpeg -y -framerate {} -i \"{}\" -c:v libx264 -pix_fmt yuv420p "
        "-crf {} -preset {} -movflags +faststart \"{}\"",
        fps, frame_pattern, crf, preset, output);
    return std::system(cmd.c_str());
}

} // namespace cli
} // namespace chronon3d
