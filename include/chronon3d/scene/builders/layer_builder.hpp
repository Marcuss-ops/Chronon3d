#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/builders/node_handle.hpp>
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

#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

class FontEngine;
struct ExtensionContext;
struct TextDefinition;
namespace builders::testing { class LayerBuilderInspector; }

class Layer3DDelegate {
public:
    static void add_fake_box3d(
        Layer& layer, std::string name, FakeBox3DParams params,
        FontEngine* font_engine);
    static void add_grid_plane(
        Layer& layer, std::string name, GridPlaneParams params,
        FontEngine* font_engine);
};

class LayerBuilder {
public:
    explicit LayerBuilder(
        std::string name,
        SampleTime current_time,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
        registry::ShapeRegistry* shape_registry = nullptr);
    explicit LayerBuilder(
        std::string name,
        Frame current_frame,
        FrameRate rate,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
        registry::ShapeRegistry* shape_registry = nullptr);

    // Timing.
    [[nodiscard]] FrameRate frame_rate() const noexcept { return m_current_time.frame_rate; }
    LayerBuilder& parent(std::string name);
    LayerBuilder& from(Frame frame);
    LayerBuilder& duration(Frame frames);
    LayerBuilder& until(Frame frame);
    LayerBuilder& offset(Frame frames);
    LayerBuilder& start_at(Frame frame) { return from(frame); }
    LayerBuilder& length(Frame frames) { return duration(frames); }

    // Time remap and visibility.
    LayerBuilder& speed(f32 multiplier);
    LayerBuilder& reverse(bool value = true);
    LayerBuilder& freeze_frame(Frame frame);
    LayerBuilder& time_remap(AnimatedValue<f32> curve);
    AnimatedValue<f32>& time_remap_anim();
    LayerBuilder& visible(bool value);
    LayerBuilder& kind(LayerKind value);
    LayerBuilder& cache_static(bool value = true);

    // Layer transform.
    LayerBuilder& position(Vec3 position);
    LayerBuilder& scale(Vec3 scale);
    LayerBuilder& rotate(Vec3 euler_degrees);
    LayerBuilder& anchor(Vec3 anchor);
    LayerBuilder& opacity(f32 value);
    LayerBuilder& enable_3d(bool value = true);
    AnimatedValue<Vec3>& position_anim();
    AnimatedValue<Vec3>& scale_anim();
    AnimatedValue<Vec3>& rotate_anim();
    AnimatedValue<Vec3>& anchor_anim();
    AnimatedValue<f32>& opacity_anim();
    AnimatedValue<f32>& blur_anim();

    LayerBuilder& depth_role(DepthRole role);
    LayerBuilder& depth_offset(f32 offset);

    // Layout.
    LayerBuilder& pin_to(Anchor anchor, f32 margin = 0.0f);
    LayerBuilder& pin_to(AnchorPlacement placement, f32 margin = 0.0f);
    LayerBuilder& keep_in_safe_area(SafeArea area = {});
    LayerBuilder& fit_text();
    LayerBuilder& center() {
        m_layer.transform.position = {
            m_screen_width * 0.5f,
            m_screen_height * 0.5f,
            0.0f};
        return *this;
    }

    LayerBuilder& font(std::string path) {
        m_default_font_path = std::move(path);
        return *this;
    }
    LayerBuilder& font_size(f32 size) {
        m_default_font_size = size;
        return *this;
    }

    // Explicit node-level access. Shape creation remains LayerBuilder-based;
    // node mutation is always performed through this handle.
    [[nodiscard]] NodeHandle last_node_handle();

    LayerBuilder& accepts_lights(bool value = true);
    LayerBuilder& casts_shadows(bool value = true);
    LayerBuilder& accepts_shadows(bool value = true);
    LayerBuilder& material(Material2_5D value);
    LayerBuilder& card3d_material(Card3DMaterial value);

    LayerBuilder& transition_in(LayerTransitionSpec spec);
    LayerBuilder& transition_out(LayerTransitionSpec spec);

    LayerBuilder& screen_dimensions(f32 width, f32 height);
    LayerBuilder& fullscreen_rect(std::string name, Color color);
    LayerBuilder& fill(Color color);
    [[nodiscard]] bool screen_dimensions_were_set() const noexcept;
    [[nodiscard]] Vec2 screen_dimensions() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;

    LayerBuilder& font_engine(FontEngine* engine);
    [[nodiscard]] FontEngine* font_engine() const;
    LayerBuilder& extension_context(const ExtensionContext& context) noexcept;
    [[nodiscard]] const ExtensionContext* extension_context() const noexcept;

    LayerBuilder& mask_rect(RectMaskParams params);
    LayerBuilder& mask_rounded_rect(RoundedRectMaskParams params);
    LayerBuilder& mask_circle(CircleMaskParams params);
    LayerBuilder& track_matte_alpha(std::string source_layer_name);
    LayerBuilder& track_matte_alpha_inverted(std::string source_layer_name);
    LayerBuilder& track_matte_luma(std::string source_layer_name);
    LayerBuilder& track_matte_luma_inverted(std::string source_layer_name);

