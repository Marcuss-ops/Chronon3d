#pragma once

#include <chronon3d/math/vec2.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace chronon3d::assets {

enum class SvgPathCommandType {
    MoveTo,
    LineTo,
    CubicTo,
    QuadTo,
    Close
};

struct SvgPathCommand {
    SvgPathCommandType type;
    Vec2 p0{};
    Vec2 p1{};
    Vec2 p2{};
};

struct SvgPath {
    std::vector<SvgPathCommand> commands;
};

struct SvgLoadResult {
    SvgPath path;
    Vec2 viewbox_size{};
};

SvgLoadResult load_svg_path_file(const std::string& filename);
SvgPath parse_svg_path_data(std::string_view d);

} // namespace chronon3d::assets
