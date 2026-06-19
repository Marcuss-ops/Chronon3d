#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>  // PR 4 — full type required for `std::unique_ptr<TextRunBuilder>` / `std::unique_ptr<TextRunSpec>` members (was forward-declared; caused `sizeof incomplete` + cascaded `private constructor` errors at `std::make_unique` sites and at every TU that destroys a LayerBuilder).
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/vector/path_factories.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/scene/model/shape/material_2_5d.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/effects/animated_transform.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <string>
#include <memory_resource>
#include <optional>

namespace chronon3d {

class FontEngine;  // forward declaration

// `TextRunBuilder` and `TextRunSpec` are now pulled in fully via
// `#include <chronon3d/scene/builders/text_run_builder.hpp>` above.
// Forward declarations of these types here caused the pre-existing
// build break: any TU that includes this header but not the full
// `text_run_builder.hpp` triggered `invalid application of sizeof to
// incomplete type` in `std::unique_ptr<TextRunBuilder>::default_delete`,
// which in turn cascaded into a misleading `private constructor` error
// at the `std::make_unique<TextRunBuilder>(this, *spec_ptr)` call site
// (the friend relationship was correct, but incomplete type was
// masking the access check).

namespace layer_builder_internal {

[[nodiscard]] RenderNode* last_node(Layer& layer);

void set_last_shadow(Layer& layer, DropShadow shadow);
void set_last_glow(Layer& layer, Glow glow);
void set_last_position(Layer& layer, Vec3 pos);
void set_last_rotation(Layer& layer, Vec3 euler_deg);
void set_last_scale(Layer& layer, Vec3 s);
void set_last_anchor(Layer& layer, Vec3 a);
void set_last_opacity(Layer& layer, f32 opacity);

} // namespace layer_builder_internal

class Layer3DDelegate {
public:
    static void add_fake_box3d(Layer& layer, std::string name, FakeBox3DParams p, FontEngine* font_engine);
    static void add_grid_plane(Layer& layer, std::string name, GridPlaneParams p, FontEngine* font_engine);
};

class LayerBuilder {
public:
    /// Primary constructor: accepts sub-frame SampleTime for continuous animation evaluation.
    explicit LayerBuilder(std::string name,
                          SampleTime current_time,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource());
    /// Backward-compatible constructor: accepts integer Frame, delegates to SampleTime.
    explicit LayerBuilder(std::string name,
                          Frame current_frame = 0,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource());
    explicit LayerBuilder(std::string name,
                          std::pmr::memory_resource* res);

    // ── Timing ──
    LayerBuilder& parent(std::string name);
    LayerBuilder& from(Frame frame);
    LayerBuilder& duration(Frame frames);
    LayerBuilder& until(Frame frame);
    LayerBuilder& offset(Frame frames);

    // ── Time Remap ──
    LayerBuilder& speed(f32 multiplier);
    LayerBuilder& reverse(bool value = true);
    LayerBuilder& freeze_frame(Frame source_frame);
    LayerBuilder& time_remap(AnimatedValue<f32> curve);
    AnimatedValue<f32>& time_remap_anim();
    LayerBuilder& visible(bool value);
    LayerBuilder& kind(LayerKind value);
    LayerBuilder& cache_static(bool value = true);

    // ── Transform ──
    LayerBuilder& position(Vec3 p);
    LayerBuilder& scale(Vec3 s);
    LayerBuilder& rotate(Vec3 euler_deg);
    LayerBuilder& anchor(Vec3 a);
    LayerBuilder& opacity(f32 value);
    LayerBuilder& enable_3d(bool value = true);

    AnimatedValue<Vec3>& position_anim();
    AnimatedValue<Vec3>& scale_anim();
    AnimatedValue<Vec3>& rotate_anim();
    AnimatedValue<Vec3>& anchor_anim();
    AnimatedValue<f32>&  opacity_anim();
    AnimatedValue<f32>&  blur_anim();

