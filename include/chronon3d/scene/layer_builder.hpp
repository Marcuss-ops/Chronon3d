#pragma once

#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/builder_params.hpp>
#include <chronon3d/scene/mask.hpp>
#include <chronon3d/scene/effect_stack.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/renderer/video/video_frame_provider.hpp>
#include <chronon3d/math/mat4.hpp>
#include <string>
#include <memory_resource>

namespace chronon3d {

class LayerBuilder {
public:
    explicit LayerBuilder(std::string name,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : m_layer(res) {
        m_layer.name = std::pmr::string{name, res};
    }

    LayerBuilder& parent(std::string name) {
        m_layer.parent_name = std::pmr::string{name, m_layer.name.get_allocator()};
        return *this;
    }

    LayerBuilder& from(Frame frame) {
        m_layer.from = frame;
        return *this;
    }

    LayerBuilder& duration(Frame frames) {
        m_layer.duration = frames;
        return *this;
    }

    LayerBuilder& visible(bool value) {
        m_layer.visible = value;
        return *this;
    }

    LayerBuilder& kind(LayerKind value) {
        m_layer.kind = value;
        return *this;
    }

    LayerBuilder& position(Vec3 p) {
        m_layer.transform.position = p;
        return *this;
    }

    LayerBuilder& scale(Vec3 s) {
        m_layer.transform.scale = s;
        return *this;
    }

    LayerBuilder& rotate(Vec3 euler_deg) {
        m_layer.transform.rotation = math::from_euler(euler_deg);
        return *this;
    }

    LayerBuilder& anchor(Vec3 a) {
        m_layer.transform.anchor = a;
        return *this;
    }

    LayerBuilder& opacity(f32 value) {
        m_layer.transform.opacity = value;
        return *this;
    }

    LayerBuilder& enable_3d(bool value = true) {
        m_layer.is_3d = value;
        return *this;
    }

    LayerBuilder& mask_rect(RectMaskParams p) {
        m_layer.mask.type     = MaskType::Rect;
        m_layer.mask.size     = p.size;
        m_layer.mask.pos      = p.pos;
        m_layer.mask.inverted = p.inverted;
        return *this;
    }

    LayerBuilder& mask_rounded_rect(RoundedRectMaskParams p) {
        m_layer.mask.type     = MaskType::RoundedRect;
        m_layer.mask.size     = p.size;
        m_layer.mask.radius   = p.radius;
        m_layer.mask.pos      = p.pos;
        m_layer.mask.inverted = p.inverted;
        return *this;
    }

    // Depth role — sets layer Z semantically; depth_offset fine-tunes within the role.
    // The role is resolved to a world Z in build(), so call order with position() does not matter.
    LayerBuilder& depth_role(DepthRole role) { m_layer.depth_role = role; return *this; }
    LayerBuilder& depth_offset(f32 offset)   { m_layer.depth_offset = offset; return *this; }

    // Effects — each call appends to the ordered stack.
    LayerBuilder& blur(f32 radius) {
        m_layer.effects.push_back({BlurParams{radius}});
        return *this;
    }
    LayerBuilder& tint(Color color, f32 amount = 1.0f) {
        m_layer.effects.push_back({TintParams{color, amount}});
        return *this;
    }
    LayerBuilder& brightness(f32 v) {
        m_layer.effects.push_back({BrightnessParams{v}});
        return *this;
    }
    LayerBuilder& contrast(f32 v) {
        m_layer.effects.push_back({ContrastParams{v}});
        return *this;
    }
    LayerBuilder& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f) {
        m_layer.effects.push_back({DropShadowParams{offset, color, radius}});
        return *this;
    }
    LayerBuilder& glow(f32 radius, f32 intensity = 0.8f, Color color = Color::white()) {
        m_layer.effects.push_back({GlowParams{radius, intensity, color}});
        return *this;
    }
    LayerBuilder& bloom(f32 threshold = 0.80f, f32 radius = 24.0f, f32 intensity = 0.60f) {
        m_layer.effects.push_back({BloomParams{threshold, radius, intensity}});
        return *this;
    }
    LayerBuilder& bloom_soft()   { return bloom(0.85f, 18.0f, 0.35f); }
    LayerBuilder& bloom_medium() { return bloom(0.80f, 28.0f, 0.60f); }
    LayerBuilder& bloom_strong() { return bloom(0.75f, 42.0f, 0.85f); }
    LayerBuilder& blend(BlendMode mode) { m_layer.blend_mode = mode; return *this; }

