#pragma once

#include <chronon3d/scene/model/shape/path.hpp>
#include <string>
#include <string_view>
#include <optional>

namespace chronon3d::assets {

struct SvgPathLoadOptions {
    bool support_relative_commands{true};
};

struct SvgPathLoadResult {
    PathShape path;
    bool ok{false};
    std::string error;
};

SvgPathLoadResult parse_svg_path_data(
    std::string_view d,
    SvgPathLoadOptions options = {}
);

SvgPathLoadResult load_svg_path_file(
    const std::string& filename,
    SvgPathLoadOptions options = {}
);

} // namespace chronon3d::assets