    // ── Motion Timeline adapters: single-axis animation with intuitive easing ──
    LayerBuilder& rotate_x(const motion::Timeline<f32>& timeline);
    LayerBuilder& rotate_y(const motion::Timeline<f32>& timeline);
    LayerBuilder& rotate_z(const motion::Timeline<f32>& timeline);
    LayerBuilder& position_x(const motion::Timeline<f32>& timeline);
    LayerBuilder& position_y(const motion::Timeline<f32>& timeline);
    LayerBuilder& position_z(const motion::Timeline<f32>& timeline);
    LayerBuilder& opacity_timeline(const motion::Timeline<f32>& timeline);
    LayerBuilder& scale_x(const motion::Timeline<f32>& timeline);
    LayerBuilder& scale_y(const motion::Timeline<f32>& timeline);
    LayerBuilder& blur_timeline(const motion::Timeline<f32>& timeline);

    // ── Depth ──
    LayerBuilder& depth_role(DepthRole role);
    LayerBuilder& depth_offset(f32 offset);

    // ── Layout ──
    LayerBuilder& pin_to(Anchor anchor, f32 margin = 0.0f);
    LayerBuilder& pin_to(AnchorPlacement placement, f32 margin = 0.0f);
    LayerBuilder& keep_in_safe_area(SafeArea area = {});
    LayerBuilder& fit_text();

    // ── Node Transform ──
    LayerBuilder& at(Vec3 pos);
    LayerBuilder& rotate_node(Vec3 euler_deg);
    LayerBuilder& scale_node(Vec3 s);
    LayerBuilder& anchor_node(Vec3 a);
    LayerBuilder& node_opacity(f32 a);

    // ── Node Polish ──
    LayerBuilder& with_shadow(DropShadow shadow);
    LayerBuilder& with_glow(Glow glow);
    LayerBuilder& accepts_lights(bool value = true);
    LayerBuilder& casts_shadows(bool value = true);
    LayerBuilder& accepts_shadows(bool value = true);
    LayerBuilder& material(Material2_5D value);
    LayerBuilder& card3d_material(Card3DMaterial value);

    // ── Transitions ──
    LayerBuilder& transition_in(LayerTransitionSpec spec);
    LayerBuilder& transition_out(LayerTransitionSpec spec);

    // ── Specialized ──
    LayerBuilder& screen_dimensions(f32 w, f32 h);
    LayerBuilder& fullscreen_rect(std::string name, Color color);
    LayerBuilder& fill(Color color);

    // ── FontEngine ──
    LayerBuilder& font_engine(FontEngine* engine);
    [[nodiscard]] FontEngine* font_engine() const;

    // ── Masks ──
    LayerBuilder& mask_rect(RectMaskParams p);
    LayerBuilder& mask_rounded_rect(RoundedRectMaskParams p);
    LayerBuilder& mask_circle(CircleMaskParams p);

    // ── Track matte ──
    LayerBuilder& track_matte_alpha(std::string source_layer_name);
    LayerBuilder& track_matte_alpha_inverted(std::string source_layer_name);
    LayerBuilder& track_matte_luma(std::string source_layer_name);
    LayerBuilder& track_matte_luma_inverted(std::string source_layer_name);

    // ── Effects ──
    LayerBuilder& blur(f32 radius);
    LayerBuilder& tint(Color color, f32 amount = 1.0f);
    LayerBuilder& brightness(f32 v);
    LayerBuilder& contrast(f32 v);
    LayerBuilder& saturation(f32 v);
    LayerBuilder& hue_rotate(f32 deg);
    LayerBuilder& invert(f32 amount = 1.0f);
    LayerBuilder& vignette(f32 radius = 0.5f, f32 softness = 0.5f, f32 amount = 1.0f);
    LayerBuilder& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f);
    LayerBuilder& glow(GlowParams params);
    LayerBuilder& bloom(f32 threshold = 0.80f, f32 radius = 24.0f, f32 intensity = 0.60f);
    LayerBuilder& fake_3d_wave(Fake3DWaveParams params);
    LayerBuilder& blend(BlendMode mode);