    // Layout rules
    LayerBuilder& pin_to(Anchor anchor, f32 margin = 0.0f) {
        m_layer.layout.enabled = true;
        m_layer.layout.pin     = anchor;
        m_layer.layout.margin  = margin;
        return *this;
    }
    LayerBuilder& keep_in_safe_area(SafeArea area = {}) {
        m_layer.layout.enabled           = true;
        m_layer.layout.keep_in_safe_area = true;
        m_layer.layout.safe_area         = area;
        return *this;
    }
    LayerBuilder& fit_text() {
        m_layer.layout.enabled  = true;
        m_layer.layout.fit_text = true;
        return *this;
    }

    LayerBuilder& mask_circle(CircleMaskParams p) {
        m_layer.mask.type     = MaskType::Circle;
        m_layer.mask.size     = {p.radius * 2.0f, p.radius * 2.0f};
        m_layer.mask.radius   = p.radius;
        m_layer.mask.pos      = p.pos;
        m_layer.mask.inverted = p.inverted;
        return *this;
    }

    LayerBuilder& rect(std::string name, RectParams p) {
        RenderNode node(m_layer.nodes.get_allocator().resource());
        node.name = std::pmr::string{name, m_layer.nodes.get_allocator().resource()};
        node.shape.type = ShapeType::Rect;
        node.shape.rect.size = p.size;
        node.world_transform.position = p.pos;
        node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
        node.color = p.color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& rounded_rect(std::string name, RoundedRectParams p) {
        RenderNode node(m_layer.nodes.get_allocator().resource());
        node.name = std::pmr::string{name, m_layer.nodes.get_allocator().resource()};
        node.shape.type = ShapeType::RoundedRect;
        node.shape.rounded_rect.size = p.size;
        node.shape.rounded_rect.radius = p.radius;
        node.world_transform.position = p.pos;
        node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
        node.color = p.color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& circle(std::string name, CircleParams p) {
        RenderNode node(m_layer.nodes.get_allocator().resource());
        node.name = std::pmr::string{name, m_layer.nodes.get_allocator().resource()};
        node.shape.type = ShapeType::Circle;
        node.shape.circle.radius = p.radius;
        node.world_transform.position = p.pos;
        node.world_transform.anchor = {p.radius, p.radius, 0.0f};
        node.color = p.color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& line(std::string name, LineParams p) {
        RenderNode node(m_layer.nodes.get_allocator().resource());
        node.name = std::pmr::string{name, m_layer.nodes.get_allocator().resource()};
        node.shape.type = ShapeType::Line;
        node.shape.line.to = p.to - p.from;
        node.shape.line.thickness = p.thickness;
        node.world_transform.position = p.from;
        node.color = p.color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& text(std::string name, TextParams p) {
        RenderNode node(m_layer.nodes.get_allocator().resource());
        node.name = std::pmr::string{name, m_layer.nodes.get_allocator().resource()};
        node.shape.type = ShapeType::Text;
        node.shape.text.text  = std::move(p.content);
        node.shape.text.style = p.style;
        node.shape.text.box   = p.box;
        node.world_transform.position = p.pos;
        node.color = p.style.color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& image(std::string name, ImageParams p) {
        RenderNode node(m_layer.nodes.get_allocator().resource());
        auto* res = m_layer.nodes.get_allocator().resource();

        node.name = std::pmr::string{name, res};
        node.shape.type = ShapeType::Image;
        node.shape.image.path = std::move(p.path);
        node.shape.image.size = p.size;
        node.shape.image.opacity = p.opacity;
        node.world_transform.position = p.pos;
        node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
        node.color = Color{1, 1, 1, p.opacity};

        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& fake_box3d(std::string name, FakeBox3DParams p) {
        auto* res = m_layer.nodes.get_allocator().resource();
        RenderNode node(res);
        node.name = std::pmr::string{name, res};
        node.shape.type = ShapeType::FakeBox3D;
        node.shape.fake_box3d.world_pos  = p.pos;
        node.shape.fake_box3d.size       = p.size;
        node.shape.fake_box3d.depth      = p.depth;
        node.shape.fake_box3d.color      = p.color;
        node.shape.fake_box3d.top_tint   = p.top_tint;
        node.shape.fake_box3d.side_tint  = p.side_tint;
        node.world_transform.position    = {0, 0, 0};
        node.world_transform.anchor      = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
        node.color = p.color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& fake_extruded_text(std::string name, FakeExtrudedTextParams p) {
        auto* res = m_layer.nodes.get_allocator().resource();
        RenderNode node(res);
        node.name = std::pmr::string{name, res};
        node.shape.type = ShapeType::FakeExtrudedText;
        auto& s = node.shape.fake_extruded_text;
        s.text              = std::move(p.text);
        s.font_path         = std::move(p.font_path);
        s.font_size         = p.font_size;
        s.align             = p.align;
        s.world_pos         = p.pos;
        s.depth             = p.depth;
        s.extrude_dir       = p.extrude_dir;
        s.extrude_z_step    = p.extrude_z_step;
        s.front_color       = p.front_color;
        s.side_color        = p.side_color;
        s.highlight_opacity = p.highlight_opacity;
        s.side_fade         = p.side_fade;
        node.world_transform.position = {0, 0, 0};
        node.color = p.front_color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& grid_plane(std::string name, GridPlaneParams p) {
        auto* res = m_layer.nodes.get_allocator().resource();
        RenderNode node(res);
        node.name = std::pmr::string{name, res};
        node.shape.type = ShapeType::GridPlane;
        node.shape.grid_plane.world_pos = p.pos;
        node.shape.grid_plane.axis      = p.axis;
        node.shape.grid_plane.extent    = p.extent;
        node.shape.grid_plane.spacing   = p.spacing;
        node.shape.grid_plane.color     = p.color;
        node.world_transform.position   = {0, 0, 0};
        node.color = p.color;
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    LayerBuilder& with_shadow(DropShadow shadow) {
        if (!m_layer.nodes.empty()) m_layer.nodes.back().shadow = shadow;
        return *this;
    }

    LayerBuilder& with_glow(Glow glow) {
        if (!m_layer.nodes.empty()) m_layer.nodes.back().glow = glow;
        return *this;
    }

    // Node-level transformations (applies to the last added node)
    LayerBuilder& at(Vec3 pos) {
        if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.position = pos;
        return *this;
    }
    LayerBuilder& rotate_node(Vec3 euler_deg) {
        if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.rotation = math::from_euler(euler_deg);
        return *this;
    }
    LayerBuilder& scale_node(Vec3 s) {
        if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.scale = s;
        return *this;
    }
    LayerBuilder& anchor_node(Vec3 a) {
        if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.anchor = a;
        return *this;
    }
    LayerBuilder& node_opacity(f32 a) {
        if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.opacity = a;
        return *this;
    }

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_layer.nodes.get_allocator().resource(); }


    // Video layer: resolves frame via VideoFrameProvider and adds as image node.
    // comp_frame and comp_fps are required to compute the source timestamp.
    LayerBuilder& video(std::string name, const VideoDesc& v,
                        Frame comp_frame, float comp_fps,
                        VideoFrameProvider& provider) {
        const std::string png = provider.frame_path(v, comp_frame, comp_fps);
        if (png.empty()) return *this;  // ffmpeg unavailable or failed

        auto* res = m_layer.nodes.get_allocator().resource();
        RenderNode node(res);
        node.name = std::pmr::string{name, res};
        node.shape.type             = ShapeType::Image;
        node.shape.image.path       = png;
        node.shape.image.size       = (v.size.x > 0 && v.size.y > 0) ? v.size : Vec2{640,360};
        node.shape.image.opacity    = v.opacity;
        node.world_transform.position = v.pos;
        node.world_transform.anchor   = {node.shape.image.size.x * 0.5f,
                                          node.shape.image.size.y * 0.5f, 0.0f};
        node.color = Color{1, 1, 1, v.opacity};
        m_layer.nodes.push_back(std::move(node));
        return *this;
    }

    [[nodiscard]] Layer build() {
        // Apply depth role last so it always wins over any explicit position.z.
        if (m_layer.depth_role != DepthRole::None) {
            m_layer.transform.position.z =
                DepthRoleResolver::z_for(m_layer.depth_role) + m_layer.depth_offset;
        }
        return std::move(m_layer);
    }

private:
    Layer m_layer;
};

} // namespace chronon3d
