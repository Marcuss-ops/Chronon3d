#include "extruded_text_mesh.hpp"

#include <clipper2/clipper.h>

#include <algorithm>
#include <array>
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

Vec2 centroid(const std::vector<Vec2>& polygon) {
    Vec2 sum{0.0f, 0.0f};
    for (const auto& point : polygon) sum += point;
    return polygon.empty() ? sum : sum / static_cast<float>(polygon.size());
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

void append_triangle_split_normals(Mesh& mesh, Vec3 a, Vec3 b, Vec3 c,
                                   Vec3 na, Vec3 nb, Vec3 nc) {
    const u32 base = static_cast<u32>(mesh.vertices().size());
    mesh.add_vertex(Vertex{a, na});
    mesh.add_vertex(Vertex{b, nb});
    mesh.add_vertex(Vertex{c, nc});
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

std::vector<std::vector<Vec2>> paths_to_contours(
    const Clipper2Lib::Paths64& paths, float offset_x, float offset_y) {
    std::vector<std::vector<Vec2>> contours;
    contours.reserve(paths.size());
    for (const auto& path : paths) {
        std::vector<Vec2> contour;
        contour.reserve(path.size());
        for (const auto& point : path) {
            contour.emplace_back(
                static_cast<float>(static_cast<double>(point.x) / kContourUnits + offset_x),
                static_cast<float>(static_cast<double>(point.y) / kContourUnits + offset_y));
        }
        if (contour.size() >= 3) contours.push_back(std::move(contour));
    }
    return contours;
}

Vec2 closest_point_on_segment(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 edge = b - a;
    const float length_sq = glm::dot(edge, edge);
    if (length_sq <= 1e-12f) return a;
    const float t = std::clamp(glm::dot(point - a, edge) / length_sq, 0.0f, 1.0f);
    return a + edge * t;
}

Vec2 closest_point_on_contour(const std::vector<Vec2>& contour, Vec2 point) {
    if (contour.empty()) return point;
    Vec2 best = contour.front();
    float best_distance = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < contour.size(); ++i) {
        const Vec2 candidate =
            closest_point_on_segment(point, contour[i], contour[(i + 1) % contour.size()]);
        const float distance = glm::dot(candidate - point, candidate - point);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

std::vector<Vec2> resample_closed_contour(const std::vector<Vec2>& contour,
                                          std::size_t count) {
    if (contour.size() < 3 || count < 3) return contour;
    std::vector<float> lengths(contour.size());
    float total = 0.0f;
    for (std::size_t i = 0; i < contour.size(); ++i) {
        lengths[i] = glm::length(contour[(i + 1) % contour.size()] - contour[i]);
        total += lengths[i];
    }
    if (total <= 1e-6f) return contour;
    std::vector<Vec2> out;
    out.reserve(count);
    std::size_t segment = 0;
    float segment_start = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float target = total * static_cast<float>(i) / static_cast<float>(count);
        while (segment + 1 < lengths.size() &&
               segment_start + lengths[segment] < target) {
            segment_start += lengths[segment];
            ++segment;
        }
        const float t = lengths[segment] > 1e-6f
            ? std::clamp((target - segment_start) / lengths[segment], 0.0f, 1.0f)
            : 0.0f;
        out.push_back(contour[segment] +
                      (contour[(segment + 1) % contour.size()] - contour[segment]) * t);
    }
    return out;
}

Vec3 front_cap_normal(Vec2 point, Vec2 centre, Vec2 half_extent) {
    // A broad dome keeps the front face from reading as a flat sticker. The
    // geometry stays planar; only the interpolated normal bends the diffuse
    // and specular response across the whole face.
    const float nx = half_extent.x > 1e-4f ? (point.x - centre.x) / half_extent.x : 0.0f;
    const float ny = half_extent.y > 1e-4f ? (point.y - centre.y) / half_extent.y : 0.0f;
    return glm::normalize(Vec3{nx * 0.28f, ny * 0.28f, -1.0f});
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

        // A front chamfer is what turns the extrusion from a translated
        // silhouette into a lit solid. Inset the outline for the front
        // plateau, then bridge that inset to the original rim. The width is
        // proportional to both depth and type size, so short and tall runs
        // keep the same visual edge.
        const float bevel_width = std::clamp(
            std::min(depth * 0.14f, font_size * 0.10f), 1.0f, 32.0f);
        const float bevel_depth = bevel_width * 0.85f;
        const float rim_z = front_z + bevel_depth;

        Clipper2Lib::Paths64 inset_paths;
        std::vector<std::vector<Vec2>> inset_contours;
        bool beveled = false;
        if (bevel_width >= 0.75f) {
            inset_paths = Clipper2Lib::InflatePaths(
                paths, -static_cast<double>(bevel_width) * kContourUnits,
                Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon,
                2.0, 0.01 * kContourUnits);
            if (!inset_paths.empty()) {
                inset_contours = paths_to_contours(inset_paths, glyph.x, glyph.y);
                beveled = inset_contours.size() == placed_contours.size();
            }
        }

        auto triangulate = [&](const Clipper2Lib::Paths64& source,
                               Clipper2Lib::Paths64& triangles) {
            return Clipper2Lib::Triangulate(source, triangles, false);
        };

        Clipper2Lib::Paths64 cap_paths = beveled ? inset_paths : paths;
        Clipper2Lib::Paths64 triangles;
        auto result = triangulate(cap_paths, triangles);
        if (beveled && (result != Clipper2Lib::TriangulateResult::success ||
                        triangles.empty())) {
            // Some very narrow counters cannot be inset cleanly. Keep the
            // glyph solid rather than risk a hole or a missing cap.
            beveled = false;
            inset_contours.clear();
            cap_paths = paths;
            result = triangulate(cap_paths, triangles);
        }
        if (result != Clipper2Lib::TriangulateResult::success)
            throw std::runtime_error(
                "FreeType glyph " + std::to_string(glyph.glyph_id) +
                " contours=" + std::to_string(paths.size()) +
                " could not be triangulated (Clipper2 result=" +
                std::to_string(static_cast<int>(result)) + ")");

        Vec2 min_point{std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity()};
        Vec2 max_point{-std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity()};
        for (const auto& contour : placed_contours) {
            for (const auto& point : contour) {
                min_point.x = std::min(min_point.x, point.x);
                min_point.y = std::min(min_point.y, point.y);
                max_point.x = std::max(max_point.x, point.x);
                max_point.y = std::max(max_point.y, point.y);
            }
        }
        const Vec2 centre = (min_point + max_point) * 0.5f;
        const Vec2 half_extent = (max_point - min_point) * 0.5f;

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
            append_triangle_split_normals(*mesh,
                Vec3{p[2], front_z}, Vec3{p[1], front_z}, Vec3{p[0], front_z},
                front_cap_normal(p[2], centre, half_extent),
                front_cap_normal(p[1], centre, half_extent),
                front_cap_normal(p[0], centre, half_extent));
            append_triangle(*mesh,
                Vec3{p[0], back_z}, Vec3{p[1], back_z},
                Vec3{p[2], back_z}, Vec3{0.0f, 0.0f, 1.0f});
        }

        if (beveled) {
            // Pair each original contour with the nearest inset contour of the
            // same winding. Glyph counters and outer loops both get a chamfer.
            std::vector<int> matches(placed_contours.size(), -1);
            std::vector<bool> used(inset_contours.size(), false);
            for (std::size_t i = 0; i < placed_contours.size(); ++i) {
                const Vec2 outer_centre = centroid(placed_contours[i]);
                const double outer_area = signed_area(placed_contours[i]);
                int best = -1;
                float best_distance = std::numeric_limits<float>::infinity();
                for (std::size_t j = 0; j < inset_contours.size(); ++j) {
                    if (used[j]) continue;
                    if ((signed_area(inset_contours[j]) < 0.0) != (outer_area < 0.0)) continue;
                    const float distance =
                        glm::dot(centroid(inset_contours[j]) - outer_centre,
                                 centroid(inset_contours[j]) - outer_centre);
                    if (distance < best_distance) {
                        best_distance = distance;
                        best = static_cast<int>(j);
                    }
                }
                if (best >= 0) {
                    matches[i] = best;
                    used[static_cast<std::size_t>(best)] = true;
                }
            }
            for (std::size_t i = 0; i < placed_contours.size(); ++i) {
                if (matches[i] < 0) continue;
                const auto& outer = placed_contours[i];
                const auto& inner = inset_contours[static_cast<std::size_t>(matches[i])];
                const std::size_t sample_count =
                    std::max<std::size_t>({outer.size(), inner.size(), 128});
                auto outer_samples = resample_closed_contour(outer, sample_count);
                auto inner_samples = resample_closed_contour(inner, sample_count);
                if (outer_samples.size() < 3 ||
                    inner_samples.size() != outer_samples.size())
                    continue;
                // Align the two rings at the point nearest the outer start so
                // the bridge cannot twist along the contour.
                std::size_t start = 0;
                float start_distance = std::numeric_limits<float>::infinity();
                for (std::size_t j = 0; j < inner_samples.size(); ++j) {
                    const float distance = glm::dot(
                        inner_samples[j] - outer_samples.front(),
                        inner_samples[j] - outer_samples.front());
                    if (distance < start_distance) {
                        start_distance = distance;
                        start = j;
                    }
                }
                std::rotate(inner_samples.begin(), inner_samples.begin() +
                            static_cast<std::ptrdiff_t>(start), inner_samples.end());
                for (std::size_t k = 0; k < outer_samples.size(); ++k) {
                    const Vec2 a = outer_samples[k];
                    const Vec2 b = outer_samples[(k + 1) % outer_samples.size()];
                    const Vec2 ia = inner_samples[k];
                    const Vec2 ib = inner_samples[(k + 1) % inner_samples.size()];
                    const Vec2 edge = b - a;
                    const float length = glm::length(edge);
                    if (length <= 1e-5f) continue;
                    const Vec2 outward{edge.y / length, -edge.x / length};
                    // The chamfer faces forward and outward: the normal has a
                    // negative Z component, matching a bevel lit from camera.
                    const Vec3 normal = glm::normalize(Vec3{
                        outward.x * bevel_depth, outward.y * bevel_depth,
                        -bevel_width});
                    const Vec3 af{ia, front_z};
                    const Vec3 bf{ib, front_z};
                    const Vec3 ar{a, rim_z};
                    const Vec3 br{b, rim_z};
                    append_triangle(*mesh, af, bf, br, normal);
                    append_triangle(*mesh, af, br, ar, normal);
                }
            }
        }

        const float wall_front_z = beveled ? rim_z : front_z;
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
                const Vec3 af{a, wall_front_z}, bf{b, wall_front_z};
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
