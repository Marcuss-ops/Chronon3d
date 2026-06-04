#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/scene/layer/track_matte.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <cmath>
#include <utility>

namespace chronon3d {

namespace layer_builder_internal {

RenderNode* last_node(Layer& layer) {
    if (layer.nodes.empty()) {
        return nullptr;
    }
    return &layer.nodes.back();
}

void set_last_shadow(Layer& layer, DropShadow shadow) {
    if (auto* node = last_node(layer)) {
        node->shadow = shadow;
    }
}

void set_last_glow(Layer& layer, Glow glow) {
    if (auto* node = last_node(layer)) {
        node->glow = glow;
    }
}

void set_last_position(Layer& layer, Vec3 pos) {
    if (auto* node = last_node(layer)) {
        node->world_transform.position = pos;
    }
}

void set_last_rotation(Layer& layer, Vec3 euler_deg) {
    if (auto* node = last_node(layer)) {
        node->world_transform.rotation = glm::quat(glm::radians(euler_deg));
    }
}

void set_last_scale(Layer& layer, Vec3 s) {
    if (auto* node = last_node(layer)) {
        node->world_transform.scale = s;
    }
}

void set_last_anchor(Layer& layer, Vec3 a) {
    if (auto* node = last_node(layer)) {
        node->world_transform.anchor = a;
    }
}

void set_last_opacity(Layer& layer, f32 opacity) {
    if (auto* node = last_node(layer)) {
        node->world_transform.opacity = opacity;
    }
}

} // namespace layer_builder_internal

void Layer3DDelegate::add_fake_box3d(Layer& layer, std::string name, FakeBox3DParams p, FontEngine* font_engine) {
    auto* res = layer.nodes.get_allocator().resource();
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
    node.world_transform.anchor      = {0.0f, 0.0f, 0.0f};
    node.color = p.color;
    node.font_engine = font_engine;
    layer.nodes.push_back(std::move(node));
}

void Layer3DDelegate::add_grid_plane(Layer& layer, std::string name, GridPlaneParams p, FontEngine* font_engine) {
    auto* res = layer.nodes.get_allocator().resource();
    RenderNode node(res);
    node.name = std::pmr::string{name, res};
    node.shape.type = ShapeType::GridPlane;
    node.shape.grid_plane.world_pos      = p.pos;
    node.shape.grid_plane.axis           = p.axis;
    node.shape.grid_plane.extent         = p.extent;
    node.shape.grid_plane.spacing        = p.spacing;
    node.shape.grid_plane.color          = p.color;
    node.shape.grid_plane.fade_distance  = p.fade_distance;
    node.shape.grid_plane.fade_min_alpha = p.fade_min_alpha;
    node.world_transform.position        = {0, 0, 0};
    node.color = p.color;
    node.font_engine = font_engine;
    layer.nodes.push_back(std::move(node));
}


LayerBuilder::LayerBuilder(std::string name, Frame current_frame, std::pmr::memory_resource* res)
    : m_layer(res), m_current_frame(current_frame) {
    m_layer.name = std::pmr::string{name, res};
}

LayerBuilder::LayerBuilder(std::string name, std::pmr::memory_resource* res)
    : LayerBuilder(std::move(name), Frame{0}, res) {}

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
    m_duration_explicit = true;
    m_until_frame.reset();
    return *this;
}

LayerBuilder& LayerBuilder::until(Frame frame) {
    m_until_frame = frame;
    m_duration_explicit = false;
    return *this;
}

LayerBuilder& LayerBuilder::offset(Frame frames) {
    m_layer.time_offset = frames;
    return *this;
}

// ── Time Remap (AE-4) ─────────────────────────────────────────────────

LayerBuilder& LayerBuilder::speed(f32 multiplier) {
    m_layer.time_remap.speed = multiplier;
    return *this;
}

LayerBuilder& LayerBuilder::reverse(bool value) {
    if (value) {
        m_layer.time_remap.speed = -1.0f;
    }
    return *this;
}

LayerBuilder& LayerBuilder::freeze_frame(Frame source_frame) {
    m_layer.time_remap.freeze_frame = source_frame;
    return *this;
}

LayerBuilder& LayerBuilder::time_remap(AnimatedValue<f32> curve) {
    m_layer.time_remap.time_remap = std::move(curve);
    return *this;
}

