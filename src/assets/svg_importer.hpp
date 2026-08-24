#pragma once

#include <chronon3d/assets/svg_path_loader.hpp>

namespace chronon3d::assets {

/// Internal SVG path importer backed by SVG++ path-data parsing.
class SvgImporter {
public:
    [[nodiscard]] SvgPathLoadResult import_path_data(
        std::string_view d,
        SvgPathLoadOptions options = {}
    ) const;
};

} // namespace chronon3d::assets
