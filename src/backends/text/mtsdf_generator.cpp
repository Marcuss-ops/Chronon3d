#include <chronon3d/backends/text/mtsdf_generator.hpp>

#ifdef CHRONON3D_ENABLE_TEXT

#include <chronon3d/core/profiling/profiling_context.hpp>  // g_current_counters
#include <chronon3d/core/profiling/render_counter_types.hpp>  // RenderCounters

#include <ft2build.h>
#include FT_OUTLINE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>

namespace chronon3d {
namespace {

struct Point {
    double x{0.0};
    double y{0.0};
};

struct Contour {
    std::vector<Point> points;
};

struct OutlineBuild {
    std::vector<Contour> contours;
    Contour current;
    Point cursor{};
    bool has_cursor{false};
};

void append_point(OutlineBuild& build, Point point) {
    if (!build.has_cursor ||
        std::hypot(point.x - build.cursor.x, point.y - build.cursor.y) > 1e-7) {
        build.current.points.push_back(point);
    }
    build.cursor = point;
    build.has_cursor = true;
}

void flatten_quadratic(OutlineBuild& build, Point control, Point end) {
    const auto length = std::hypot(build.cursor.x - control.x,
                                   build.cursor.y - control.y) +
                        std::hypot(control.x - end.x, control.y - end.y);
    const int steps = std::clamp(static_cast<int>(std::ceil(length / 2.0)), 4, 32);
    const Point start = build.cursor;
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double u = 1.0 - t;
        append_point(build, Point{
            u * u * start.x + 2.0 * u * t * control.x + t * t * end.x,
            u * u * start.y + 2.0 * u * t * control.y + t * t * end.y});
    }
}

void flatten_cubic(OutlineBuild& build, Point c1, Point c2, Point end) {
    const auto length = std::hypot(build.cursor.x - c1.x, build.cursor.y - c1.y) +
                        std::hypot(c1.x - c2.x, c1.y - c2.y) +
                        std::hypot(c2.x - end.x, c2.y - end.y);
    const int steps = std::clamp(static_cast<int>(std::ceil(length / 2.0)), 6, 48);
    const Point start = build.cursor;
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double u = 1.0 - t;
        append_point(build, Point{
            u * u * u * start.x + 3.0 * u * u * t * c1.x +
                3.0 * u * t * t * c2.x + t * t * t * end.x,
            u * u * u * start.y + 3.0 * u * u * t * c1.y +
                3.0 * u * t * t * c2.y + t * t * t * end.y});
    }
}

void finish_contour(OutlineBuild& build) {
    if (build.current.points.size() >= 2) {
        if (std::hypot(build.current.points.front().x - build.current.points.back().x,
                       build.current.points.front().y - build.current.points.back().y) < 1e-7) {
            build.current.points.pop_back();
        }
        if (build.current.points.size() >= 2) {
            build.contours.push_back(std::move(build.current));
        }
    }
    build.current = {};
    build.has_cursor = false;
}

int outline_move_to(const FT_Vector* to, void* user) {
    auto& build = *static_cast<OutlineBuild*>(user);
    finish_contour(build);
    append_point(build, Point{static_cast<double>(to->x) / 64.0,
                              -static_cast<double>(to->y) / 64.0});
    return 0;
}

int outline_line_to(const FT_Vector* to, void* user) {
    auto& build = *static_cast<OutlineBuild*>(user);
    append_point(build, Point{static_cast<double>(to->x) / 64.0,
                              -static_cast<double>(to->y) / 64.0});
    return 0;
}

int outline_conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto& build = *static_cast<OutlineBuild*>(user);
    flatten_quadratic(
        build,
        Point{static_cast<double>(control->x) / 64.0,
              -static_cast<double>(control->y) / 64.0},
        Point{static_cast<double>(to->x) / 64.0,
              -static_cast<double>(to->y) / 64.0});
    return 0;
}

int outline_cubic_to(const FT_Vector* c1, const FT_Vector* c2,
                     const FT_Vector* to, void* user) {
    auto& build = *static_cast<OutlineBuild*>(user);
    flatten_cubic(
        build,
        Point{static_cast<double>(c1->x) / 64.0,
              -static_cast<double>(c1->y) / 64.0},
        Point{static_cast<double>(c2->x) / 64.0,
              -static_cast<double>(c2->y) / 64.0},
        Point{static_cast<double>(to->x) / 64.0,
              -static_cast<double>(to->y) / 64.0});
    return 0;
}

