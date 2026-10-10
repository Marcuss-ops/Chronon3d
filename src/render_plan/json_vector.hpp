#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chronon3d::render_plan::detail {

/// Decode a fixed-size numeric JSON vector. Callers retain field-specific
/// finite/range checks and pass the existing diagnostic for contract parity.
template <std::size_t N>
[[nodiscard]] std::array<float, N> decode_json_vector(
    const nlohmann::json& value, std::string_view error_message) {
    if (!value.is_array() || value.size() != N)
        throw std::runtime_error(std::string(error_message));
    std::array<float, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        if (!value.at(i).is_number())
            throw std::runtime_error(std::string(error_message));
        result[i] = value.at(i).get<float>();
    }
    return result;
}

} // namespace chronon3d::render_plan::detail
