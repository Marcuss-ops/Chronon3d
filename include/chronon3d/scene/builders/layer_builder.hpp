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

    // ── Command declarations (split by domain) ──────────────────────────
#include <chronon3d/scene/builders/layer_builder_core.hpp>
#include <chronon3d/scene/builders/layer_builder_masks.hpp>
#include <chronon3d/scene/builders/layer_builder_effects.hpp>
#include <chronon3d/scene/builders/layer_builder_shapes.hpp>
#include <chronon3d/scene/builders/layer_builder_motion.hpp>
#include <chronon3d/scene/builders/layer_builder_video.hpp>

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
