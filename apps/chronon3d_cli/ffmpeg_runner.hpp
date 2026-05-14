#pragma once

#include <string>

namespace chronon3d {
namespace cli {

bool ffmpeg_available();
int run_ffmpeg_encode(const std::string& frame_pattern, const std::string& output,
                      int fps, int crf, const std::string& preset);

} // namespace cli
} // namespace chronon3d