    // ── 2D Shapes ──
    LayerBuilder& rect(std::string name, RectParams p);
    LayerBuilder& rounded_rect(std::string name, RoundedRectParams p);
    LayerBuilder& circle(std::string name, CircleParams p);
    LayerBuilder& line(std::string name, LineParams p);
    LayerBuilder& path(std::string name, PathParams p);
    LayerBuilder& arrow(std::string name, ArrowParams p);
    LayerBuilder& star(std::string name, StarParams p);
    LayerBuilder& badge(std::string name, BadgeParams p);
    LayerBuilder& speech_bubble(std::string name, SpeechBubbleParams p);
    LayerBuilder& callout(std::string name, CalloutParams p);
    LayerBuilder& progress_bar(std::string name, ProgressBarParams p);
    LayerBuilder& timeline_bar(std::string name, TimelineBarParams p);
    LayerBuilder& image(std::string name, ImageParams p);
    LayerBuilder& tiled_image(std::string name, ImageParams p);
    LayerBuilder& grid_background(std::string name, GridBackgroundParams p);
    LayerBuilder& text(std::string name, TextSpec p);

    // ── TextRunBuilder (PR 4 — TextAnimator V2) ──────────────────────────
    /// Push a new text-run entry into the layer's pending specs and
    /// return a `TextRunBuilder&` for fluent chaining.  The text-run
    /// entry is committed to a `RenderNode` (with `is_text_run_shape
    /// = true` and a populated `text_run_shape`) inside
    /// `LayerBuilder::build()`.  Multiple `text_run(...)` calls per
    /// layer are allowed — each spawns a separate RenderNode plus
    /// downstream TextRunNode at compose time.
    ///
    /// Note: this method does NOT participate in `LayerBuilder`'s
    /// `return *this` chain.  The returned `TextRunBuilder&` is the
    /// next layer in the chain; calling `.commit()` explicitly hands
    /// control back to the layer-level builder.
    [[nodiscard]] TextRunBuilder& text_run(std::string name, TextRunParams params);

    LayerBuilder& shape(std::string_view id, std::string name, registry::ShapeParams params);

    // ── 3D Shapes (Delegated) ──
    LayerBuilder& fake_box3d(std::string name, FakeBox3DParams p);
    LayerBuilder& grid_plane(std::string name, GridPlaneParams p);

    // ── Motion Presets ──
    LayerBuilder& slide_in(Vec3 from, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& soft_pop(Frame duration = Frame{30});
    LayerBuilder& float_idle(f32 amplitude_y = 12.0f, Frame cycle = Frame{120});
    LayerBuilder& depth_reveal(f32 depth_z = 260.0f, Frame duration = Frame{45});
    LayerBuilder& card_flip_2_5d(Frame duration = Frame{60});
    LayerBuilder& settle(f32 overshoot = 0.08f, Frame duration = Frame{20});
    LayerBuilder& fade_in(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& focus_in(f32 start_blur, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& scale_drop(f32 start_scale, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_vertical(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_horizontal(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& reveal_from_bottom(f32 distance, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& center_split(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& underline_draw(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& highlight_block(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& framing_bracket(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& word_stagger(Frame delay, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& tracking_breathing(f32 scale_factor, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_vertical(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_horizontal(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& curtain_close(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});

    // ── Video ──
    LayerBuilder& video(video::VideoSource source);
    LayerBuilder& video(std::string path);
    LayerBuilder& video_size(Vec2 size);

    // ── Precomp ──
    LayerBuilder& precomp(std::string comp_name);

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_layer.nodes.get_allocator().resource(); }
    [[nodiscard]] Layer build();

private:
    Layer m_layer;
    SampleTime m_current_time{SampleTime::from_frame_int(0, FrameRate{30, 1})};
    std::optional<Frame> m_until_frame{};
    bool m_duration_explicit{false};
    f32 m_screen_width{1920.0f};
    f32 m_screen_height{1080.0f};
    FontEngine* m_font_engine{nullptr};

    // ── PR 4 — pending text-run specs + builder pool ──────────────────
    //
    // `m_text_runs` holds the spec data the builder writes into; stored
    // via `unique_ptr` so push_back doesn't invalidate references.
    //
    // `m_text_run_builders` holds each TextRunBuilder keyed to the
    // matching spec.  Builders own nothing on the stack — the caller
    // keeps a `TextRunBuilder&` reference borrowed from this pool,
    // so destruction of this LayerBuilder (or any explicit reset())
    // is what ends the builder's lifetime.  This is the only way the
    // compiler accepts `layer.text_run(...).position(...).opacity(...)`
    // when chained on multiple statements.
    std::vector<std::unique_ptr<TextRunSpec>> m_text_runs;
    std::vector<std::unique_ptr<TextRunBuilder>> m_text_run_builders;
};

} // namespace chronon3d
