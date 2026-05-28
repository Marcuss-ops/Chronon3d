#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>

namespace chronon3d {

Layer::Layer(std::pmr::memory_resource* res)
    : name(res), nodes(res), precomp_composition_name(res) {}

Layer::~Layer() = default;

Layer::Layer(const Layer& other)
    : name(other.name.get_allocator()),
      parent_name(other.parent_name),
      kind(other.kind),
      transform(other.transform),
      anim_transform(other.anim_transform),
      from(other.from),
      duration(other.duration),
      time_offset(other.time_offset),
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
      transition_in(other.transition_in),
      transition_out(other.transition_out),
      nodes(other.nodes),
      precomp_composition_name(other.precomp_composition_name),
      m_static_hash(other.m_static_hash),
      m_static_hash_computed(other.m_static_hash_computed) {
    
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
    anim_transform = other.anim_transform;
    from = other.from;
    duration = other.duration;
    time_offset = other.time_offset;
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
    transition_in = other.transition_in;
    transition_out = other.transition_out;
    nodes = other.nodes;
    precomp_composition_name = other.precomp_composition_name;
    m_static_hash = other.m_static_hash;
    m_static_hash_computed = other.m_static_hash_computed;
    
    if (other.video_source) {
        video_source = std::make_unique<video::VideoSource>(*other.video_source);
    } else {
        video_source.reset();
    }
    
    return *this;
}

Layer::Layer(Layer&& other) noexcept = default;
Layer& Layer::operator=(Layer&& other) noexcept = default;

uint64_t Layer::get_static_hash() const {
    if (!m_static_hash_computed) {
        using namespace chronon3d::graph;
        uint64_t h = 0;
        h = hash_combine(h, hash_string(name));
        h = hash_combine(h, static_cast<u64>(kind));
        h = hash_combine(h, visible ? 1 : 0);
        h = hash_combine(h, is_3d ? 1 : 0);
        h = hash_combine(h, cache_static ? 1 : 0);
        h = hash_combine(h, static_cast<u64>(blend_mode));
        h = hash_combine(h, hash_mask(mask));
        h = hash_combine(h, hash_effect_stack(effects));
        for (const auto& node : nodes) {
            h = hash_combine(h, hash_render_node(node));
        }
        if (kind == LayerKind::Precomp) {
            h = hash_combine(h, hash_string(precomp_composition_name));
        }
        m_static_hash = h;
        m_static_hash_computed = true;
    }
    return m_static_hash;
}

} // namespace chronon3d
