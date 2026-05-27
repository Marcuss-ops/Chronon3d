#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/material_2_5d.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/math/math_base.hpp>
#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/animation/animated_transform.hpp>
#include <string>
#include <memory_resource>
#include <optional>

namespace chronon3d {

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
    LayerBuilder& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f);
    LayerBuilder& glow(f32 radius, f32 intensity = 0.8f, Color color = Color::white(), f32 threshold = 0.0f, f32 spread = 1.0f, f32 softness = 1.0f);
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

    /**
     * Convenience helper for video by path.
     */
    LayerBuilder& video(std::string path);

    /**
     * Set the natural render size for the video layer.
     * {0,0} (default) means use the render context dimensions.
     */
    LayerBuilder& video_size(Vec2 size);

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_layer.nodes.get_allocator().resource(); }
    [[nodiscard]] Layer build();

private:
    Layer m_layer;
    Frame m_current_frame{0};
    std::optional<Frame> m_until_frame{};
    bool m_duration_explicit{false};
    f32 m_screen_width{1920.0f};
    f32 m_screen_height{1080.0f};
};

} // namespace chronon3d
