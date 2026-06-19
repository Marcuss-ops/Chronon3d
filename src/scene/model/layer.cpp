#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/shape/material_2_5d.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d {

Layer::Layer(std::pmr::memory_resource* res)
    : name(res),
      parent_name(res),
      m_effects(std::make_unique<EffectStack>()),
      m_material(std::make_unique<Material2_5D>()),
      m_card3d_material(std::make_unique<Card3DMaterial>()),
      nodes(res),
      precomp_composition_name(res) {}

Layer::~Layer() = default;

Layer::Layer(const Layer& other)
    : name(other.name.get_allocator()),
      parent_name(other.parent_name.get_allocator()),
      kind(other.kind),
      transform(other.transform),
      anim_transform(other.anim_transform),
      from(other.from),
      duration(other.duration),
      time_offset(other.time_offset),
      time_remap(other.time_remap),
      visible(other.visible),
      uses_2_5d_projection(other.uses_2_5d_projection),
      hierarchy_resolved(other.hierarchy_resolved),
      cache_static(other.cache_static),
      mask(other.mask),
      m_effects(std::make_unique<EffectStack>(*other.m_effects)),
      blend_mode(other.blend_mode),
      composite_operator(other.composite_operator),
      depth_role(other.depth_role),
      depth_offset(other.depth_offset),
      layout(other.layout),
      track_matte(other.track_matte),
      m_material(std::make_unique<Material2_5D>(*other.m_material)),
      m_card3d_material(std::make_unique<Card3DMaterial>(*other.m_card3d_material)),
      transition_in(other.transition_in),
      transition_out(other.transition_out),
      nodes(other.nodes),
      precomp_composition_name(other.precomp_composition_name),
      font_engine(other.font_engine),
      m_static_hash(other.m_static_hash),
      m_static_hash_computed(other.m_static_hash_computed) {

    name = other.name;
    parent_name = other.parent_name;
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
    time_remap = other.time_remap;
    visible = other.visible;
    uses_2_5d_projection = other.uses_2_5d_projection;
    hierarchy_resolved = other.hierarchy_resolved;
    cache_static = other.cache_static;
    mask = other.mask;
    *m_effects = *other.m_effects;
    blend_mode = other.blend_mode;
    composite_operator = other.composite_operator;
    depth_role = other.depth_role;
    depth_offset = other.depth_offset;
    layout = other.layout;
    track_matte = other.track_matte;
    *m_material = *other.m_material;
    *m_card3d_material = *other.m_card3d_material;
    transition_in = other.transition_in;
    transition_out = other.transition_out;
    nodes = other.nodes;
    precomp_composition_name = other.precomp_composition_name;
    font_engine = other.font_engine;
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
    // Always compute fresh — memoization was removed because it could become
    // stale after field mutations (Layer fields are public).  The cost is
    // acceptable: get_static_hash() is called at most once per layer per
    // frame during scene fingerprinting, not per pixel.  If profiling shows
    // this to be a bottleneck, re-add memoization with a LayerVersion counter
    // that is incremented by every mutating accessor.
    using namespace chronon3d::graph;
    uint64_t h = 0;
    h = hash_combine(h, hash_string(name));
    h = hash_combine(h, static_cast<u64>(kind));
    h = hash_combine(h, visible ? 1 : 0);
    h = hash_combine(h, uses_2_5d_projection ? 1 : 0);
    h = hash_combine(h, cache_static ? 1 : 0);
    h = hash_combine(h, static_cast<u64>(blend_mode));
    h = hash_combine(h, hash_mask(mask));
    h = hash_combine(h, hash_effect_stack(*m_effects));
    for (const auto& node : nodes) {
        h = hash_combine(h, hash_render_node(node));
    }
    if (kind == LayerKind::Precomp) {
        h = hash_combine(h, hash_string(precomp_composition_name));
    }
    return h;
}

} // namespace chronon3d
