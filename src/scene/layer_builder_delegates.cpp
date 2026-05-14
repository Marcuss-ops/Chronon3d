#include <chronon3d/scene/layer_builder_delegates.hpp>
#include <chronon3d/render_graph/render_node.hpp>

namespace chronon3d {

void Layer3DDelegate::add_fake_box3d(Layer& layer, std::string name, FakeBox3DParams p) {
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
    node.world_transform.anchor      = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.color = p.color;
    layer.nodes.push_back(std::move(node));
}

void Layer3DDelegate::add_fake_extruded_text(Layer& layer, std::string name, FakeExtrudedTextParams p) {
    auto* res = layer.nodes.get_allocator().resource();
    RenderNode node(res);
    node.name = std::pmr::string{name, res};
    node.shape.type = ShapeType::FakeExtrudedText;
    auto& s = node.shape.fake_extruded_text;
    s.text              = std::move(p.text);
    s.font_path         = std::move(p.font_path);
    s.font_size         = p.font_size;
    s.align             = p.align;
    s.world_pos         = p.pos;
    s.depth             = p.depth;
    s.extrude_dir       = p.extrude_dir;
    s.extrude_z_step    = p.extrude_z_step;
    s.front_color       = p.front_color;
    s.side_color        = p.side_color;
    s.highlight_opacity = p.highlight_opacity;
    s.side_fade         = p.side_fade;
    s.bevel_size        = p.bevel_size;
    node.world_transform.position = {0, 0, 0};
    node.color = p.front_color;
    layer.nodes.push_back(std::move(node));
}

void Layer3DDelegate::add_grid_plane(Layer& layer, std::string name, GridPlaneParams p) {
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
    layer.nodes.push_back(std::move(node));
}

} // namespace chronon3d