struct Edge {
    Point a{};
    Point b{};
    std::uint32_t channel{0};
};

double point_segment_distance(Point p, const Edge& edge) noexcept {
    const double dx = edge.b.x - edge.a.x;
    const double dy = edge.b.y - edge.a.y;
    const double length_sq = dx * dx + dy * dy;
    if (length_sq <= 1e-18) {
        return std::hypot(p.x - edge.a.x, p.y - edge.a.y);
    }
    const double t = std::clamp(((p.x - edge.a.x) * dx +
                                 (p.y - edge.a.y) * dy) / length_sq,
                                0.0, 1.0);
    return std::hypot(p.x - (edge.a.x + t * dx),
                      p.y - (edge.a.y + t * dy));
}

void accumulate_winding(Point p, const Edge& edge, int& winding) noexcept {
    if (edge.a.y <= p.y) {
        if (edge.b.y > p.y &&
            (edge.b.x - edge.a.x) * (p.y - edge.a.y) -
                (p.x - edge.a.x) * (edge.b.y - edge.a.y) > 0.0) {
            ++winding;
        }
    } else if (edge.b.y <= p.y &&
               (edge.b.x - edge.a.x) * (p.y - edge.a.y) -
                   (p.x - edge.a.x) * (edge.b.y - edge.a.y) < 0.0) {
        --winding;
    }
}

int compute_winding(Point p, const std::vector<Edge>& edges) noexcept {
    int winding = 0;
    for (const auto& edge : edges) {
        accumulate_winding(p, edge, winding);
    }
    return winding;
}

// Extract true silhouette and counter boundary sub-segments by splitting
// intersecting contour edges and discarding internal overlap seams where both
// sides of a sub-segment have non-zero winding (common in variable/OpenType
// fonts such as Inter-Bold where stems and bowls are authored as overlapping
// contours).
std::vector<Edge> extract_boundary_edges(const std::vector<Edge>& edges) {
    if (edges.empty()) return {};
    std::vector<Edge> boundary;
    boundary.reserve(edges.size());
    std::vector<double> splits;
    splits.reserve(16);

    for (std::size_t i = 0; i < edges.size(); ++i) {
        const Point a = edges[i].a;
        const Point b = edges[i].b;
        const double ux = b.x - a.x;
        const double uy = b.y - a.y;
        const double len_sq = ux * ux + uy * uy;
        if (len_sq <= 1e-14) continue;

        splits.clear();
        splits.push_back(0.0);
        splits.push_back(1.0);

        for (std::size_t j = 0; j < edges.size(); ++j) {
            if (i == j) continue;
            const Point c = edges[j].a;
            const Point d = edges[j].b;
            const double vx = d.x - c.x;
            const double vy = d.y - c.y;
            const double wx = a.x - c.x;
            const double wy = a.y - c.y;
            const double denom = ux * vy - uy * vx;
            if (std::abs(denom) > 1e-10) {
                const double t = (vx * wy - vy * wx) / denom;
                const double s = (ux * wy - uy * wx) / denom;
                if (t > 1e-6 && t < 1.0 - 1e-6 &&
                    s >= -1e-6 && s <= 1.0 + 1e-6) {
                    splits.push_back(t);
                }
            } else {
                // Nearly parallel/collinear: split at projected endpoints of j
                // if they lie close to segment i.
                const double dist_c = std::abs((c.x - a.x) * uy - (c.y - a.y) * ux) / std::sqrt(len_sq);
                if (dist_c < 1e-4) {
                    const double tc = ((c.x - a.x) * ux + (c.y - a.y) * uy) / len_sq;
                    if (tc > 1e-6 && tc < 1.0 - 1e-6) splits.push_back(tc);
                    const double td = ((d.x - a.x) * ux + (d.y - a.y) * uy) / len_sq;
                    if (td > 1e-6 && td < 1.0 - 1e-6) splits.push_back(td);
                }
            }
        }

        std::sort(splits.begin(), splits.end());
        const double len = std::sqrt(len_sq);
        const double nx = -uy / len;
        const double ny = ux / len;
        constexpr double kProbeEps = 1e-3;

        for (std::size_t k = 0; k + 1 < splits.size(); ++k) {
            const double t0 = splits[k];
            const double t1 = splits[k + 1];
            if ((t1 - t0) * len <= 1e-5) continue;
            const double tm = 0.5 * (t0 + t1);
            const Point mid{a.x + tm * ux, a.y + tm * uy};
            const Point p_left{mid.x + kProbeEps * nx, mid.y + kProbeEps * ny};
            const Point p_right{mid.x - kProbeEps * nx, mid.y - kProbeEps * ny};
            const bool inside_left = compute_winding(p_left, edges) != 0;
            const bool inside_right = compute_winding(p_right, edges) != 0;
            if (inside_left != inside_right) {
                boundary.push_back(Edge{
                    Point{a.x + t0 * ux, a.y + t0 * uy},
                    Point{a.x + t1 * ux, a.y + t1 * uy},
                    edges[i].channel});
            }
        }
    }

    return boundary.empty() ? edges : boundary;
}

