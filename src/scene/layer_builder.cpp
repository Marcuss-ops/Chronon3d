#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/layer_builder_delegates.hpp>
#include <chronon3d/scene/layer/track_matte.hpp>
#include <chronon3d/math/transform.hpp>
#include "layer_builder_internal.hpp"

#include <utility>

namespace chronon3d {

LayerBuilder::LayerBuilder(std::string name, std::pmr::memory_resource* res)
    : m_layer(res) {
    m_layer.name = std::pmr::string{name, res};
}

LayerBuilder& LayerBuilder::parent(std::string name) {
    m_layer.parent_name = std::pmr::string{name, m_layer.name.get_allocator()};
    return *this;
}

LayerBuilder& LayerBuilder::from(Frame frame) {
    m_layer.from = frame;
    return *this;
}

LayerBuilder& LayerBuilder::duration(Frame frames) {
    m_layer.duration = frames;
    return *this;
}

LayerBuilder& LayerBuilder::visible(bool value) {
    m_layer.visible = value;
    return *this;
}

LayerBuilder& LayerBuilder::kind(LayerKind value) {
    m_layer.kind = value;
    return *this;
}

LayerBuilder& LayerBuilder::position(Vec3 p) {
    m_layer.transform.position = p;
    return *this;
}

LayerBuilder& LayerBuilder::scale(Vec3 s) {
    m_layer.transform.scale = s;
    return *this;
}

LayerBuilder& LayerBuilder::rotate(Vec3 euler_deg) {
    m_layer.transform.rotation = math::from_euler(euler_deg);
    return *this;
}

LayerBuilder& LayerBuilder::anchor(Vec3 a) {
    m_layer.transform.anchor = a;
    return *this;
}

LayerBuilder& LayerBuilder::opacity(f32 value) {
    m_layer.transform.opacity = value;
    return *this;
}

LayerBuilder& LayerBuilder::enable_3d(bool value) {
    m_layer.is_3d = value;
    return *this;
}

LayerBuilder& LayerBuilder::depth_role(DepthRole role) { m_layer.depth_role = role; return *this; }
LayerBuilder& LayerBuilder::depth_offset(f32 offset)   { m_layer.depth_offset = offset; return *this; }

LayerBuilder& LayerBuilder::mask_rect(RectMaskParams p) {
    m_layer.mask.type = MaskType::Rect;
    m_layer.mask.size = p.size;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_rounded_rect(RoundedRectMaskParams p) {
    m_layer.mask.type = MaskType::RoundedRect;
    m_layer.mask.size = p.size;
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_circle(CircleMaskParams p) {
    m_layer.mask.type = MaskType::Circle;
    m_layer.mask.size = {p.radius * 2.0f, p.radius * 2.0f};
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::blur(f32 radius) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id="blur.gaussian"}, BlurParams{radius}});
    return *this;
}

LayerBuilder& LayerBuilder::tint(Color color, f32 amount) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id="color.tint"}, TintParams{color, amount}});
    return *this;
}

LayerBuilder& LayerBuilder::brightness(f32 v) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id="color.brightness"}, BrightnessParams{v}});
    return *this;
}

LayerBuilder& LayerBuilder::contrast(f32 v) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id="color.contrast"}, ContrastParams{v}});
    return *this;
}

LayerBuilder& LayerBuilder::drop_shadow(Vec2 offset, Color color, f32 radius) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id="light.drop_shadow"}, DropShadowParams{offset, color, radius}});
    return *this;
}

LayerBuilder& LayerBuilder::glow(f32 radius, f32 intensity, Color color) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id="light.glow"}, GlowParams{radius, intensity, color}});
    return *this;
}

LayerBuilder& LayerBuilder::bloom(f32 threshold, f32 radius, f32 intensity) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id="light.bloom"}, BloomParams{threshold, radius, intensity}});
    return *this;
}

LayerBuilder& LayerBuilder::blend(BlendMode mode) { m_layer.blend_mode = mode; return *this; }

LayerBuilder& LayerBuilder::pin_to(Anchor anchor, f32 margin) {
    return pin_to(AnchorPlacement{anchor}, margin);
}

LayerBuilder& LayerBuilder::pin_to(AnchorPlacement placement, f32 margin) {
    m_layer.layout.enabled = true;
    m_layer.layout.pin     = placement;
    m_layer.layout.margin  = margin;
    return *this;
}

LayerBuilder& LayerBuilder::keep_in_safe_area(SafeArea area) {
    m_layer.layout.enabled           = true;
    m_layer.layout.keep_in_safe_area = true;
    m_layer.layout.safe_area         = area;
    return *this;
}

LayerBuilder& LayerBuilder::fit_text() {
    m_layer.layout.enabled  = true;
    m_layer.layout.fit_text = true;
    return *this;
}

