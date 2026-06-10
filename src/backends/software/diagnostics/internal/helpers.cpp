// ---------------------------------------------------------------------------
// diagnostics/internal/helpers.cpp
// ---------------------------------------------------------------------------

#include "helpers.hpp"
#include "../../rasterizers/line_rasterizer.hpp"
#include <chronon3d/core/enum_utils.hpp>

namespace chronon3d::renderer::diagnostics::internal {

void draw_crosshair(Framebuffer& fb, Vec2 center, f32 radius, const Color& color) {
    renderer::bline(fb, {center.x - radius, center.y}, {center.x + radius, center.y}, color);
    renderer::bline(fb, {center.x, center.y - radius}, {center.x, center.y + radius}, color);
}

const char* shape_type_name(ShapeType type) {
    static thread_local std::string name;
    name = std::string(enum_utils::enum_name_exact(type));
    return name.empty() ? "Unknown" : name.c_str();
}

} // namespace chronon3d::renderer::diagnostics::internal
