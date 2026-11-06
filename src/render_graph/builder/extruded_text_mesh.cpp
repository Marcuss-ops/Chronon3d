#include "extruded_text_mesh.hpp"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::graph::detail {
namespace {

constexpr double kContourUnits = 1024.0;

double signed_area(const std::vector<Vec2>& polygon) {
    double area = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto& a = polygon[i];
        const auto& b = polygon[(i + 1) % polygon.size()];
        area += static_cast<double>(a.x) * b.y -
                static_cast<double>(b.x) * a.y;
    }
    return area * 0.5;
}

void append_triangle(Mesh& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 normal) {
    const u32 base = static_cast<u32>(mesh.vertices().size());
    mesh.add_vertex(Vertex{a, normal});
    mesh.add_vertex(Vertex{b, normal});
    mesh.add_vertex(Vertex{c, normal});
    mesh.add_index(base);
    mesh.add_index(base + 1);
    mesh.add_index(base + 2);
}

FontSpec font_for_glyph(const TextRunLayout& layout, std::size_t glyph_index,
                        float& size, std::string& axes) {
    FontSpec font = layout.font;
    size = layout.font_size;
    axes = layout.variation_axes;
    for (const auto& span : layout.font_spans) {
        if (glyph_index < span.glyph_begin || glyph_index >= span.glyph_end) continue;
        font.font_path = span.font.font_path;
        font.font_family = span.font.font_family;
        font.font_weight = span.font.font_weight;
        font.font_style = span.font.font_style;
        if (span.font_size > 0.0f) size = span.font_size;
        axes = span.variation_axes;
        break;
    }
    return font;
}

bool is_static_glyph_state(const GlyphInstanceState& state) {
    return state.position == Vec3{0.0f} && state.scale == Vec3{1.0f} &&
           state.rotation == Vec3{0.0f} && state.anchor == Vec3{0.0f} &&
           state.skew == 0.0f && state.baseline_shift == 0.0f &&
           state.opacity == 1.0f;
}

} // namespace

