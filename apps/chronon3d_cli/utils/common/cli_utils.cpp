#include "cli_utils.hpp"
#include <fmt/format.h>
#include <algorithm>

namespace chronon3d {
namespace cli {

namespace {
    bool is_digits(const std::string& s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
    }
}

ParseFrameRangeResult parse_frames_safe(const std::string& s) {
    if (s.empty()) return {false, {}, "Empty frame range string"};
    
    ParseFrameRangeResult res;
    res.ok = true;

    auto dash_pos = s.find('-');
    if (dash_pos == std::string::npos) {
        if (!is_digits(s)) return {false, {}, fmt::format("Invalid frame number: '{}'", s)};
        try {
            res.value.start = res.value.end = std::stoll(s);
        } catch (...) {
            return {false, {}, fmt::format("Frame number too large: '{}'", s)};
        }
    } else {
        std::string start_s = s.substr(0, dash_pos);
        if (!is_digits(start_s)) return {false, {}, fmt::format("Invalid start frame: '{}'", start_s)};
        
        std::string rest = s.substr(dash_pos + 1);
        if (rest.empty()) return {false, {}, "Missing end frame after '-'"};

        auto x_pos = rest.find('x');
        std::string end_s = (x_pos == std::string::npos) ? rest : rest.substr(0, x_pos);
        if (!is_digits(end_s)) return {false, {}, fmt::format("Invalid end frame: '{}'", end_s)};

        try {
            res.value.start = std::stoll(start_s);
            res.value.end = std::stoll(end_s);
            
            if (res.value.end < res.value.start) {
                return {false, {}, fmt::format("End frame ({}) is before start frame ({})", res.value.end, res.value.start)};
            }

            if (x_pos != std::string::npos) {
                std::string step_s = rest.substr(x_pos + 1);
                if (!is_digits(step_s)) return {false, {}, fmt::format("Invalid step: '{}'", step_s)};
                res.value.step = std::stoll(step_s);
                if (res.value.step <= 0) return {false, {}, fmt::format("Step must be positive, got {}", res.value.step)};
            }
        } catch (...) {
            return {false, {}, "Frame number or step too large"};
        }
    }

    return res;
}

FrameRange parse_frames(const std::string& s) {
    auto res = parse_frames_safe(s);
    return res.value;
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
