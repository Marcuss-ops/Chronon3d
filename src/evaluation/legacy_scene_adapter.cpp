#include <chronon3d/evaluation/legacy_scene_adapter.hpp>
#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/render_node.hpp>
#include <chronon3d/scene/shape.hpp>
#include <variant>

namespace chronon3d {

namespace {

// Build a RenderNode from a VisualDesc, applying the layer's world transform
// as the base position offset.
RenderNode make_render_node(const VisualDesc& vd,
                            const Transform& layer_transform,
                            std::pmr::memory_resource* res) {
    RenderNode node(res);

    std::visit([&node, &layer_transform, res](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, RectParams>) {
            node.name            = std::pmr::string{"rect", res};
            node.shape.type      = ShapeType::Rect;
            node.shape.rect.size = v.size;
            node.color           = v.color;
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};

        } else if constexpr (std::is_same_v<T, RoundedRectParams>) {
            node.name                         = std::pmr::string{"rrect", res};
            node.shape.type                   = ShapeType::RoundedRect;
            node.shape.rounded_rect.size      = v.size;
            node.shape.rounded_rect.radius    = v.radius;
            node.color                        = v.color;
            node.world_transform.position     = layer_transform.position + v.pos;
            node.world_transform.anchor       = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};

        } else if constexpr (std::is_same_v<T, CircleParams>) {
            node.name                     = std::pmr::string{"circle", res};
            node.shape.type               = ShapeType::Circle;
            node.shape.circle.radius      = v.radius;
            node.color                    = v.color;
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.radius, v.radius, 0.0f};

        } else if constexpr (std::is_same_v<T, LineParams>) {
            node.name                     = std::pmr::string{"line", res};
            node.shape.type               = ShapeType::Line;
            node.shape.line.to            = v.to - v.from;
            node.shape.line.thickness     = v.thickness;
            node.color                    = v.color;
            node.world_transform.position = layer_transform.position + v.from;
            node.world_transform.anchor   = {0.0f, 0.0f, 0.0f};

        } else if constexpr (std::is_same_v<T, TextParams>) {
            node.name                     = std::pmr::string{"text", res};
            node.shape.type               = ShapeType::Text;
            node.shape.text.text          = v.content;
            node.shape.text.style         = v.style;
            node.color                    = v.style.color;
            node.world_transform.position = layer_transform.position + v.pos;

        } else if constexpr (std::is_same_v<T, ImageParams>) {
            node.name                     = std::pmr::string{"image", res};
            node.shape.type               = ShapeType::Image;
            node.shape.image.path         = v.path;
            node.shape.image.size         = v.size;
            node.shape.image.opacity      = v.opacity;
            node.color                    = Color{1.0f, 1.0f, 1.0f, v.opacity};
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};
        } else if constexpr (std::is_same_v<T, FakeExtrudedTextParams>) {
            node.name                     = std::pmr::string{"fake_extruded_text", res};
            node.shape.type               = ShapeType::FakeExtrudedText;
            auto& s                       = node.shape.fake_extruded_text;
            s.text                        = v.text;
            s.font_path                   = v.font_path;
            s.font_size                   = v.font_size;
            s.align                       = v.align;
            s.world_pos                   = layer_transform.position + v.pos;
            s.depth                       = v.depth;
            s.extrude_dir                 = v.extrude_dir;
            s.extrude_z_step              = v.extrude_z_step;
            s.front_color                 = v.front_color;
            s.side_color                  = v.side_color;
            s.side_fade                   = v.side_fade;
            s.highlight_opacity           = v.highlight_opacity;
            s.bevel_size                  = v.bevel_size;
            node.color                    = v.front_color;
            node.world_transform.position  = layer_transform.position + v.pos;
        }

        // Carry layer scale and rotation into the node transform
        node.world_transform.scale    = layer_transform.scale;
        node.world_transform.rotation = layer_transform.rotation;
    }, vd);

    return node;
}

} // namespace

Scene LegacySceneAdapter::convert(const EvaluatedScene& evaluated,
                                   std::pmr::memory_resource* res) const {
    Scene scene(res);

    // Camera
    if (evaluated.camera) {
        scene.set_camera_2_5d(*evaluated.camera);
    }

    for (const auto& el : evaluated.layers) {
        if (!el.visible) continue;

        Layer layer(res);
        layer.name         = std::pmr::string{el.name, res};
        layer.transform    = el.world_transform;
        layer.transform.opacity = el.opacity;
        layer.is_3d        = el.is_3d;
        layer.depth_role   = el.depth_role;
        layer.blend_mode   = el.blend_mode;
        layer.effects      = el.resolved_effects;
        layer.visible      = true;

        for (const auto& vd : el.visuals) {
            layer.nodes.push_back(make_render_node(vd, el.world_transform, res));
        }

        scene.add_layer(std::move(layer));
    }

    return scene;
}

} // namespace chronon3d
