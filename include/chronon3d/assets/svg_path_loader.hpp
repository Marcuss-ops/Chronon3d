#pragma once

#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/assets/svg_styled_element.hpp>
#include <array>
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

struct SvgFilterEffectSpec {
    enum class Kind { GaussianBlur, DropShadow, ColorMatrix, Blend };
    Kind kind{Kind::GaussianBlur};
    float radius{0.0f};
    std::array<float, 2> offset{0.0f, 0.0f};
    std::array<float, 4> color{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 20> color_matrix{};
    std::string blend_mode{"normal"};
};

/// Result of importing SVG geometry elements in document order.
///
/// This is deliberately a geometry-only boundary: fill, stroke, styles, and CSS
/// are owned by the shape/style pipeline, not by the SVG XML geometry reader.
struct SvgDocumentLoadResult {
    std::vector<PathShape> paths;
    std::vector<SvgStyledElement> elements;
    std::vector<SvgTextRun> text_runs;
    std::optional<PathShape> vector_mask;
    /// One root-scoped SVG Gaussian blur that maps directly to Chronon's
    /// existing layer GaussianBlur effect. Arbitrary filter graphs are rejected.
    std::optional<float> root_gaussian_blur;
    /// Ordered CPU effect lowering for the bounded root SVG filter chain.
    std::vector<SvgFilterEffectSpec> root_filter_effects;
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
