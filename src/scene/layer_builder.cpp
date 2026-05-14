#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/layer_builder_delegates.hpp>
#include <chronon3d/render_graph/render_node.hpp>
#include <chronon3d/math/transform.hpp>

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
    m_layer.mask.type     = MaskType::Rect;
    m_layer.mask.size     = p.size;
    m_layer.mask.pos      = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_rounded_rect(RoundedRectMaskParams p) {
    m_layer.mask.type     = MaskType::RoundedRect;
    m_layer.mask.size     = p.size;
    m_layer.mask.radius   = p.radius;
    m_layer.mask.pos      = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_circle(CircleMaskParams p) {
    m_layer.mask.type     = MaskType::Circle;
    m_layer.mask.size     = {p.radius * 2.0f, p.radius * 2.0f};
    m_layer.mask.radius   = p.radius;
    m_layer.mask.pos      = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::blur(f32 radius) {
    m_layer.effects.push_back({BlurParams{radius}});
    return *this;
}

LayerBuilder& LayerBuilder::tint(Color color, f32 amount) {
    m_layer.effects.push_back({TintParams{color, amount}});
    return *this;
}

LayerBuilder& LayerBuilder::brightness(f32 v) {
    m_layer.effects.push_back({BrightnessParams{v}});
    return *this;
}

LayerBuilder& LayerBuilder::contrast(f32 v) {
    m_layer.effects.push_back({ContrastParams{v}});
    return *this;
}

LayerBuilder& LayerBuilder::drop_shadow(Vec2 offset, Color color, f32 radius) {
    m_layer.effects.push_back({DropShadowParams{offset, color, radius}});
    return *this;
}

LayerBuilder& LayerBuilder::glow(f32 radius, f32 intensity, Color color) {
    m_layer.effects.push_back({GlowParams{radius, intensity, color}});
    return *this;
}

LayerBuilder& LayerBuilder::bloom(f32 threshold, f32 radius, f32 intensity) {
    m_layer.effects.push_back({BloomParams{threshold, radius, intensity}});
    return *this;
}

LayerBuilder& LayerBuilder::blend(BlendMode mode) { m_layer.blend_mode = mode; return *this; }

LayerBuilder& LayerBuilder::pin_to(Anchor anchor, f32 margin) {
    m_layer.layout.enabled = true;
    m_layer.layout.pin     = anchor;
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

LayerBuilder& LayerBuilder::rounded_rect(std::string name, RoundedRectParams p) {
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

LayerBuilder& LayerBuilder::circle(std::string name, CircleParams p) {
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

LayerBuilder& LayerBuilder::line(std::string name, LineParams p) {
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

LayerBuilder& LayerBuilder::text(std::string name, TextParams p) {
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

LayerBuilder& LayerBuilder::image(std::string name, ImageParams p) {
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
    if (!m_layer.nodes.empty()) m_layer.nodes.back().shadow = shadow;
    return *this;
}

LayerBuilder& LayerBuilder::with_glow(Glow glow) {
    if (!m_layer.nodes.empty()) m_layer.nodes.back().glow = glow;
    return *this;
}

LayerBuilder& LayerBuilder::at(Vec3 pos) {
    if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.position = pos;
    return *this;
}

LayerBuilder& LayerBuilder::rotate_node(Vec3 euler_deg) {
    if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.rotation = math::from_euler(euler_deg);
    return *this;
}

LayerBuilder& LayerBuilder::scale_node(Vec3 s) {
    if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.scale = s;
    return *this;
}

LayerBuilder& LayerBuilder::anchor_node(Vec3 a) {
    if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.anchor = a;
    return *this;
}

LayerBuilder& LayerBuilder::node_opacity(f32 a) {
    if (!m_layer.nodes.empty()) m_layer.nodes.back().world_transform.opacity = a;
    return *this;
}

LayerBuilder& LayerBuilder::video(video::VideoSource source) {
    m_layer.kind = LayerKind::Video;
    m_layer.video_source = std::move(source);
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
