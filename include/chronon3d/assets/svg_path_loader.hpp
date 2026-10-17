#pragma once

#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/assets/svg_styled_element.hpp>
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
    std::vector<SvgStyledElement> elements;
    std::optional<PathShape> vector_mask;
    /// Declared SVG viewport size (explicit `width`/`height`, else the
    /// viewBox-derived viewport). Present only when the document declares a
    /// viewport; render-plan lowering uses it to center the document on the
    /// layer box, and leaves dimension-less documents at their parsed
    /// coordinates for backward compatibility.
    std::optional<std::array<float, 2>> viewport_size;
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
