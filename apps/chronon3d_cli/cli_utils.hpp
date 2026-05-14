#pragma once

#include <string>
#include <cstdint>

namespace chronon3d {
namespace cli {

struct FrameRange {
    int64_t start{0};
    int64_t end{0};
    int64_t step{1};
};

FrameRange parse_frames(const std::string& s);
std::string format_path(const std::string& pattern, int64_t frame, bool is_range = false);

} // namespace cli
} // namespace chronon3d