float encode_distance(double distance, double range) noexcept {
    return static_cast<float>(std::clamp(0.5 + distance / range, 0.0, 1.0));
}

// Measures how long the FreeType guard is held and folds it into the render
// counters, so the lock's real cost is observable instead of assumed. Same
// pattern as the FramebufferPool lock meter.
class MtsdfLockHoldMeter {
public:
    MtsdfLockHoldMeter() noexcept : t0_(std::chrono::steady_clock::now()) {}
    ~MtsdfLockHoldMeter() {
        if (!profiling::g_current_counters) return;
        profiling::g_current_counters->mtsdf_lock_hold_wall_us.fetch_add(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0_).count()),
            std::memory_order_relaxed);
    }
    MtsdfLockHoldMeter(const MtsdfLockHoldMeter&) = delete;
    MtsdfLockHoldMeter& operator=(const MtsdfLockHoldMeter&) = delete;

private:
    std::chrono::steady_clock::time_point t0_;
};

float packed_rgba8(float r, float g, float b, float a) noexcept {
    const auto byte = [](float value) {
        return static_cast<std::uint32_t>(std::clamp(
            std::lround(value * 255.0f), 0L, 255L));
    };
    const auto packed = byte(r) | (byte(g) << 8) | (byte(b) << 16) |
                        (byte(a) << 24);
    return std::bit_cast<float>(packed);
}

} // namespace

