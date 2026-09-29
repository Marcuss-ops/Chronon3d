#pragma once

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::render_plan::detail {

template <typename Enum, std::size_t Size>
[[nodiscard]] std::optional<Enum> find_enum_value(
    std::string_view name,
    const std::array<std::pair<std::string_view, Enum>, Size>& values) {
    for (const auto& [candidate, value] : values)
        if (candidate == name) return value;
    return std::nullopt;
}

template <typename Enum, std::size_t Size>
[[nodiscard]] Enum enum_from_string(
    std::string_view name,
    const std::array<std::pair<std::string_view, Enum>, Size>& values,
    std::string_view error_prefix) {
    if (const auto value = find_enum_value(name, values)) return *value;
    throw std::runtime_error(std::string(error_prefix) + std::string(name));
}

}  // namespace chronon3d::render_plan::detail
