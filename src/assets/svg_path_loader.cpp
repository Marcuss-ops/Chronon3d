#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::assets {
namespace {

void collect_path_data(
    const boost::property_tree::ptree& tree,
    std::vector<std::string>& paths) {
    for (const auto& [name, node] : tree) {
        if (name == "path") {
            if (auto d = node.get_optional<std::string>("<xmlattr>.d")) {
                paths.push_back(*d);
            }
        }
        collect_path_data(node, paths);
    }
}

} // namespace

SvgPathLoadResult parse_svg_path_data(std::string_view d, SvgPathLoadOptions options) {
    return SvgImporter{}.import_path_data(d, options);
}

SvgDocumentLoadResult load_svg_document_file(
    const std::string& filename,
    SvgPathLoadOptions options) {
    boost::property_tree::ptree document;
    try {
        boost::property_tree::read_xml(
            filename, document,
            boost::property_tree::xml_parser::trim_whitespace);
    } catch (const boost::property_tree::xml_parser::xml_parser_error& e) {
        return {.paths = {}, .ok = false, .error = e.what()};
    }

    std::vector<std::string> path_data;
    collect_path_data(document, path_data);
    if (path_data.empty()) {
        return {.paths = {}, .ok = false, .error = "No <path d=\"...\"> found"};
    }

    SvgDocumentLoadResult result;
    result.paths.reserve(path_data.size());
    for (std::size_t index = 0; index < path_data.size(); ++index) {
        auto path = SvgImporter{}.import_path_data(path_data[index], options);
        if (!path.ok) {
            return {
                .paths = {},
                .ok = false,
                .error = "Failed to import <path> at index " +
                         std::to_string(index) + ": " + path.error,
            };
        }
        result.paths.push_back(std::move(path.path));
    }
    result.ok = true;
    return result;
}

SvgPathLoadResult load_svg_path_file(const std::string& filename, SvgPathLoadOptions options) {
    auto document = load_svg_document_file(filename, options);
    if (!document.ok) {
        return {.path = {}, .ok = false, .error = std::move(document.error)};
    }
    return {.path = std::move(document.paths.front()), .ok = true, .error = {}};
}

} // namespace chronon3d::assets
