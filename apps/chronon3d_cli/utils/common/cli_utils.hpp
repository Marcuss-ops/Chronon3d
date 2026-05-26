#pragma once

#include <filesystem>
#include <string>
#include <cstdint>
#include <string_view>

namespace chronon3d {
namespace cli {

struct FrameRange {
    int64_t start{0};
    int64_t end{0};
    int64_t step{1};
};

struct ParseFrameRangeResult {
    bool ok{false};
    FrameRange value{};
    std::string error;
};

FrameRange parse_frames(const std::string& s); // Keep for compatibility
ParseFrameRangeResult parse_frames_safe(const std::string& s);
std::string format_path(const std::string& pattern, int64_t frame, bool is_range = false);
std::filesystem::path chronon_artifacts_root();
std::filesystem::path chronon_artifact_path(std::string_view category, std::string_view filename);

} // namespace cli
} // namespace chronon3d
