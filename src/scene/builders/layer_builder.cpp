// FASE 22: this file retains ONLY the constructors, simple setters, anim
// accessors, and the font_engine getter/setter.  Motion timeline adapters
// live in layer_builder_motion.cpp; text_run() + build() live in
// layer_builder_compile.cpp.  Post-split minimal include surface:
//   layer_builder.hpp (always) — full class declaration including
//     SampleTime, AnimatedValue, motion::Timeline (transitive via
//     animated_transform.hpp), FontEngine forward-decl, all param structs.
//   font_engine.hpp — needed by font_engine_setter / getter body for
//     the full FontEngine type (pointer is fine but the include is here
//     pre-FASE 22 for legacy reasons; keep for grep-compatibility).
//
// (text_run_builder.hpp, transform.hpp, sample_time.hpp, effect_ids.hpp,
//  text_run.hpp, text_animator_property.hpp, glyph_selector.hpp,
//  pending_text_run_impl.hpp, spdlog/spdlog.h, cmath, utility all moved
//  to the helpers that actually use them.)
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <utility>

namespace chronon3d {

// ── Primary constructor (SampleTime) ────────────────────────────────────────

LayerBuilder::LayerBuilder(std::string name, SampleTime current_time, std::pmr::memory_resource* res,
                           registry::ShapeRegistry* shape_registry)
    : m_layer(res), m_current_time(current_time) {
    m_layer.name = std::pmr::string{name, res};
    if (shape_registry) {
        m_shape_registry = shape_registry;
    } else {
        m_own_shape_registry.emplace(registry::make_default_shape_registry());
        m_shape_registry = &*m_own_shape_registry;
    }
}

// ── Backward-compatible constructor (Frame → SampleTime) ────────────────────

LayerBuilder::LayerBuilder(std::string name, Frame current_frame, std::pmr::memory_resource* res,
                           registry::ShapeRegistry* shape_registry)
    : LayerBuilder(std::move(name), SampleTime::from_frame_int(current_frame, FrameRate{30, 1}), res, shape_registry) {}

LayerBuilder::LayerBuilder(std::string name, std::pmr::memory_resource* res,
                           registry::ShapeRegistry* shape_registry)
    : LayerBuilder(std::move(name), SampleTime::from_frame_int(0, FrameRate{30, 1}), res, shape_registry) {}

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

LayerBuilder& LayerBuilder::freeze_frame(Frame frame) {
    m_layer.time_remap.freeze_frame = frame;
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

// Shape methods → shape_commands.cpp
// Node transform + internal helpers → node_transform_commands.cpp
// Mask, track_matte, transitions, video → layer_property_commands.cpp

AnimatedValue<Vec3>& LayerBuilder::position_anim() { return m_layer.anim_transform.position; }
AnimatedValue<Vec3>& LayerBuilder::scale_anim()    { return m_layer.anim_transform.scale; }
AnimatedValue<Vec3>& LayerBuilder::rotate_anim()   { return m_layer.anim_transform.rotation_euler; }
AnimatedValue<Vec3>& LayerBuilder::anchor_anim()   { return m_layer.anim_transform.anchor; }
AnimatedValue<f32>&  LayerBuilder::opacity_anim()  { return m_layer.anim_transform.opacity; }
AnimatedValue<f32>&  LayerBuilder::blur_anim()    { return m_layer.anim_transform.blur; }

// Motion Timeline adapters (apply_axis_timeline helper + 9 motion methods:
//   rotate_x / rotate_y / rotate_z, position_x / _y / _z, scale_x / _y,
//   opacity_timeline, blur_timeline)
// → layer_builder_motion.cpp

// Motion preset methods (slide_in, soft_pop, float_idle, etc.) are now
// defined in src/scene/builders/commands/motion_preset_methods.cpp.
// This reduces layer_builder.cpp from 815 → ~515 lines.

// NOTE — `screen_dimensions(w, h)` body intentionally defined in
// the header (in-class) so the PR 4 flag-flip
// (`m_screen_dimensions_explicit = true`) lives next to the readback
// accessors it gates. A non-inline out-of-class definition here
// causes a redefinition error (since the in-class body is implicitly
// inline) and silently drops the flag, breaking the
// `Layer(LayerBuilder&)` 'was-set' detection.

LayerBuilder& LayerBuilder::font_engine(FontEngine* engine) {
    m_font_engine = engine;
    return *this;
}

FontEngine* LayerBuilder::font_engine() const {
    return m_font_engine;
}

// text_run(name, params) + build() → layer_builder_compile.cpp

} // namespace chronon3d