std::shared_ptr<Mesh> build_extruded_text_mesh(
    const TextRunShape& shape, float depth) {
    if (!shape.layout || !shape.engine)
        throw std::runtime_error("ExtrudedText3D requires a prepared TextRun and FontEngine");
    if (!std::isfinite(depth) || depth <= 0.0f || depth > 10000.0f)
        throw std::runtime_error("ExtrudedText3D depth must be finite and in (0, 10000]");
    if (!shape.animators.empty() || shape.animated_doc || shape.dissolve_layout)
        throw std::runtime_error("ExtrudedText3D currently requires a static TextRun");
    const auto& layout = *shape.layout;
    const auto& glyphs = layout.placed.glyphs;
    if (glyphs.empty() || shape.glyphs.size() != glyphs.size())
        throw std::runtime_error("ExtrudedText3D glyph state does not match the shaped layout");
    for (const auto& state : shape.glyphs) {
        if (!is_static_glyph_state(state))
            throw std::runtime_error("ExtrudedText3D does not accept animated glyph transforms");
    }

    auto mesh = std::make_shared<Mesh>("extruded_text_run");
    // The native Vulkan mesh path reflects camera Z before projection. Put
    // the readable/front cap on local -Z so it faces the canonical camera
    // side while preserving positive camera depth and readable winding.
    const float half_depth = depth * 0.5f;
    const float front_z = -half_depth;
    const float back_z = half_depth;
    std::size_t outline_count = 0;
    for (std::size_t glyph_index = 0; glyph_index < glyphs.size(); ++glyph_index) {
        const auto& glyph = glyphs[glyph_index];
        float font_size = 0.0f;
        std::string axes;
        const FontSpec font = font_for_glyph(layout, glyph_index, font_size, axes);
        auto contours = shape.engine->glyph_outline_contours(
            font, glyph.glyph_id, font_size, axes);
        if (contours.empty()) continue; // spaces and non-outline glyphs

        Clipper2Lib::Paths64 paths;
        std::vector<std::vector<Vec2>> placed_contours;
        paths.reserve(contours.size());
        placed_contours.reserve(contours.size());
        for (auto& contour : contours) {
            if (contour.size() > 3 &&
                glm::length(contour.front() - contour.back()) < 1e-4f)
                contour.pop_back(); // Clipper expects an implicit, not duplicated, closure.
            std::vector<Vec2> placed;
            Clipper2Lib::Path64 path;
            placed.reserve(contour.size());
            path.reserve(contour.size());
            for (const auto& point : contour) {
                const Vec2 p{point.x + glyph.x, point.y + glyph.y};
                if (!placed.empty() && glm::length(p - placed.back()) < 1e-4f) continue;
                placed.push_back(p);
                path.emplace_back(
                    static_cast<std::int64_t>(std::llround(point.x * kContourUnits)),
                    static_cast<std::int64_t>(std::llround(point.y * kContourUnits)));
            }
            if (placed.size() < 3 || std::abs(signed_area(placed)) < 1e-5) continue;
            paths.push_back(std::move(path));
            placed_contours.push_back(std::move(placed));
        }
        if (paths.empty()) continue;

        // FreeType contours are winding-oriented but may contain touching
        // component boundaries. Normalize each glyph using the font's
        // non-zero fill rule before asking the CDT to triangulate counters.
        paths = Clipper2Lib::BooleanOp(
            Clipper2Lib::ClipType::Union,
            Clipper2Lib::FillRule::NonZero,
            paths,
            {});
        if (paths.empty()) continue;
        placed_contours.clear();
        placed_contours.reserve(paths.size());
        for (const auto& path : paths) {
            std::vector<Vec2> placed;
            placed.reserve(path.size());
            for (const auto& point : path) {
                placed.emplace_back(
                    static_cast<float>(static_cast<double>(point.x) / kContourUnits + glyph.x),
                    static_cast<float>(static_cast<double>(point.y) / kContourUnits + glyph.y));
            }
            if (placed.size() >= 3) placed_contours.push_back(std::move(placed));
        }

        Clipper2Lib::Paths64 triangles;
        const auto result = Clipper2Lib::Triangulate(paths, triangles, false);
        if (result != Clipper2Lib::TriangulateResult::success)
            throw std::runtime_error(
                "FreeType glyph " + std::to_string(glyph.glyph_id) +
                " contours=" + std::to_string(paths.size()) +
                " could not be triangulated (Clipper2 result=" +
                std::to_string(static_cast<int>(result)) + ")");

        for (const auto& triangle : triangles) {
            if (triangle.size() != 3) continue;
            std::array<Vec2, 3> p{};
            for (std::size_t j = 0; j < 3; ++j) {
                p[j] = Vec2{
                    static_cast<float>(static_cast<double>(triangle[j].x) / kContourUnits + glyph.x),
                    static_cast<float>(static_cast<double>(triangle[j].y) / kContourUnits + glyph.y)};
            }
            const double area = signed_area({p[0], p[1], p[2]});
            if (std::abs(area) < 1e-8) continue;
            if (area < 0.0) std::swap(p[1], p[2]);
            append_triangle(*mesh,
                Vec3{p[2], front_z}, Vec3{p[1], front_z},
                Vec3{p[0], front_z}, Vec3{0.0f, 0.0f, -1.0f});
            append_triangle(*mesh,
                Vec3{p[0], back_z}, Vec3{p[1], back_z},
                Vec3{p[2], back_z}, Vec3{0.0f, 0.0f, 1.0f});
        }

        for (const auto& contour : placed_contours) {
            for (std::size_t i = 0; i < contour.size(); ++i) {
                const Vec2 a = contour[i];
                const Vec2 b = contour[(i + 1) % contour.size()];
                const Vec2 edge = b - a;
                const float length = glm::length(edge);
                if (length <= 1e-5f) continue;
                // For FreeType's non-zero winding contours, the right-hand
                // normal is outward on outer loops and points into counters
                // on oppositely-wound hole loops.
                const Vec3 normal{edge.y / length, -edge.x / length, 0.0f};
                const Vec3 af{a, front_z}, bf{b, front_z};
                const Vec3 ab{a, back_z}, bb{b, back_z};
                append_triangle(*mesh, af, bf, bb, normal);
                append_triangle(*mesh, af, bb, ab, normal);
            }
        }
        outline_count += placed_contours.size();
    }
    if (outline_count == 0 || mesh->indices().empty())
        throw std::runtime_error("ExtrudedText3D produced no outline geometry");
    // Shaped glyph positions and FreeType outlines use top-down Y. Native
    // camera mesh coordinates are Y-up, so reflect only Y around the run's
    // bounds while keeping its placement and left-to-right glyph order.
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& vertex : mesh->vertices()) {
        min_y = std::min(min_y, vertex.position.y);
        max_y = std::max(max_y, vertex.position.y);
    }
    auto oriented_mesh = std::make_shared<Mesh>("extruded_text_run");
    for (const auto& source : mesh->vertices()) {
        Vertex vertex = source;
        vertex.position.y = min_y + max_y - vertex.position.y;
        vertex.normal.y = -vertex.normal.y;
        oriented_mesh->add_vertex(vertex);
    }
    const auto& indices = mesh->indices();
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        oriented_mesh->add_index(indices[i]);
        oriented_mesh->add_index(indices[i + 2]);
        oriented_mesh->add_index(indices[i + 1]);
    }
    mesh = std::move(oriented_mesh);
    return mesh;
}

} // namespace chronon3d::graph::detail
