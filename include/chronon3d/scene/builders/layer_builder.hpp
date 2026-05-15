#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/backends/video/video_frame_provider.hpp>
#include <chronon3d/math/mat4.hpp>
#include <string>
#include <memory_resource>

namespace chronon3d {

class LayerBuilder {
public:
    explicit LayerBuilder(std::string name,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource());

    LayerBuilder& parent(std::string name);
    LayerBuilder& from(Frame frame);
    LayerBuilder& duration(Frame frames);
    LayerBuilder& visible(bool value);
    LayerBuilder& kind(LayerKind value);

    // Transform
    LayerBuilder& position(Vec3 p);
    LayerBuilder& scale(Vec3 s);
    LayerBuilder& rotate(Vec3 euler_deg);
    LayerBuilder& anchor(Vec3 a);
    LayerBuilder& opacity(f32 value);
    LayerBuilder& enable_3d(bool value = true);

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
    LayerBuilder& glow(f32 radius, f32 intensity = 0.8f, Color color = Color::white());
    LayerBuilder& bloom(f32 threshold = 0.80f, f32 radius = 24.0f, f32 intensity = 0.60f);
    LayerBuilder& blend(BlendMode mode);

    // Layout
    LayerBuilder& pin_to(Anchor anchor, f32 margin = 0.0f);
    LayerBuilder& keep_in_safe_area(SafeArea area = {});
    LayerBuilder& fit_text();

    // 2D Shapes
    LayerBuilder& rect(std::string name, RectParams p);
    LayerBuilder& rounded_rect(std::string name, RoundedRectParams p);
    LayerBuilder& circle(std::string name, CircleParams p);
    LayerBuilder& line(std::string name, LineParams p);
    LayerBuilder& text(std::string name, TextParams p);
    LayerBuilder& image(std::string name, ImageParams p);

    // 3D Shapes (Delegated)
    LayerBuilder& fake_box3d(std::string name, FakeBox3DParams p);
    LayerBuilder& fake_extruded_text(std::string name, FakeExtrudedTextParams p);
    LayerBuilder& grid_plane(std::string name, GridPlaneParams p);

    // Node Polish
    LayerBuilder& with_shadow(DropShadow shadow);
    LayerBuilder& with_glow(Glow glow);

    // Node Transform
    LayerBuilder& at(Vec3 pos);
    LayerBuilder& rotate_node(Vec3 euler_deg);
    LayerBuilder& scale_node(Vec3 s);
    LayerBuilder& anchor_node(Vec3 a);
    LayerBuilder& node_opacity(f32 a);

    // Specialized
    /**
     * Declarative video layer. 
     * The actual frame extraction is handled by the renderer/graph.
     */
    LayerBuilder& video(video::VideoSource source);

    /**
     * Convenience helper for video by path.
     */
    LayerBuilder& video(std::string path);

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_layer.nodes.get_allocator().resource(); }
    [[nodiscard]] Layer build();

private:
    Layer m_layer;
};

} // namespace chronon3d
