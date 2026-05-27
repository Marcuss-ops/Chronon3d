#pragma once

#include <magic_enum/magic_enum.hpp>

#include <cctype>
#include <string>
#include <string_view>

namespace chronon3d::enum_utils {

inline std::string to_lower_snake(std::string_view value) {
    std::string out;
    out.reserve(value.size() * 2);
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (std::isupper(c)) {
            if (i > 0) {
                out.push_back('_');
            }
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

inline std::string to_upper_snake(std::string_view value) {
    std::string out = to_lower_snake(value);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

template <typename E>
inline std::string enum_name_lower_snake(E value) {
    return to_lower_snake(magic_enum::enum_name(value));
}

template <typename E>
inline std::string enum_name_upper_snake(E value) {
    return to_upper_snake(magic_enum::enum_name(value));
}

template <typename E>
inline std::string enum_name_lower(E value) {
    return std::string(magic_enum::enum_name(value));
}

template <typename E>
inline std::string_view enum_name_exact(E value) {
    return magic_enum::enum_name(value);
}

} // namespace chronon3d::enum_utils
