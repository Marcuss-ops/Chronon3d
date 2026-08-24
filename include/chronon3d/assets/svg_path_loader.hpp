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

/// Result of importing the path elements in an SVG document.
///
/// The document importer is the canonical XML boundary.  The legacy
/// `load_svg_path_file()` API below remains as a compatibility convenience and
/// returns the first imported path.
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

SvgPathLoadResult load_svg_path_file(
    const std::string& filename,
    SvgPathLoadOptions options = {}
);

} // namespace chronon3d::assets