AnimatedValue<f32>& LayerBuilder::time_remap_anim() {
    return m_layer.time_remap.time_remap;
}

LayerBuilder& LayerBuilder::visible(bool value) {
    m_layer.visible = value;
    return *this;
}

LayerBuilder& LayerBuilder::kind(LayerKind value) {
    m_layer.kind = value;
    return *this;
}

LayerBuilder& LayerBuilder::cache_static(bool value) {
    m_layer.cache_static = value;
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
    m_layer.transform.rotation = glm::quat(glm::radians(euler_deg));
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
    m_layer.uses_2_5d_projection = value;
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
    bg_path.commands = make_rounded_rect_commands({0.0f, 0.0f}, p.size, p.corner_radius);
    bg_path.fill = p.background_style.fill;
    bg_path.stroke = p.background_style.stroke;
    bg_path.pos = p.pos;
    bg_path.closed = true;
    path(name + "_bg", bg_path);

    if (p.progress > 0.0f) {
        PathParams fg_path;
        f32 fg_w = p.size.x * std::clamp(p.progress, 0.0f, 1.0f);
        Vec2 fg_center{ -p.size.x * 0.5f + fg_w * 0.5f, 0.0f };
        fg_path.commands = make_rounded_rect_commands(fg_center, {fg_w, p.size.y}, p.corner_radius);
        fg_path.fill = p.fill_style.fill;
        fg_path.stroke = p.fill_style.stroke;
        fg_path.pos = p.pos;
        fg_path.closed = true;
        // Use color field if fill_style has solid white or custom color
        if (p.color != Color{1,1,1,1}) {
            fg_path.color = p.color;
        }
        path(name, fg_path);
    }
    return *this;
}

LayerBuilder& LayerBuilder::timeline_bar(std::string name, TimelineBarParams p) {
    PathParams bg_path;
    bg_path.commands = make_rounded_rect_commands({0.0f, 0.0f}, p.size, p.corner_radius);
    bg_path.fill = p.background_style.fill;
    bg_path.stroke = p.background_style.stroke;
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
        fg_path.fill = p.fill_style.fill;
        fg_path.stroke = p.fill_style.stroke;
        fg_path.pos = p.pos;
        fg_path.closed = true;
        if (p.color != Color{1,1,1,1}) {
            fg_path.color = p.color;
        }
        path(name, fg_path);
    }
    return *this;
}

