#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <spdlog/spdlog.h>

#include <cmath>
#include <utility>

namespace chronon3d {

// ── Primary constructor (SampleTime) ────────────────────────────────────────

LayerBuilder::LayerBuilder(std::string name, SampleTime current_time, std::pmr::memory_resource* res)
    : m_layer(res), m_current_time(current_time) {
    m_layer.name = std::pmr::string{name, res};
}

// ── Backward-compatible constructor (Frame → SampleTime) ────────────────────

LayerBuilder::LayerBuilder(std::string name, Frame current_frame, std::pmr::memory_resource* res)
    : LayerBuilder(std::move(name), SampleTime::from_frame_int(current_frame, FrameRate{30, 1}), res) {}

LayerBuilder::LayerBuilder(std::string name, std::pmr::memory_resource* res)
    : LayerBuilder(std::move(name), SampleTime::from_frame_int(0, FrameRate{30, 1}), res) {}

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

// ── Motion Timeline adapters ────────────────────────────────────────────────
// These map a single-axis motion::Timeline<f32> onto the corresponding axis of
// the layer's animated transform (Vec3 for rotate/position, f32 for opacity).
// The easing from the timeline is mapped to AnimatedValue's convention
// (prev->easing controls the outgoing segment) via timeline::Point::outgoing_easing.

namespace {

/// Helper: apply a single-axis timeline to an AnimatedValue<Vec3>,
/// keeping the other two axes at their base values.
void apply_axis_timeline(AnimatedValue<Vec3>& dest, Vec3 base,
                         int axis,  // 0=X, 1=Y, 2=Z
                         const motion::Timeline<f32>& timeline) {
    dest.clear();
    for (const auto& pt : timeline.points()) {
        Vec3 v = base;
        v[axis] = pt.value;
        dest.key(pt.frame, v, pt.outgoing_easing);
    }
}

} // anonymous namespace

LayerBuilder& LayerBuilder::rotate_x(const motion::Timeline<f32>& timeline) {
    Vec3 base = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    apply_axis_timeline(rotate_anim(), base, 0, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::rotate_y(const motion::Timeline<f32>& timeline) {
    Vec3 base = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    apply_axis_timeline(rotate_anim(), base, 1, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::rotate_z(const motion::Timeline<f32>& timeline) {
    Vec3 base = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    apply_axis_timeline(rotate_anim(), base, 2, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::position_x(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.position;
    apply_axis_timeline(position_anim(), base, 0, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::position_y(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.position;
    apply_axis_timeline(position_anim(), base, 1, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::position_z(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.position;
    apply_axis_timeline(position_anim(), base, 2, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::opacity_timeline(const motion::Timeline<f32>& timeline) {
    auto& anim = opacity_anim();
    anim.clear();
    for (const auto& pt : timeline.points()) {
        anim.key(pt.frame, pt.value, pt.outgoing_easing);
    }
    return *this;
}

LayerBuilder& LayerBuilder::scale_x(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.scale;
    apply_axis_timeline(scale_anim(), base, 0, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::scale_y(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.scale;
    apply_axis_timeline(scale_anim(), base, 1, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::blur_timeline(const motion::Timeline<f32>& timeline) {
    auto& anim = blur_anim();
    anim.clear();
    for (const auto& pt : timeline.points()) {
        anim.key(pt.frame, pt.value, pt.outgoing_easing);
    }
    return *this;
}

// Motion preset methods (slide_in, soft_pop, float_idle, etc.) are now
// defined in src/scene/builders/commands/motion_preset_methods.cpp.
// This reduces layer_builder.cpp from 815 → ~515 lines.

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

// ═══════════════════════════════════════════════════════════════════════════
// TextRunBuilder — PR 4 (TextAnimator V2)
// ═══════════════════════════════════════════════════════════════════════════

TextRunBuilder& LayerBuilder::text_run(std::string name, TextRunParams params) {
    auto spec_uptr = std::make_unique<TextRunSpec>(TextRunSpec{
        .name = std::move(name),
        .params = std::move(params),
        .consumed = false,
    });
    TextRunSpec* spec_ptr = spec_uptr.get();
    m_text_runs.push_back(std::move(spec_uptr));
    // Push a fresh builder into the pool, keyed to the same spec we
    // just added.  Pool storage means the returned reference stays
    // valid for the lifetime of the LayerBuilder — even across many
    // `.text_run(...)` calls.
    m_text_run_builders.push_back(
        std::make_unique<TextRunBuilder>(this, *spec_ptr));
    return *m_text_run_builders.back();
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
    // Evaluate transform when ANY component is time-dependent (keyframes OR expressions).
    // Expression-only properties (e.g. "sin(time * 2)") have no keyframes, so
    // is_animated() alone would skip evaluation — causing stale values.
    if (m_layer.anim_transform.is_time_dependent()) {
        const SampleTime local_time = m_layer.local_time(m_current_time);
        Transform baked = m_layer.anim_transform.evaluate(local_time);
        if (m_layer.anim_transform.position.is_time_dependent())
            m_layer.transform.position = baked.position;
        if (m_layer.anim_transform.rotation_euler.is_time_dependent())
            m_layer.transform.rotation = baked.rotation;
        if (m_layer.anim_transform.scale.is_time_dependent())
            m_layer.transform.scale = baked.scale;
        if (m_layer.anim_transform.anchor.is_time_dependent())
            m_layer.transform.anchor = baked.anchor;
        if (m_layer.anim_transform.opacity.is_time_dependent())
            m_layer.transform.opacity = baked.opacity;
    }
    // Bake animated blur into the effect stack at the current sub-frame time.
    // Blur can also be expression-only.
    if (m_layer.anim_transform.blur.is_time_dependent()) {
        const SampleTime local_time = m_layer.local_time(m_current_time);
        f32 blur_radius = m_layer.anim_transform.blur.evaluate(local_time);
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

    // ── PR 4 — Materialize pending text-run specs ───────────────────
    //
    // For each TextRunSpec pushed via `LayerBuilder::text_run(name,
    // TextRunParams)`, evaluate the animator stack at the layer's
    // current local time and append a corresponding RenderNode
    // flagged with `is_text_run_shape=true`.  The graph-builder
    // source-pass (PR 3) auto-routes these to a TextRunNode.
    //
    // Each entry uses the layer's FontEngine if one was set, falling
    // back to the process-wide shared FontEngine.  Shaping failures
    // log warn-level and skip the entry (the layer otherwise renders
    // as a normal Layer — explicit empty-place behavior).
    if (!m_text_runs.empty()) {
        const SampleTime local_time = m_layer.local_time(m_current_time);
        std::pmr::memory_resource* res = m_layer.nodes.get_allocator().resource();

        for (auto& spec_uptr : m_text_runs) {
            TextRunSpec& spec = *spec_uptr;
            if (spec.consumed) continue;

            RenderNode& node = m_layer.nodes.emplace_back(res);
            node.name = std::pmr::string{spec.name, res};
            node.is_text_run_shape = true;     // always flagged
            node.font_engine = m_font_engine;
            node.world_transform.position = spec.params.pos;
            node.world_transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
            node.world_transform.scale = Vec3{1.0f, 1.0f, 1.0f};
            node.world_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            node.color = spec.params.color;
            node.fill = Fill::solid_color(spec.params.color);

            auto shape = materialize_text_run_shape(
                spec.params, m_font_engine, local_time);
            if (shape) {
                node.text_run_shape = std::move(shape);
            }
            // On failure, leave `text_run_shape = nullptr` and rely on
            // the graph-builder source-pass to emit its existing
            // one-shot `spdlog::error` for null-shape fallthrough.
            // We do NOT silently drop the placeholder node, so the
            // user sees the failure at compose time, not just in
            // build-time logs.

            spec.consumed = true;
        }
    }

    return std::move(m_layer);
}

} // namespace chronon3d
