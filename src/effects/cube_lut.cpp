#include <chronon3d/effects/cube_lut.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

namespace chronon3d::effects {

Lut3DParams parse_cube_lut(std::string_view source, float amount) {
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f)
        throw std::runtime_error(".cube LUT amount must be finite and in [0, 1]");
    Lut3DParams lut;
    lut.amount = amount;
    auto values = std::make_shared<std::vector<std::array<float, 3>>>();
    bool saw_size = false;
    std::istringstream input{std::string(source)};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream row(line);
        std::string token;
        if (!(row >> token)) continue;
        if (token == "TITLE") continue;
        if (token == "LUT_1D_SIZE")
            throw std::runtime_error("1D .cube LUTs are unsupported");
        if (token == "LUT_3D_SIZE") {
            long long size = 0;
            std::string extra;
            if (!(row >> size) || (row >> extra) || size < 2 || size > 64 || saw_size)
                throw std::runtime_error("invalid LUT_3D_SIZE at .cube line " + std::to_string(line_number));
            lut.dimension = static_cast<uint32_t>(size);
            values->reserve(static_cast<std::size_t>(size * size * size));
            saw_size = true;
            continue;
        }
        if (token == "DOMAIN_MIN" || token == "DOMAIN_MAX") {
            std::array<float, 3> value{};
            std::string extra;
            if (!(row >> value[0] >> value[1] >> value[2]) || (row >> extra) ||
                !std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2]))
                throw std::runtime_error("invalid .cube domain at line " + std::to_string(line_number));
            (token == "DOMAIN_MIN" ? lut.domain_min : lut.domain_max) = value;
            continue;
        }
        if (!saw_size)
            throw std::runtime_error(".cube data encountered before LUT_3D_SIZE");
        std::array<float, 3> value{};
        std::string extra;
        row.clear();
        row.seekg(0);
        if (!(row >> value[0] >> value[1] >> value[2]) || (row >> extra) ||
            !std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2]))
            throw std::runtime_error("invalid RGB value at .cube line " + std::to_string(line_number));
        values->push_back(value);
        if (values->size() > static_cast<std::size_t>(lut.dimension) * lut.dimension * lut.dimension)
            throw std::runtime_error("too many RGB values in .cube LUT");
    }
    if (!saw_size || values->size() != static_cast<std::size_t>(lut.dimension) * lut.dimension * lut.dimension)
        throw std::runtime_error(".cube LUT must contain exactly LUT_3D_SIZE cubed RGB rows");
    for (std::size_t channel = 0; channel < 3; ++channel)
        if (!(lut.domain_max[channel] > lut.domain_min[channel]))
            throw std::runtime_error(".cube DOMAIN_MAX must exceed DOMAIN_MIN for each channel");
    lut.values = std::move(values);
    return lut;
}

} // namespace chronon3d::effects
