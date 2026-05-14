#include "cli_utils.hpp"
#include <fmt/format.h>

namespace chronon3d {
namespace cli {

FrameRange parse_frames(const std::string& s) {
    FrameRange r;
    if (auto pos = s.find('-'); pos == std::string::npos) {
        r.start = r.end = std::stoll(s);
    } else {
        r.start = std::stoll(s.substr(0, pos));
        auto rest = s.substr(pos + 1);
        if (auto x = rest.find('x'); x != std::string::npos) {
            r.end = std::stoll(rest.substr(0, x));
            r.step = std::stoll(rest.substr(x + 1));
        } else {
            r.end = std::stoll(rest);
        }
    }
    return r;
}

std::string format_path(const std::string& pattern, int64_t frame, bool is_range) {
    std::string s = pattern;
    std::string replacement = fmt::format("{:04d}", frame);
    size_t pos = s.find("####");
    if (pos != std::string::npos) {
        s.replace(pos, 4, replacement);
    } else if (is_range) {
        size_t dot_pos = s.find_last_of('.');
        if (dot_pos != std::string::npos) {
            s.insert(dot_pos, "_" + replacement);
        } else {
            s += "_" + replacement;
        }
    }
    return s;
}

} // namespace cli
} // namespace chronon3d
