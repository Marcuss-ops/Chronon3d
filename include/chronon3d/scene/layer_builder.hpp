#pragma once

#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/builder_params.hpp>
#include <chronon3d/scene/mask.hpp>
#include <chronon3d/scene/layer_effect.hpp>
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

    // Effects
    LayerBuilder& blur(f32 radius) { m_layer.effect.blur_radius = radius; return *this; }
    LayerBuilder& tint(Color color) { m_layer.effect.tint = color; return *this; }
    LayerBuilder& brightness(f32 v) { m_layer.effect.brightness = v; return *this; }
    LayerBuilder& contrast(f32 v)   { m_layer.effect.contrast   = v; return *this; }
    LayerBuilder& blend(BlendMode mode) { m_layer.blend_mode = mode; return *this; }

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
        node.shape.text.text = std::move(p.content);
        node.shape.text.style = p.style;
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

    LayerBuilder& with_shadow(DropShadow shadow) {
        m_layer.nodes.back().shadow = shadow;
        return *this;
    }

    LayerBuilder& with_glow(Glow glow) {
        m_layer.nodes.back().glow = glow;
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
