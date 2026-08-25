#pragma once

#include <chronon3d/scene/model/shape/path.hpp>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace chronon3d::assets {

struct SvgPathLoadOptions {
    bool support_relative_commands{true};
};

struct SvgPathLoadResult {
    PathShape path;
    bool ok{false};
    std::string error;
};

/// Result of importing SVG geometry elements in document order.
///
/// This is deliberately a geometry-only boundary: fill, stroke, styles, and CSS
/// are owned by the shape/style pipeline, not by the SVG XML geometry reader.
struct SvgDocumentLoadResult {
    std::vector<PathShape> paths;
    bool ok{false};
    std::string error;
};

SvgPathLoadResult parse_svg_path_data(
    std::string_view d,
    SvgPathLoadOptions options = {}
);

SvgDocumentLoadResult load_svg_document_file(
    const std::string& filename,
    SvgPathLoadOptions options = {}
);

} // namespace chronon3d::assets