    // Effects and layer material.
    LayerBuilder& blur(f32 radius);
    LayerBuilder& tint(Color color, f32 amount = 1.0f);
    LayerBuilder& brightness(f32 value);
    LayerBuilder& contrast(f32 value);
    LayerBuilder& saturation(f32 value);
    LayerBuilder& hue_rotate(f32 degrees);
    LayerBuilder& invert(f32 amount = 1.0f);
    LayerBuilder& vignette(
        f32 radius = 0.5f, f32 softness = 0.5f, f32 amount = 1.0f);
    LayerBuilder& drop_shadow(
        Vec2 offset, Color color = {0, 0, 0, 0.35f}, f32 radius = 12.0f);
    LayerBuilder& glow(GlowParams params);
    LayerBuilder& bloom(
        f32 threshold = 0.80f, f32 radius = 24.0f, f32 intensity = 0.60f);
    LayerBuilder& fake_3d_wave(Fake3DWaveParams params);
    LayerBuilder& blend(BlendMode mode);

    // 2D shapes and media.
    LayerBuilder& rect(std::string name, RectParams params);
    LayerBuilder& rounded_rect(std::string name, RoundedRectParams params);
    LayerBuilder& circle(std::string name, CircleParams params);
    LayerBuilder& line(std::string name, LineParams params);
    LayerBuilder& path(std::string name, PathParams params);
    LayerBuilder& arrow(std::string name, ArrowParams params);
    LayerBuilder& star(std::string name, StarParams params);
    LayerBuilder& badge(std::string name, BadgeParams params);
    LayerBuilder& speech_bubble(std::string name, SpeechBubbleParams params);
    LayerBuilder& callout(std::string name, CalloutParams params);
    LayerBuilder& progress_bar(std::string name, ProgressBarParams params);
    LayerBuilder& timeline_bar(std::string name, TimelineBarParams params);
    LayerBuilder& image(std::string name, ImageParams params);
    LayerBuilder& tiled_image(std::string name, ImageParams params);
    LayerBuilder& grid_background(std::string name, GridBackgroundParams params);

    // Canonical simple-text path. TextSpec remains a compatibility adapter
    // until the dedicated TextSpec migration is completed.
    LayerBuilder& text(std::string name, const TextDefinition& definition);
    [[deprecated("Use text(name, const TextDefinition&)")]]
    LayerBuilder& text(std::string name, const TextSpec& spec);

    [[nodiscard]] TextRunBuilder& animated_text(
        std::string name, TextRunSpec params);
    [[deprecated("Use animated_text(name, TextRunSpec) instead")]]
    TextRunBuilder& text_run(std::string name, TextRunSpec params) {
        return animated_text(std::move(name), std::move(params));
    }

    LayerBuilder& shape(
        std::string_view id, std::string name, registry::ShapeParams params);
    LayerBuilder& fake_box3d(std::string name, FakeBox3DParams params);
    LayerBuilder& grid_plane(std::string name, GridPlaneParams params);

    // Canonical motion preset entry points.
    LayerBuilder& slide_in(
        Vec3 from, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& soft_pop(Frame duration = Frame{30});
    LayerBuilder& float_idle(f32 amplitude_y = 12.0f, Frame cycle = Frame{120});
    LayerBuilder& depth_reveal(f32 depth_z = 260.0f, Frame duration = Frame{45});
    LayerBuilder& card_flip_2_5d(Frame duration = Frame{60});
    LayerBuilder& settle(f32 overshoot = 0.08f, Frame duration = Frame{20});
    LayerBuilder& fade_in(
        Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& focus_in(
        f32 start_blur, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& scale_drop(
        f32 start_scale, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_vertical(
        Vec3 offset, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_horizontal(
        Vec3 offset, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& reveal_from_bottom(
        f32 distance, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& center_split(
        Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& underline_draw(
        Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& highlight_block(
        Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& framing_bracket(
        Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& word_stagger(
        Frame delay, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& tracking_breathing(
        f32 scale_factor, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_vertical(
        Vec3 offset, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_horizontal(
        Vec3 offset, Frame duration,
        EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& curtain_close(
        Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});

    LayerBuilder& video(video::VideoSource source);
    LayerBuilder& video(std::string path);
    LayerBuilder& video_size(Vec2 size);
    LayerBuilder& precomp(std::string composition_name);

    [[nodiscard]] std::pmr::memory_resource* resource() const;
    [[nodiscard]] Layer build();

private:
    friend class builders::testing::LayerBuilderInspector;

    Layer m_layer;
    SampleTime m_current_time{SampleTime::from_frame_int(0, FrameRate{30, 1})};
    std::optional<Frame> m_until_frame{};
    bool m_duration_explicit{false};
    f32 m_screen_width{1920.0f};
    f32 m_screen_height{1080.0f};
    bool m_screen_dimensions_explicit{false};
    std::string m_default_font_path;
    std::optional<f32> m_default_font_size;
    FontEngine* m_font_engine{nullptr};
    registry::ShapeRegistry* m_shape_registry{nullptr};
    std::optional<registry::ShapeRegistry> m_own_shape_registry;
    const ExtensionContext* m_extension_context{nullptr};
    std::vector<std::unique_ptr<PendingTextRun>> m_text_runs;
    std::vector<std::unique_ptr<TextRunBuilder>> m_text_run_builders;
};

} // namespace chronon3d

#include <chronon3d/scene/builders/detail/layer_builder_inline.inl>
