#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
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
#include <string>
#include <memory_resource>
#include <optional>

namespace chronon3d {

class FontEngine;  // forward declaration

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
    explicit LayerBuilder(std::string name,
                          Frame current_frame = 0,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource());
    explicit LayerBuilder(std::string name,
                          std::pmr::memory_resource* res);

    LayerBuilder& parent(std::string name);
    LayerBuilder& from(Frame frame);
    LayerBuilder& duration(Frame frames);
    LayerBuilder& until(Frame frame);
    LayerBuilder& offset(Frame frames);

    // ── Time Remap (AE-4): per-layer time control ──
    LayerBuilder& speed(f32 multiplier);            // 0.5 = slow-mo, 2.0 = fast, -1.0 = reverse
    LayerBuilder& reverse(bool value = true);        // convenience: sets speed = -1.0
    LayerBuilder& freeze_frame(Frame source_frame);  // hold a specific source frame
    LayerBuilder& time_remap(AnimatedValue<f32> curve); // animated comp-frame → source-frame
    AnimatedValue<f32>& time_remap_anim();           // direct access to time_remap curve
    LayerBuilder& visible(bool value);
    LayerBuilder& kind(LayerKind value);
    LayerBuilder& cache_static(bool value = true);

    // Transform
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

    // ── Motion Presets (keyframe-based animation helpers) ─────────────────
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



    // Depth
    LayerBuilder& depth_role(DepthRole role);
    LayerBuilder& depth_offset(f32 offset);

    // Masks
    LayerBuilder& mask_rect(RectMaskParams p);
    LayerBuilder& mask_rounded_rect(RoundedRectMaskParams p);
    LayerBuilder& mask_circle(CircleMaskParams p);

    // Effects
    LayerBuilder& blur(f32 radius);
    LayerBuilder& tint(Color color, f32 amount = 1.0f);
    LayerBuilder& brightness(f32 v);
    LayerBuilder& contrast(f32 v);
    LayerBuilder& saturation(f32 v);    // 1.0 = unchanged, 0 = greyscale
    LayerBuilder& hue_rotate(f32 deg);  // degrees, 0 = unchanged
    LayerBuilder& invert(f32 amount = 1.0f); // 1.0 = full invert, 0 = no-op
    LayerBuilder& vignette(f32 radius = 0.5f, f32 softness = 0.5f, f32 amount = 1.0f);
    LayerBuilder& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f);
    LayerBuilder& glow(GlowParams params);
    LayerBuilder& bloom(f32 threshold = 0.80f, f32 radius = 24.0f, f32 intensity = 0.60f);
    LayerBuilder& fake_3d_wave(Fake3DWaveParams params);
    LayerBuilder& blend(BlendMode mode);

    // Layout
    LayerBuilder& pin_to(Anchor anchor, f32 margin = 0.0f);
    LayerBuilder& pin_to(AnchorPlacement placement, f32 margin = 0.0f);
    LayerBuilder& keep_in_safe_area(SafeArea area = {});
    LayerBuilder& fit_text();

    // 2D Shapes
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
    LayerBuilder& text(std::string name, TextParams p);
    LayerBuilder& shape(std::string_view id, std::string name, registry::ShapeParams params);

    // 3D Shapes (Delegated)
    LayerBuilder& fake_box3d(std::string name, FakeBox3DParams p);
    LayerBuilder& grid_plane(std::string name, GridPlaneParams p);

    // Node Polish
    LayerBuilder& with_shadow(DropShadow shadow);
    LayerBuilder& with_glow(Glow glow);
    LayerBuilder& accepts_lights(bool value = true);
    LayerBuilder& casts_shadows(bool value = true);
    LayerBuilder& accepts_shadows(bool value = true);
    LayerBuilder& material(Material2_5D value);
    LayerBuilder& card3d_material(Card3DMaterial value);

    // Node Transform
    LayerBuilder& at(Vec3 pos);
    LayerBuilder& rotate_node(Vec3 euler_deg);
    LayerBuilder& scale_node(Vec3 s);
    LayerBuilder& anchor_node(Vec3 a);
    LayerBuilder& node_opacity(f32 a);

    // Track matte
    LayerBuilder& track_matte_alpha(std::string source_layer_name);
    LayerBuilder& track_matte_alpha_inverted(std::string source_layer_name);
    LayerBuilder& track_matte_luma(std::string source_layer_name);
    LayerBuilder& track_matte_luma_inverted(std::string source_layer_name);

    // Transitions
    LayerBuilder& transition_in(LayerTransitionSpec spec);
    LayerBuilder& transition_out(LayerTransitionSpec spec);

    // Specialized
    LayerBuilder& screen_dimensions(f32 w, f32 h);
    LayerBuilder& fullscreen_rect(std::string name, Color color);
    LayerBuilder& fill(Color color);
    /**
     * Declarative video layer.
     * The actual frame extraction is handled by the renderer/graph.
     */
    LayerBuilder& video(video::VideoSource source);

    // ── Precomp (AE-6) ───────────────────────────────────────────────
    /**
     * Mark this layer as a precomp referencing a named composition.
     * The composition must be registered in the CompositionRegistry
     * or defined as a SubComposition in the SceneDescription.
     */
    LayerBuilder& precomp(std::string comp_name);

    /**
     * Convenience helper for video by path.
     */
    LayerBuilder& video(std::string path);

    /**
     * Set the natural render size for the video layer.
     * {0,0} (default) means use the render context dimensions.
     */
    LayerBuilder& video_size(Vec2 size);

    // ── FontEngine (for precise text shaping / glyph metrics) ───────────
    LayerBuilder& font_engine(FontEngine* engine);
    [[nodiscard]] FontEngine* font_engine() const;

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_layer.nodes.get_allocator().resource(); }
    [[nodiscard]] Layer build();

private:
    Layer m_layer;
    Frame m_current_frame{0};
    std::optional<Frame> m_until_frame{};
    bool m_duration_explicit{false};
    f32 m_screen_width{1920.0f};
    f32 m_screen_height{1080.0f};
    FontEngine* m_font_engine{nullptr};
};

} // namespace chronon3d