LayerBuilder& LayerBuilder::rect(std::string name, RectParams p) {
    layer_builder_internal::append_rect(m_layer, std::move(name), p);
    return *this;
}

LayerBuilder& LayerBuilder::rounded_rect(std::string name, RoundedRectParams p) {
    layer_builder_internal::append_rounded_rect(m_layer, std::move(name), p);
    return *this;
}

LayerBuilder& LayerBuilder::circle(std::string name, CircleParams p) {
    layer_builder_internal::append_circle(m_layer, std::move(name), p);
    return *this;
}

LayerBuilder& LayerBuilder::line(std::string name, LineParams p) {
    layer_builder_internal::append_line(m_layer, std::move(name), p);
    return *this;
}

LayerBuilder& LayerBuilder::text(std::string name, TextParams p) {
    layer_builder_internal::append_text(m_layer, std::move(name), std::move(p));
    return *this;
}

LayerBuilder& LayerBuilder::image(std::string name, ImageParams p) {
    layer_builder_internal::append_image(m_layer, std::move(name), std::move(p));
    return *this;
}

LayerBuilder& LayerBuilder::fake_box3d(std::string name, FakeBox3DParams p) {
    Layer3DDelegate::add_fake_box3d(m_layer, name, p);
    return *this;
}

LayerBuilder& LayerBuilder::fake_extruded_text(std::string name, FakeExtrudedTextParams p) {
    Layer3DDelegate::add_fake_extruded_text(m_layer, name, p);
    return *this;
}

LayerBuilder& LayerBuilder::grid_plane(std::string name, GridPlaneParams p) {
    Layer3DDelegate::add_grid_plane(m_layer, name, p);
    return *this;
}

LayerBuilder& LayerBuilder::with_shadow(DropShadow shadow) {
    layer_builder_internal::set_last_shadow(m_layer, shadow);
    return *this;
}

LayerBuilder& LayerBuilder::with_glow(Glow glow) {
    layer_builder_internal::set_last_glow(m_layer, glow);
    return *this;
}

LayerBuilder& LayerBuilder::accepts_lights(bool value) {
    m_layer.material.accepts_lights = value;
    return *this;
}

LayerBuilder& LayerBuilder::casts_shadows(bool value) {
    m_layer.material.casts_shadows = value;
    return *this;
}

LayerBuilder& LayerBuilder::accepts_shadows(bool value) {
    m_layer.material.accepts_shadows = value;
    return *this;
}

LayerBuilder& LayerBuilder::material(Material2_5D value) {
    m_layer.material = value;
    return *this;
}

LayerBuilder& LayerBuilder::at(Vec3 pos) {
    layer_builder_internal::set_last_position(m_layer, pos);
    return *this;
}

LayerBuilder& LayerBuilder::rotate_node(Vec3 euler_deg) {
    layer_builder_internal::set_last_rotation(m_layer, euler_deg);
    return *this;
}

LayerBuilder& LayerBuilder::scale_node(Vec3 s) {
    layer_builder_internal::set_last_scale(m_layer, s);
    return *this;
}

LayerBuilder& LayerBuilder::anchor_node(Vec3 a) {
    layer_builder_internal::set_last_anchor(m_layer, a);
    return *this;
}

LayerBuilder& LayerBuilder::node_opacity(f32 a) {
    layer_builder_internal::set_last_opacity(m_layer, a);
    return *this;
}

LayerBuilder& LayerBuilder::track_matte_alpha(std::string src) {
    m_layer.track_matte.type = TrackMatteType::Alpha;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}
LayerBuilder& LayerBuilder::track_matte_alpha_inverted(std::string src) {
    m_layer.track_matte.type = TrackMatteType::AlphaInverted;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}
LayerBuilder& LayerBuilder::track_matte_luma(std::string src) {
    m_layer.track_matte.type = TrackMatteType::Luma;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}
LayerBuilder& LayerBuilder::track_matte_luma_inverted(std::string src) {
    m_layer.track_matte.type = TrackMatteType::LumaInverted;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

LayerBuilder& LayerBuilder::video(video::VideoSource source) {
    m_layer.kind = LayerKind::Video;
    m_layer.video_source = std::make_unique<video::VideoSource>(std::move(source));
    return *this;
}

LayerBuilder& LayerBuilder::video(std::string path) {
    video::VideoSource source;
    source.path = std::move(path);
    return video(std::move(source));
}

Layer LayerBuilder::build() {
    if (m_layer.depth_role != DepthRole::None) {
        m_layer.transform.position.z =
            DepthRoleResolver::z_for(m_layer.depth_role) + m_layer.depth_offset;
    }
    return std::move(m_layer);
}

} // namespace chronon3d
