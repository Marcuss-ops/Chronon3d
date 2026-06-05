#pragma once

#include <string>
#include <algorithm>
#include <cctype>

namespace chronon3d {

/// Returns a lowercased copy of @p s.
inline std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace chronon3d
