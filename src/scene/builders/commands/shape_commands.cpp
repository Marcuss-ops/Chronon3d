// Shape method implementations for LayerBuilder.
// Extracted from layer_builder.cpp to reduce file size.

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/registry/shape_ids.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace chronon3d {

// ── Layer3DDelegate ───────────────────────────────────────────────────

void Layer3DDelegate::add_fake_box3d(Layer& layer, std::string name, FakeBox3DParams p, FontEngine* font_engine) {
    auto* res = layer.nodes.get_allocator().resource();
    RenderNode node(res);
    node.name = std::pmr::string{name, res};
    node.shape.set_type(ShapeType::FakeBox3D);
    node.shape.fake_box3d().world_pos  = p.pos;
    node.shape.fake_box3d().size       = p.size;
    node.shape.fake_box3d().depth      = p.depth;
    node.shape.fake_box3d().color      = p.color;
    node.shape.fake_box3d().top_tint   = p.top_tint;
    node.shape.fake_box3d().side_tint  = p.side_tint;
    node.world_transform.position    = {0, 0, 0};
    node.world_transform.anchor      = {0.0f, 0.0f, 0.0f};
    node.color = p.color;
    node.font_engine = font_engine;
    layer.nodes.push_back(std::move(node));
}

void Layer3DDelegate::add_grid_plane(Layer& layer, std::string name, GridPlaneParams p, FontEngine* font_engine) {
    auto* res = layer.nodes.get_allocator().resource();
    RenderNode node(res);
    node.name = std::pmr::string{name, res};
    node.shape.set_type(ShapeType::GridPlane);
    node.shape.grid_plane().world_pos      = p.pos;
    node.shape.grid_plane().axis           = p.axis;
    node.shape.grid_plane().extent         = p.extent;
    node.shape.grid_plane().spacing        = p.spacing;
    node.shape.grid_plane().color          = p.color;
    node.shape.grid_plane().fade_distance  = p.fade_distance;
    node.shape.grid_plane().fade_min_alpha = p.fade_min_alpha;
    node.world_transform.position        = {0, 0, 0};
    node.color = p.color;
    node.font_engine = font_engine;
    layer.nodes.push_back(std::move(node));
}

// ── 2D Shapes ─────────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::rect(std::string name, RectParams p) {
    return shape(registry::shape_ids::Rect, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::rounded_rect(std::string name, RoundedRectParams p) {
    return shape(registry::shape_ids::RoundedRect, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::circle(std::string name, CircleParams p) {
    return shape(registry::shape_ids::Circle, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::line(std::string name, LineParams p) {
    return shape(registry::shape_ids::Line, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::path(std::string name, PathParams p) {
    return shape(registry::shape_ids::Path, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::arrow(std::string name, ArrowParams p) {
    return path(std::move(name), make_arrow(p));
}

LayerBuilder& LayerBuilder::star(std::string name, StarParams p) {
    return path(std::move(name), make_star(p));
}

LayerBuilder& LayerBuilder::badge(std::string name, BadgeParams p) {
    return path(std::move(name), make_badge(p));
}

LayerBuilder& LayerBuilder::speech_bubble(std::string name, SpeechBubbleParams p) {
    return path(std::move(name), make_speech_bubble(p));
}

LayerBuilder& LayerBuilder::callout(std::string name, CalloutParams p) {
    return path(std::move(name), make_callout(p));
}

LayerBuilder& LayerBuilder::progress_bar(std::string name, ProgressBarParams p) {
    PathParams bg_path;
    bg_path.commands = make_rounded_rect_commands({0.0f, 0.0f}, p.size, p.corner_radius);        bg_path.fill = p.background_style.fill.to_fill();
        bg_path.stroke = p.background_style.stroke.to_path_stroke();
        bg_path.pos = p.pos;
        bg_path.closed = true;
        path(name + "_bg", bg_path);

        if (p.progress > 0.0f) {
            PathParams fg_path;
            f32 fg_w = p.size.x * std::clamp(p.progress, 0.0f, 1.0f);
            Vec2 fg_center{ -p.size.x * 0.5f + fg_w * 0.5f, 0.0f };
            fg_path.commands = make_rounded_rect_commands(fg_center, {fg_w, p.size.y}, p.corner_radius);
            fg_path.fill = p.fill_style.fill.to_fill();
            fg_path.stroke = p.fill_style.stroke.to_path_stroke();
        fg_path.pos = p.pos;
        fg_path.closed = true;
        if (p.color != Color{1,1,1,1}) {
            fg_path.color = p.color;
        }
        path(name, fg_path);
    }
    return *this;
}

LayerBuilder& LayerBuilder::timeline_bar(std::string name, TimelineBarParams p) {
    PathParams bg_path;
    bg_path.commands = make_rounded_rect_commands({0.0f, 0.0f}, p.size, p.corner_radius);        bg_path.fill = p.background_style.fill.to_fill();
        bg_path.stroke = p.background_style.stroke.to_path_stroke();
        bg_path.pos = p.pos;
        bg_path.closed = true;
        path(name + "_bg", bg_path);

        f32 s = std::clamp(p.start, 0.0f, 1.0f);
        f32 e = std::clamp(p.end, 0.0f, 1.0f);
        if (e > s) {
            PathParams fg_path;
            f32 fg_w = p.size.x * (e - s);
            f32 start_pos = -p.size.x * 0.5f + s * p.size.x;
            Vec2 fg_center{ start_pos + fg_w * 0.5f, 0.0f };
            fg_path.commands = make_rounded_rect_commands(fg_center, {fg_w, p.size.y}, p.corner_radius);
            fg_path.fill = p.fill_style.fill.to_fill();
            fg_path.stroke = p.fill_style.stroke.to_path_stroke();
        fg_path.pos = p.pos;
        fg_path.closed = true;
        if (p.color != Color{1,1,1,1}) {
            fg_path.color = p.color;
        }
        path(name, fg_path);
    }
    return *this;
}

// ── Content Shapes ────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::image(std::string name, ImageParams p) {
    // Sequence V2: collect image asset reference
    if (!p.path.empty()) {
        m_layer.asset_manifest.add_image(p.path, std::string(m_layer.name) + "/" + name);
    }
    return shape(registry::shape_ids::Image, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::tiled_image(std::string name, ImageParams p) {
    // Sequence V2: collect image asset reference (same as image())
    if (!p.path.empty()) {
        m_layer.asset_manifest.add_image(p.path, std::string(m_layer.name) + "/" + name);
    }
    return shape(registry::shape_ids::TiledImage, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::grid_background(std::string name, GridBackgroundParams p) {
    return shape(registry::shape_ids::GridBackground, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::text(std::string name, TextSpec p) {
    // P1 — single canonical text pipeline
    // (docs/MIGRATION_TEXT_SPEC.md §9 acceptance: "rimuovi la shape diretta
    // Text dal registry, conserva solo TextRun. Anche quando animators è
    // vuoto, materializza TextRunNode").
    //
    // The legacy `Text` ShapeDescriptor was REMOVED from
    // chronon3d::registry::ShapeRegistry (only `TextRun` remains). To keep
    // the existing `LayerBuilder::text(name, TextSpec)` ergonomic — used by
    // 59+ content/ call sites — this method is now a transitional shim that
    // maps the flat TextSpec into a TextRunParams (with .text = std::move(p))
    // and routes through `text_run(name, params).commit()`. The downstream
    // RenderNode is flagged ShapeType::TextRun in both cases; empty animators
    // still produce a valid TextRunShape via `materialize_text_run_shape`.
    TextRunParams run;
    run.text = std::move(p);
    return text_run(std::move(name), std::move(run)).commit();
}

LayerBuilder& LayerBuilder::shape(std::string_view id, std::string name, registry::ShapeParams params) {
    m_layer.nodes.push_back(m_shape_registry->create_node(
        id,
        m_layer.nodes.get_allocator().resource(),
        std::move(name),
        std::move(params)
    ));
    m_layer.nodes.back().font_engine = m_font_engine;
    return *this;
}

// ── 3D Shapes (Delegated) ─────────────────────────────────────────────

LayerBuilder& LayerBuilder::fake_box3d(std::string name, FakeBox3DParams p) {
    Layer3DDelegate::add_fake_box3d(m_layer, name, p, m_font_engine);
    return *this;
}

LayerBuilder& LayerBuilder::grid_plane(std::string name, GridPlaneParams p) {
    Layer3DDelegate::add_grid_plane(m_layer, name, p, m_font_engine);
    return *this;
}

// ── Convenience Shapes ────────────────────────────────────────────────

LayerBuilder& LayerBuilder::fullscreen_rect(std::string name, Color color) {
    // FIX 4 (10-point friction audit): make fullscreen_rect canvas-correct
    // in BOTH modular_coordinates=true (root shift applied by the
    // resolver) and modular_coordinates=false (centered-rendering bake
    // added by the source pass / bbox collector).
    //
    // Mechanism: pin_to(Anchor::Center) marks this layer as canvas-centred
    // via the LAYOUT pin flag; the explicit pos offset (-w/2, -h/2) pre-
    // translates the rect into the negative half-plane so that the
    // downstream canvas-centre translation (added by either the resolver
    // for modular or the source pass for non-modular) produces the
    // intended full-canvas bbox (0,0)→(w,h).
    //
    // Without this offset the rect would land at (w/2,h/2)→(w,h) (only
    // bottom-right quadrant) because pin_to(Center) DOES NOT itself add
    // a transform translation; the offset MUST be made explicit here.
    // Do not simplify this back to pos=(0,0,0) — it reintroduces the
    // half-canvas bug.  Regression test:
    //   tests/scene/test_fullscreen_rect_modular_bbox.cpp
    pin_to(Anchor::Center);
    return rect(std::move(name), {
        .size = { m_screen_width, m_screen_height },
        .color = color,
        .pos = {
            -m_screen_width * 0.5f,
            -m_screen_height * 0.5f,
            0.0f
        }
    });
}

LayerBuilder& LayerBuilder::fill(Color color) {
    return fullscreen_rect("fill", color);
}

} // namespace chronon3d
