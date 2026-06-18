#include "blend2d_path_builder.hpp"

#include <glm/glm.hpp>

namespace chronon3d::blend2d_bridge::path_builder {

// Helper: project a single path-command point through the model matrix
// and re-base it to the ROI origin.  Mirrors the legacy
// `transform_point(model, vec)` from `path_rasterizer.cpp` kept here
// as an explicitly-local helper to avoid pulling in path_utils.hpp
// (which transitively depends on the path_cache LRU — overkill for a
// pure function in a screen-space builder).
namespace {
[[nodiscard]] inline chronon3d::Vec2 project_point(const chronon3d::Mat4& m,
                                                    chronon3d::Vec2 p,
                                                    chronon3d::Vec2 origin) {
    const chronon3d::Vec4 r = m * chronon3d::Vec4(p.x, p.y, 0.0f, 1.0f);
    return { r.x - origin.x, r.y - origin.y };
}
} // anonymous namespace

BLPath make_bl_path(const chronon3d::PathShape& path,
                    const chronon3d::Mat4& model,
                    chronon3d::Vec2 bbox_origin)
{
    BLPath out;
    if (path.commands.empty()) return out;

    for (const auto& cmd : path.commands) {
        switch (cmd.type) {
            case chronon3d::PathCommandType::MoveTo: {
                const chronon3d::Vec2 p = project_point(model, cmd.p0, bbox_origin);
                out.moveTo(static_cast<double>(p.x), static_cast<double>(p.y));
                break;
            }
            case chronon3d::PathCommandType::LineTo: {
                const chronon3d::Vec2 p = project_point(model, cmd.p0, bbox_origin);
                out.lineTo(static_cast<double>(p.x), static_cast<double>(p.y));
                break;
            }
            case chronon3d::PathCommandType::QuadraticTo: {
                // BL2 has first-class `quadTo` for true quadratic B-splines;
                // prefer it over `conicTo(..., weight=1.0)`.  Control-point
                // is `p1`, end is `p0` per the Chronon3d convention.
                const chronon3d::Vec2 cp = project_point(model, cmd.p1, bbox_origin);
                const chronon3d::Vec2 p  = project_point(model, cmd.p0, bbox_origin);
                out.quadTo(
                    static_cast<double>(cp.x), static_cast<double>(cp.y),
                    static_cast<double>(p.x),  static_cast<double>(p.y));
                break;
            }
            case chronon3d::PathCommandType::CubicTo: {
                const chronon3d::Vec2 c1 = project_point(model, cmd.p1, bbox_origin);
                const chronon3d::Vec2 c2 = project_point(model, cmd.p2, bbox_origin);
                const chronon3d::Vec2 p  = project_point(model, cmd.p0, bbox_origin);
                out.cubicTo(
                    static_cast<double>(c1.x), static_cast<double>(c1.y),
                    static_cast<double>(c2.x), static_cast<double>(c2.y),
                    static_cast<double>(p.x),  static_cast<double>(p.y));
                break;
            }
            case chronon3d::PathCommandType::Close: {
                out.close();
                break;
            }
        }
    }
    return out;
}

} // namespace chronon3d::blend2d_bridge::path_builder