std::optional<MtsdfGlyphBitmap> generate_mtsdf_glyph(
    MtsdfFaceGuard face_guard, std::uint32_t glyph_id, std::uint16_t em_px,
    std::uint16_t distance_range_px, std::uint16_t padding_px) {
    if (!face_guard.valid() || em_px == 0 || distance_range_px == 0) {
        return std::nullopt;
    }
    FT_Face face = face_guard.face;

    // ── Narrow critical section (TICKET-HOTPATH-TEXT-MTSDF-CONTENTION) ────
    //
    // FT_Face is mutable (FT_Load_Glyph updates face->glyph) and the face is
    // owned by the FreeType face cache, so FreeType state needs a guard. That
    // guard used to live for the WHOLE function, which serialized the
    // O(width*height*edges) distance-field construction of every glyph from
    // every worker behind one process-wide mutex — a hard ceiling on
    // subtitle / multi-font / cold-atlas bursts.
    //
    // The guard now covers only the FreeType-owned work: load, control-box
    // read, outline decomposition (which reads the face-owned FT_Outline
    // through our callbacks) and the advance read. Everything after it runs on
    // local data, unlocked and parallel.
    //
    // The guard is per-face: `MtsdfFaceGuard` carries the mutex of whoever
    // owns this face (a face-cache entry or a local owner), so glyphs from
    // different fonts rasterize concurrently instead of serializing behind
    // one process-wide lock (TICKET-HOTPATH-TEXT-MTSDF-CONTENTION).
    OutlineBuild build;
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    double advance_x = 0.0;
    {
        const auto wait_t0 = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(*face_guard.mutex);
        const auto hold_t0 = std::chrono::steady_clock::now();
        const MtsdfLockHoldMeter hold_meter{};
        if (profiling::g_current_counters) {
            profiling::g_current_counters->mtsdf_lock_wait_us.fetch_add(
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        hold_t0 - wait_t0).count()),
                std::memory_order_relaxed);
        }

        if (FT_Load_Glyph(face, glyph_id, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0 ||
            face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
            return std::nullopt;
        }

        FT_BBox box{};
        FT_Outline_Get_CBox(&face->glyph->outline, &box);
        min_x = static_cast<double>(box.xMin) / 64.0;
        max_x = static_cast<double>(box.xMax) / 64.0;
        min_y = -static_cast<double>(box.yMax) / 64.0;
        max_y = -static_cast<double>(box.yMin) / 64.0;
        if (!(max_x > min_x && max_y > min_y)) return std::nullopt;

        FT_Outline_Funcs funcs{};
        funcs.move_to = outline_move_to;
        funcs.line_to = outline_line_to;
        funcs.conic_to = outline_conic_to;
        funcs.cubic_to = outline_cubic_to;
        if (FT_Outline_Decompose(&face->glyph->outline, &funcs, &build) != 0) {
            return std::nullopt;
        }
        // Read every face-owned scalar INSIDE the guard: after it, `face` may
        // be re-loaded by another worker.
        advance_x = static_cast<double>(face->glyph->advance.x / 64.0);
    }
    // ── Unlocked, parallel section ───────────────────────────────────────
    finish_contour(build);
    if (build.contours.empty()) return std::nullopt;

    const double pad = static_cast<double>(padding_px);
    const std::uint32_t width = static_cast<std::uint32_t>(
        std::ceil(max_x - min_x) + 2.0 * pad);
    const std::uint32_t height = static_cast<std::uint32_t>(
        std::ceil(max_y - min_y) + 2.0 * pad);
    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        return std::nullopt;
    }

    std::vector<Edge> edges;
    for (const auto& contour : build.contours) {
        const auto count = contour.points.size();
        for (std::size_t i = 0; i < count; ++i) {
            const auto& a = contour.points[i];
            const auto& b = contour.points[(i + 1) % count];
            if (std::hypot(b.x - a.x, b.y - a.y) > 1e-7) {
                edges.push_back(Edge{a, b,
                                     static_cast<std::uint32_t>(i % 3)});
            }
        }
    }
    if (edges.empty()) return std::nullopt;
    const std::vector<Edge> boundary_edges = extract_boundary_edges(edges);

    const double origin_x = min_x - pad;
    const double origin_y = min_y - pad;
    const double range = static_cast<double>(distance_range_px);
    MtsdfGlyphBitmap result;
    result.width = width;
    result.height = height;
    // Keep the fractional origin: discarding it here quantises the glyph
    // plane to em pixels, which becomes a visible sub-pixel/2 px shift once
    // the canonical em_px atlas is scaled to the runtime font size. The
    // distance field was generated around this exact origin, so the plane
    // must describe the same origin.
    result.x_offset = static_cast<float>(origin_x);
    result.y_offset = static_cast<float>(origin_y);
    result.advance_x = static_cast<float>(advance_x);
    result.rgba8.resize(static_cast<std::size_t>(width) * height);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const Point p{origin_x + static_cast<double>(x) + 0.5,
                          origin_y + static_cast<double>(y) + 0.5};
            std::array<double, 3> channel_distance{
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
            double true_distance = std::numeric_limits<double>::infinity();
            for (const auto& edge : boundary_edges) {
                const double distance = point_segment_distance(p, edge);
                channel_distance[edge.channel] =
                    std::min(channel_distance[edge.channel], distance);
                true_distance = std::min(true_distance, distance);
            }
            const int winding = compute_winding(p, edges);
            const double sign = winding != 0 ? 1.0 : -1.0;
            for (auto& distance : channel_distance) {
                if (!std::isfinite(distance)) distance = true_distance;
                distance *= sign;
            }
            true_distance *= sign;
            result.rgba8[static_cast<std::size_t>(y) * width + x] =
                packed_rgba8(encode_distance(channel_distance[0], range),
                             encode_distance(channel_distance[1], range),
                             encode_distance(channel_distance[2], range),
                             encode_distance(true_distance, range));
        }
    }
    if (profiling::g_current_counters) {
        profiling::g_current_counters->mtsdf_glyphs_generated.fetch_add(
            1, std::memory_order_relaxed);
    }
    return result;
}

} // namespace chronon3d

#endif // CHRONON3D_ENABLE_TEXT
