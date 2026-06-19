#include <chronon3d/scene/model/render/render_node_factory.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <utility>

namespace chronon3d {

RenderNode RenderNodeFactory::base(std::pmr::memory_resource* res, std::string name) {
    RenderNode node(res);
    node.name = std::pmr::string{std::move(name), res};
    node.surface_policy = SurfacePolicy::IntrinsicSize;
    node.transform_policy = TransformPolicy::MatrixOnly;
    return node;
}

RenderNode RenderNodeFactory::rect(std::pmr::memory_resource* res, std::string name, const RectParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Rect;
    node.shape.rect.size = p.size;
    node.shape.rect.stroke = p.stroke.to_shape_stroke();
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.has_value() ? p.fill->to_fill() : Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::rounded_rect(std::pmr::memory_resource* res, std::string name, const RoundedRectParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::RoundedRect;
    node.shape.rounded_rect.size = p.size;
    node.shape.rounded_rect.radius = p.radius;
    node.shape.rounded_rect.stroke = p.stroke.to_shape_stroke();
    node.corner_radius = p.radius;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.has_value() ? p.fill->to_fill() : Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::circle(std::pmr::memory_resource* res, std::string name, const CircleParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Circle;
    node.shape.circle.radius = p.radius;
    node.shape.circle.stroke = p.stroke.to_shape_stroke();
    node.world_transform.anchor = {p.radius, p.radius, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.has_value() ? p.fill->to_fill() : Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::line(std::pmr::memory_resource* res, std::string name, const LineParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Line;
    node.shape.line.to = p.to - p.from;
    node.shape.line.thickness = p.thickness;
    node.shape.line.stroke.trim_start = p.stroke.trim_start;
    node.shape.line.stroke.trim_end = p.stroke.trim_end;
    node.shape.line.stroke.enabled = p.stroke.enabled;
    node.shape.line.stroke.color = p.stroke.color;
    node.shape.line.stroke.width = p.stroke.width;
    node.shape.line.stroke.alignment = p.stroke.alignment;
    node.world_transform.position = p.from;
    node.world_transform.anchor = {0, 0, 0};
    node.color = p.color;
    node.fill = Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::path(std::pmr::memory_resource* res, std::string name, PathParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Path;
    node.shape.path.commands = std::move(p.commands);
    node.shape.path.stroke = p.stroke;
    node.shape.path.fill = p.fill;
    node.shape.path.closed = p.closed;
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill;
    return node;
}

RenderNode RenderNodeFactory::image(std::pmr::memory_resource* res, std::string name, ImageParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Image;
    node.shape.image.path = std::move(p.path);
    node.shape.image.size = p.size;
    node.shape.image.fit = p.fit;
    node.shape.image.focal_point = p.focal_point;
    node.shape.image.crop = p.crop;
    node.shape.image.opacity = p.opacity;
    node.shape.image.radius = p.radius;
    node.corner_radius = p.radius;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = Color{1, 1, 1, p.opacity};
    return node;
}

RenderNode RenderNodeFactory::tiled_image(std::pmr::memory_resource* res, std::string name, ImageParams p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::TiledImage;
    node.shape.image.path = std::move(p.path);
    node.shape.image.size = p.size;
    node.shape.image.opacity = p.opacity;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {0, 0, 0};
    node.color = Color{1, 1, 1, p.opacity};
    return node;
}

RenderNode RenderNodeFactory::grid_background(std::pmr::memory_resource* res, std::string name, const GridBackgroundParams& p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::GridBackground;
    node.surface_policy = SurfacePolicy::ViewportSize;
    node.transform_policy = TransformPolicy::RasterizeAfter;
    node.shape.grid_background.size = p.size;
    node.shape.grid_background.offset = p.offset;
    node.shape.grid_background.bg_color = p.bg_color;
    node.shape.grid_background.grid_color = p.grid_color;
    node.shape.grid_background.spacing = p.spacing;
    node.shape.grid_background.minor_thickness = p.minor_thickness;
    node.shape.grid_background.major_thickness = p.major_thickness;
    node.shape.grid_background.major_every = p.major_every;
    node.shape.grid_background.centered = p.centered;
    node.world_transform.position = {0.0f, 0.0f, 0.0f};
    node.world_transform.anchor = {0.0f, 0.0f, 0.0f};
    node.color = p.bg_color;
    return node;
}

// ── resolve_text_anchor — maps TextAnchor enum to world_transform anchor ──
// Determines which point of the text box corresponds to TextParams.pos.
static Vec3 resolve_text_anchor(TextAnchor anchor, Vec2 box) {
    switch (anchor) {
        case TextAnchor::TopLeft:        return {0.0f, 0.0f, 0.0f};
        case TextAnchor::TopCenter:      return {box.x * 0.5f, 0.0f, 0.0f};
        case TextAnchor::Center:         return {box.x * 0.5f, box.y * 0.5f, 0.0f};
        case TextAnchor::BottomCenter:   return {box.x * 0.5f, box.y, 0.0f};
        case TextAnchor::BaselineLeft:   return {0.0f, box.y * 0.5f, 0.0f};
        case TextAnchor::BaselineCenter: return {box.x * 0.5f, box.y * 0.5f, 0.0f};
    }
    return {box.x * 0.5f, box.y * 0.5f, 0.0f}; // fallback = Center
}

RenderNode RenderNodeFactory::text(std::pmr::memory_resource* res, std::string name, TextSpec p) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::Text;
    node.shape.text.text = std::move(p.content.value);
    // Fallback: if the caller left font_path empty, use the project default
    // (Inter-Bold). Without this, rasterize_text_to_bl_image() returns
    // std::nullopt and the text is silently dropped from the render.
    if (p.font.font_path.empty()) {
        p.font.font_path = "assets/fonts/Inter-Bold.ttf";
    }
    node.shape.text.style.font_path = std::move(p.font.font_path);
    node.shape.text.style.font_family = std::move(p.font.font_family);
    node.shape.text.style.font_weight = p.font.font_weight;
    node.shape.text.style.font_style = std::move(p.font.font_style);
    node.shape.text.style.size = p.font.font_size;
    node.shape.text.style.color = p.appearance.color;
    // ── TextAnchor: anchor determines the box attachment point ──
    node.shape.text.style.anchor    = p.layout.anchor;
    // ── Intra-box alignment: separate from box anchoring ──
    node.shape.text.style.align         = p.layout.align;
    node.shape.text.style.vertical_align = p.layout.vertical_align;
    node.shape.text.style.line_height = p.layout.line_height;
    node.shape.text.style.tracking = p.layout.tracking;
    node.shape.text.style.centering_mode = p.layout.centering_mode;
    node.shape.text.style.box_style = p.appearance.box_style;
    node.shape.text.style.paint = p.appearance.paint;
    node.shape.text.style.shadows = std::move(p.appearance.shadows);
    node.shape.text.style.material = std::move(p.appearance.material);

    node.shape.text.style.auto_fit = p.layout.auto_fit;
    node.shape.text.style.auto_scale = p.layout.auto_fit;
    node.shape.text.style.max_lines = p.layout.max_lines;
    node.shape.text.style.ellipsis = p.layout.ellipsis;
    node.shape.text.style.min_size = p.layout.min_font_size;
    node.shape.text.style.max_size = p.layout.max_font_size;
    node.shape.text.style.overflow = p.layout.overflow;
    node.shape.text.style.wrap = p.layout.wrap;
    node.shape.text.style.pre_shaped = std::move(p.content.pre_shaped);

    node.shape.text.box.enabled = true;
    node.shape.text.box.size = p.layout.box;
    node.world_transform.anchor = resolve_text_anchor(p.layout.anchor, p.layout.box);
    node.world_transform.position = p.position;
    node.color = p.appearance.color;
    node.fill = Fill::solid_color(p.appearance.color);
    return node;
}

// ═══════════════════════════════════════════════════════════════════════════
// text_run factory
// ═══════════════════════════════════════════════════════════════════════════

RenderNode RenderNodeFactory::text_run(
    std::pmr::memory_resource* res,
    std::string name,
    TextRunSpec p,    // canonical composable (TextRunParams was the prior alias)
    FontEngine* engine,
    SampleTime sample_time
) {
    auto node = base(res, std::move(name));
    node.shape.type = ShapeType::TextRun;
    node.is_text_run_shape = true;
    node.font_engine = engine;

    // World transform from TextRunSpec (deep-nested field paths).
    node.world_transform.position = p.text.position;
    node.world_transform.anchor = Vec3{0.0f, 0.0f, 0.0f};

#ifdef CHRONON3D_USE_BLEND2D
    // ── Pass canonical composite TextRunSpec directly ───────────────
    auto shape = materialize_text_run_shape(p, engine, sample_time);
    if (!shape) {
        // Materialization failed (shaping / empty text).  Leave
        // text_run_shape null and `is_text_run_shape=true` so the
        // graph-builder source-pass routes to TextRunNode, which
        // will emit an `spdlog::error` for missing-shape.  Better
        // than silently dropping the entry.
    } else {
        node.text_run_shape = std::move(shape);
    }
#endif

    node.color = p.text.appearance.color;
    node.fill = Fill::solid_color(p.text.appearance.color);
    return node;
}

} // namespace chronon3d
