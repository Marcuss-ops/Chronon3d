#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/backends/video/video_source.hpp>

namespace chronon3d {

Layer::Layer(std::pmr::memory_resource* res)
    : name(res), nodes(res), precomp_composition_name(res) {}

Layer::~Layer() = default;

Layer::Layer(const Layer& other)
    : name(other.name.get_allocator()),
      parent_name(other.parent_name),
      kind(other.kind),
      transform(other.transform),
      from(other.from),
      duration(other.duration),
      visible(other.visible),
      is_3d(other.is_3d),
      hierarchy_resolved(other.hierarchy_resolved),
      mask(other.mask),
      effects(other.effects),
      blend_mode(other.blend_mode),
      depth_role(other.depth_role),
      depth_offset(other.depth_offset),
      layout(other.layout),
      track_matte(other.track_matte),
      material(other.material),
      nodes(other.nodes),
      precomp_composition_name(other.precomp_composition_name) {
    
    name = other.name;
    if (other.video_source) {
        video_source = std::make_unique<video::VideoSource>(*other.video_source);
    }
}

Layer& Layer::operator=(const Layer& other) {
    if (this == &other) return *this;
    
    name = other.name;
    parent_name = other.parent_name;
    kind = other.kind;
    transform = other.transform;
    from = other.from;
    duration = other.duration;
    visible = other.visible;
    is_3d = other.is_3d;
    hierarchy_resolved = other.hierarchy_resolved;
    mask = other.mask;
    effects = other.effects;
    blend_mode = other.blend_mode;
    depth_role = other.depth_role;
    depth_offset = other.depth_offset;
    layout = other.layout;
    track_matte = other.track_matte;
    material = other.material;
    nodes = other.nodes;
    precomp_composition_name = other.precomp_composition_name;
    
    if (other.video_source) {
        video_source = std::make_unique<video::VideoSource>(*other.video_source);
    } else {
        video_source.reset();
    }
    
    return *this;
}

Layer::Layer(Layer&& other) noexcept = default;
Layer& Layer::operator=(Layer&& other) noexcept = default;

} // namespace chronon3d