LayerBuilder& LayerBuilder::image(std::string name, ImageParams p) {
    return shape(registry::shape_ids::Image, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::tiled_image(std::string name, ImageParams p) {
    return shape(registry::shape_ids::TiledImage, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::grid_background(std::string name, GridBackgroundParams p) {
    return shape(registry::shape_ids::GridBackground, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::text(std::string name, TextParams p) {
    return shape(registry::shape_ids::Text, std::move(name), std::move(p));
}

LayerBuilder& LayerBuilder::shape(std::string_view id, std::string name, registry::ShapeParams params) {
    m_layer.nodes.push_back(registry::ShapeRegistry::instance().create_node(
        id,
        m_layer.nodes.get_allocator().resource(),
        std::move(name),
        std::move(params)
    ));
    m_layer.nodes.back().font_engine = m_font_engine;
    return *this;
}

LayerBuilder& LayerBuilder::fake_box3d(std::string name, FakeBox3DParams p) {
    Layer3DDelegate::add_fake_box3d(m_layer, name, p, m_font_engine);
    return *this;
}

LayerBuilder& LayerBuilder::grid_plane(std::string name, GridPlaneParams p) {
    Layer3DDelegate::add_grid_plane(m_layer, name, p, m_font_engine);
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
LayerBuilder& LayerBuilder::transition_in(LayerTransitionSpec spec) {
    m_layer.transition_in = std::move(spec);
    return *this;
}
LayerBuilder& LayerBuilder::transition_out(LayerTransitionSpec spec) {
    m_layer.transition_out = std::move(spec);
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

LayerBuilder& LayerBuilder::video_size(Vec2 size) {
    if (m_layer.video_source) {
        m_layer.video_source->size = size;
    }
    return *this;
}

AnimatedValue<Vec3>& LayerBuilder::position_anim() { return m_layer.anim_transform.position; }
AnimatedValue<Vec3>& LayerBuilder::scale_anim()    { return m_layer.anim_transform.scale; }
AnimatedValue<Vec3>& LayerBuilder::rotate_anim()   { return m_layer.anim_transform.rotation_euler; }
AnimatedValue<Vec3>& LayerBuilder::anchor_anim()   { return m_layer.anim_transform.anchor; }
AnimatedValue<f32>&  LayerBuilder::opacity_anim()  { return m_layer.anim_transform.opacity; }
AnimatedValue<f32>&  LayerBuilder::blur_anim()    { return m_layer.anim_transform.blur; }

LayerBuilder& LayerBuilder::slide_in(Vec3 from, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, from, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::soft_pop(Frame duration) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.90f, 0.90f, 1.0f}, EasingCurve{Easing::OutBack});
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::float_idle(f32 amplitude_y, Frame cycle) {
    auto& pos = position_anim();
    pos.loop_mode(LoopMode::Loop);

    // Sample the wave at frame granularity so the transform stays on whole
    // pixels. This keeps translation-only layers on the integer fast path and
    // avoids sub-pixel bbox churn that would otherwise force framebuffer
    // bucket mismatches every frame.
    const int period = std::max(1, static_cast<int>(cycle));
    for (int f = 0; f <= period; ++f) {
        const f32 phase = static_cast<f32>(f) / static_cast<f32>(period);
        const f32 wave = std::sin(phase * 6.2831853071795864769f);
        const f32 snapped_y = std::round(wave * amplitude_y);
        pos.key(Frame{f}, Vec3{0.0f, snapped_y, 0.0f});
    }
    return *this;
}

LayerBuilder& LayerBuilder::depth_reveal(f32 depth_z, Frame duration) {
    m_layer.uses_2_5d_projection = true;

    auto& pos = position_anim();
    Vec3 start = m_layer.transform.position;
    start.z += depth_z;
    pos.key(Frame{0}, start, EasingCurve{Easing::OutCubic});
    pos.key(duration, m_layer.transform.position);

    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic});
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::card_flip_2_5d(Frame duration) {
    m_layer.uses_2_5d_projection = true;

    auto& rot = rotate_anim();
    Vec3 current_euler = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    Vec3 start_euler = current_euler;
    start_euler.y -= 90.0f;
    rot.key(Frame{0}, start_euler, EasingCurve{Easing::OutCubic});
    rot.key(duration, current_euler);

    auto& pos = position_anim();
    Vec3 start_pos = m_layer.transform.position;
    start_pos.z += 240.0f;
    pos.key(Frame{0}, start_pos, EasingCurve{Easing::OutCubic});
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    op.key(Frame{static_cast<int>(duration * 0.6f)}, m_layer.transform.opacity, EasingCurve{Easing::OutCubic});
    return *this;
}

LayerBuilder& LayerBuilder::settle(f32 overshoot, Frame duration) {
    auto& sc = scale_anim();
    Vec3 start = m_layer.transform.scale * (1.0f + overshoot);
    sc.key(Frame{0}, start, EasingCurve{Easing::OutBack});
    sc.key(duration, m_layer.transform.scale);

    auto& rot = rotate_anim();
    Vec3 current_euler = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    Vec3 start_rot = current_euler;
    start_rot.z += 2.0f;
    rot.key(Frame{0}, start_rot, EasingCurve{Easing::OutBack});
    rot.key(duration, current_euler);

    auto& pos = position_anim();
    Vec3 start_pos = m_layer.transform.position;
    start_pos.y += 8.0f;
    pos.key(Frame{0}, start_pos, EasingCurve{Easing::OutBack});
    pos.key(duration, m_layer.transform.position);
    return *this;
}

LayerBuilder& LayerBuilder::fade_in(Frame duration, EasingCurve easing) {
    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::focus_in(f32 start_blur, Frame duration, EasingCurve easing) {
    auto& bl = blur_anim();
    bl.key(Frame{0}, start_blur, easing);
    bl.key(duration, 0.0f);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::scale_drop(f32 start_scale, Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{start_scale, start_scale, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::fade_shift_vertical(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position + offset, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::fade_shift_horizontal(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position + offset, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::reveal_from_bottom(f32 distance, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    Vec3 start = m_layer.transform.position;
    start.y += distance;
    pos.key(Frame{0}, start, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::center_split(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::underline_draw(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::highlight_block(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::framing_bracket(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::word_stagger(Frame delay, Frame duration, EasingCurve easing) {
    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(delay, 0.0f, easing);
    op.key(delay + duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::tracking_breathing(f32 scale_factor, Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, m_layer.transform.scale * scale_factor, easing);
    sc.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::elegant_exit_vertical(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position, easing);
    pos.key(duration, m_layer.transform.position + offset);

    auto& op = opacity_anim();
    op.key(Frame{0}, m_layer.transform.opacity, easing);
    op.key(duration, 0.0f);
    return *this;
}

LayerBuilder& LayerBuilder::elegant_exit_horizontal(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position, easing);
    pos.key(duration, m_layer.transform.position + offset);

    auto& op = opacity_anim();
    op.key(Frame{0}, m_layer.transform.opacity, easing);
    op.key(duration, 0.0f);
    return *this;
}

LayerBuilder& LayerBuilder::curtain_close(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, m_layer.transform.scale, easing);
    sc.key(duration, Vec3{m_layer.transform.scale.x, 0.0f, m_layer.transform.scale.z});

    auto& op = opacity_anim();
    op.key(Frame{0}, m_layer.transform.opacity, easing);
    op.key(duration, 0.0f);
    return *this;
}

LayerBuilder& LayerBuilder::screen_dimensions(f32 w, f32 h) {


    m_screen_width = w;
    m_screen_height = h;
    return *this;
}

LayerBuilder& LayerBuilder::font_engine(FontEngine* engine) {
    m_font_engine = engine;
    return *this;
}

FontEngine* LayerBuilder::font_engine() const {
    return m_font_engine;
}

LayerBuilder& LayerBuilder::fullscreen_rect(std::string name, Color color) {
    return rect(std::move(name), {
        .size = { m_screen_width, m_screen_height },
        .color = color,
        // Anchor is set to half the size in RenderNodeFactory::rect, centering
        // the rect on the layer's position. The scene graph shifts unpinned 2D
        // layers by (+width/2, +height/2), so identity position here keeps the
        // rect centered and covering the full frame after that shift.
        .pos = { 0.0f, 0.0f, 0.0f }
    });
}

LayerBuilder& LayerBuilder::fill(Color color) {
    return fullscreen_rect("fill", color);
}

Layer LayerBuilder::build() {
    if (m_until_frame && !m_duration_explicit) {
        m_layer.duration = *m_until_frame - m_layer.from;
    }

    if (m_layer.depth_role != DepthRole::None) {
        m_layer.transform.position.z =
            DepthRoleResolver::z_for(m_layer.depth_role) + m_layer.depth_offset;
    }
    m_layer.font_engine = m_font_engine;
    if (m_layer.anim_transform.is_animated()) {
        const Frame local_frame = m_layer.local_frame(m_current_frame);
        Transform baked = m_layer.anim_transform.evaluate(local_frame);
        if (m_layer.anim_transform.position.is_animated())
            m_layer.transform.position = baked.position;
        if (m_layer.anim_transform.rotation_euler.is_animated())
            m_layer.transform.rotation = baked.rotation;
        if (m_layer.anim_transform.scale.is_animated())
            m_layer.transform.scale = baked.scale;
        if (m_layer.anim_transform.anchor.is_animated())
            m_layer.transform.anchor = baked.anchor;
        if (m_layer.anim_transform.opacity.is_animated())
            m_layer.transform.opacity = baked.opacity;
    }
    // Bake animated blur into the effect stack at the current frame.
    if (m_layer.anim_transform.blur.is_animated()) {
        const Frame local_frame = m_layer.local_frame(m_current_frame);
        f32 blur_radius = m_layer.anim_transform.blur.evaluate(local_frame);
        bool found = false;
        for (auto& effect : m_layer.effects) {
            if (auto* blur = std::get_if<BlurParams>(&effect.params)) {
                blur->radius = blur_radius;
                found = true;
                break;
            }
        }
        if (!found) {
            m_layer.effects.push_back(EffectInstance{
                effects::EffectDescriptor{.id = std::string{effects::ids::BlurGaussian}},
                BlurParams{blur_radius}
            });
        }
    }
    return std::move(m_layer);
}

} // namespace chronon3d
