#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <string>
#include <string_view>

namespace chronon3d::assets {
namespace {

std::optional<std::string> find_first_path_d(
    const boost::property_tree::ptree& tree) {
    for (const auto& [name, node] : tree) {
        if (name == "path") {
            if (auto d = node.get_optional<std::string>("<xmlattr>.d")) {
                return *d;
            }
        }
        if (auto nested = find_first_path_d(node)) {
            return nested;
        }
    }
    return std::nullopt;
}

} // namespace

SvgPathLoadResult parse_svg_path_data(std::string_view d, SvgPathLoadOptions options) {
    return SvgImporter{}.import_path_data(d, options);
}

SvgPathLoadResult load_svg_path_file(const std::string& filename, SvgPathLoadOptions options) {
    boost::property_tree::ptree document;
    try {
        boost::property_tree::read_xml(
            filename, document,
            boost::property_tree::xml_parser::trim_whitespace);
    } catch (const boost::property_tree::xml_parser::xml_parser_error& e) {
        return {.path = {}, .ok = false, .error = e.what()};
    }
    const auto d = find_first_path_d(document);
    if (!d) {
        return {.path = {}, .ok = false, .error = "No <path d=\"...\"> found"};
    }

    return SvgImporter{}.import_path_data(*d, options);
}

} // namespace chronon3d::assets
