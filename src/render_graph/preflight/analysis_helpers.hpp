#pragma once

// ---------------------------------------------------------------------------
// analysis_helpers.hpp
//
// Internal utility functions for preflight render graph analysis.
// Header-only (static inline) — used by analysis_graph.cpp and
// analysis_diagnostics.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ── Bounding box helpers ────────────────────────────────────────────

[[nodiscard]] static inline int64_t bbox_area(const raster::BBox& b) {
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return 0;
    return static_cast<int64_t>(b.x1 - b.x0) * static_cast<int64_t>(b.y1 - b.y0);
}

[[nodiscard]] static inline raster::BBox bbox_intersect(const raster::BBox& a, const raster::BBox& b) {
    raster::BBox r;
    r.x0 = std::max(a.x0, b.x0);
    r.y0 = std::max(a.y0, b.y0);
    r.x1 = std::min(a.x1, b.x1);
    r.y1 = std::min(a.y1, b.y1);
    if (r.x1 <= r.x0 || r.y1 <= r.y0) {
        return raster::BBox{0, 0, 0, 0};
    }
    return r;
}

// ── Shape complexity ────────────────────────────────────────────────

[[nodiscard]] static inline int get_shape_complexity(ShapeType type) {
    switch (type) {
        case ShapeType::Rect:
        case ShapeType::RoundedRect:
        case ShapeType::Image:
            return 1;
        case ShapeType::TiledImage:
        case ShapeType::GridBackground:
        case ShapeType::Circle:
        case ShapeType::Line:
            return 2;
        case ShapeType::Text:
            return 4;
        case ShapeType::Path:
            return 8;
        case ShapeType::Mesh:
        case ShapeType::FakeBox3D:
        case ShapeType::GridPlane:
        case ShapeType::FakeExtrudedText:
            return 6;
        default:
            return 1;
    }
}

// ── Asset existence checks ─────────────────────────────────────────

static inline void check_shape_assets(const Shape& shape, const std::string& node_name, std::vector<std::string>& warnings) {
    // WP-8 PR 8.1 — typed engine-local resolution via the service-
    // locator helper.  Same 2-branch fallback as the other migrated
    // call sites: present → opt->string(), absent → raw relative
    // (or empty for empty input) so subsequent fs::exists walks
    // the same legacy path it used to.
    const auto& resolver = chronon3d::runtime::typed_resolver_for_deep_code();
    auto resolve_path = [&](const std::string& p) -> std::string {
        if (auto opt = resolver.resolve_lexical(p)) return opt->string();
        return p.empty() ? std::string{} : std::string{p};
    };
    if (shape.type() == ShapeType::Image) {
        const std::string path = shape.image().path;
        if (!path.empty()) {
            const std::string resolved = resolve_path(path);
            if (!std::filesystem::exists(resolved)) {
                warnings.push_back("MISSING_ASSET: Image file does not exist: \"" + path + "\"");
            }
        }
    } else if (shape.type() == ShapeType::TiledImage) {
        const std::string path = shape.tiled_image().image.path;
        if (!path.empty()) {
            const std::string resolved = resolve_path(path);
            if (!std::filesystem::exists(resolved)) {
                warnings.push_back("MISSING_ASSET: TiledImage file does not exist: \"" + path + "\"");
            }
        }
    } else if (shape.type() == ShapeType::Text) {
        const std::string font_path = shape.text().style.font_path;
        if (!font_path.empty()) {
            const std::string resolved = resolve_path(font_path);
            if (!std::filesystem::exists(resolved)) {
                warnings.push_back("MISSING_ASSET: Font file does not exist: \"" + font_path + "\"");
            }
        }
    } else if (shape.type() == ShapeType::FakeExtrudedText) {
        const std::string font_path = shape.fake_extruded_text().font_path;
        if (!font_path.empty()) {
            const std::string resolved = resolve_path(font_path);
            if (!std::filesystem::exists(resolved)) {
                warnings.push_back("MISSING_ASSET: Font file does not exist: \"" + font_path + "\"");
            }
        }
    }
}

} // namespace chronon3d::graph
