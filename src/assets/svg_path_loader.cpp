#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace chronon3d::assets {
namespace {

std::optional<std::string> extract_first_path_d(std::string_view svg) {
    const auto path_pos = svg.find("<path");
    if (path_pos == std::string_view::npos) return std::nullopt;

    const auto d_pos = svg.find("d=", path_pos);
    if (d_pos == std::string_view::npos || d_pos + 2 >= svg.size()) {
        return std::nullopt;
    }

    const char quote = svg[d_pos + 2];
    if (quote != '"' && quote != '\'') return std::nullopt;

    const auto value_start = d_pos + 3;
    const auto value_end = svg.find(quote, value_start);
    if (value_end == std::string_view::npos) return std::nullopt;

    return std::string(svg.substr(value_start, value_end - value_start));
}

} // namespace

SvgPathLoadResult parse_svg_path_data(std::string_view d, SvgPathLoadOptions options) {
    return SvgImporter{}.import_path_data(d, options);
}

SvgPathLoadResult load_svg_path_file(const std::string& filename, SvgPathLoadOptions options) {
    std::ifstream in(filename);
    if (!in) {
        return {.path = {}, .ok = false, .error = "Cannot open SVG file"};
    }

    std::stringstream ss;
    ss << in.rdbuf();
    const std::string svg = ss.str();
    const auto d = extract_first_path_d(svg);
    if (!d) {
        return {.path = {}, .ok = false, .error = "No <path d=\"...\"> found"};
    }

    return SvgImporter{}.import_path_data(*d, options);
}

} // namespace chronon3d::assets
