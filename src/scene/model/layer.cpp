#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/shape/material_2_5d.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/text/text_run_shape.hpp>

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

bool Layer::is_native_3d() const noexcept {
    for (const auto& node : nodes) {
        if (node.shape.type() == ShapeType::FakeBox3D ||
            node.shape.type() == ShapeType::GridPlane ||
            node.shape.type() == ShapeType::Mesh) {
            return true;
        }
        if (node.shape.type() == ShapeType::TextRun) {
            const auto run = node.shape.text_run_shape_handle().value;
            if (run && run->material.extruded_text) return true;
        }
    }
    return false;
}

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
      screen_space(other.screen_space),
      hierarchy_resolved(other.hierarchy_resolved),
      cache_static(other.cache_static),
      mask(other.mask),
      m_effects(std::make_unique<EffectStack>(*other.m_effects)),
      blend_mode(other.blend_mode),
      composite_operator(other.composite_operator),
      backdrop_blur(other.backdrop_blur),
      depth_role(other.depth_role),
      depth_offset(other.depth_offset),
      layout(other.layout),
      track_matte(other.track_matte),
      m_material(std::make_unique<Material2_5D>(*other.m_material)),
      m_card3d_material(std::make_unique<Card3DMaterial>(*other.m_card3d_material)),
      m_card3d_material_configured(other.m_card3d_material_configured),
      lottie_precomp_groups(other.lottie_precomp_groups),
      transition_in(other.transition_in),
      transition_out(other.transition_out),
      asset_manifest(other.asset_manifest),
      nodes(other.nodes),
      precomp_composition_name(other.precomp_composition_name),
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
    screen_space = other.screen_space;
    hierarchy_resolved = other.hierarchy_resolved;
    cache_static = other.cache_static;
    mask = other.mask;
    *m_effects = *other.m_effects;
    blend_mode = other.blend_mode;
    composite_operator = other.composite_operator;
    backdrop_blur = other.backdrop_blur;
    depth_role = other.depth_role;
    depth_offset = other.depth_offset;
    layout = other.layout;
    track_matte = other.track_matte;
    *m_material = *other.m_material;
    *m_card3d_material = *other.m_card3d_material;
    m_card3d_material_configured = other.m_card3d_material_configured;
    lottie_precomp_groups = other.lottie_precomp_groups;
    transition_in = other.transition_in;
    transition_out = other.transition_out;
    asset_manifest = other.asset_manifest;
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
    // Always compute fresh — memoization was removed because it could become
    // stale after field mutations (Layer fields are public).  The cost is
    // acceptable: get_static_hash() is called at most once per layer per
    // frame during scene fingerprinting, not per pixel.  If profiling shows
    // this to be a bottleneck, re-add memoization with a LayerVersion counter
    // that is incremented by every mutating accessor.
    using namespace chronon3d::graph;
    uint64_t h = hash_string("chronon.layer-static.v2");
    h = hash_combine(h, hash_string(name));
    h = hash_combine(h, hash_string(parent_name));
    h = hash_combine(h, static_cast<u64>(kind));
    h = hash_combine(h, visible ? 1 : 0);
    h = hash_combine(h, uses_2_5d_projection ? 1 : 0);
    h = hash_combine(h, screen_space ? 1 : 0);
    h = hash_combine(h, cache_static ? 1 : 0);
    h = hash_combine(h, static_cast<u64>(blend_mode));
    h = hash_combine(h, static_cast<u64>(composite_operator));
    h = hash_combine(h, hash_transform_v2(transform));
    h = hash_combine(h, hash_value(static_cast<int64_t>(from)));
    h = hash_combine(h, hash_value(static_cast<int64_t>(duration)));
    h = hash_combine(h, hash_value(static_cast<int64_t>(time_offset)));
    for (const auto& group : lottie_precomp_groups) {
        h = hash_combine(h, hash_string(group.id));
        h = hash_combine(h, hash_value(group.opacity));
        h = hash_combine(h, group.opacity_active ? 1ULL : 0ULL);
        h = hash_combine(h, static_cast<u64>(group.blend_mode));
        h = hash_combine(h, group.opacity_animated ? 1ULL : 0ULL);
    }
    h = hash_combine(h, hash_mask_v2(mask));
    h = hash_combine(h, hash_value(static_cast<int>(track_matte.type)));
    if (track_matte.active())
        h = hash_combine(h, hash_string(track_matte.source_layer));
    h = hash_combine(h, hash_effect_stack_v3(*m_effects));
    h = hash_combine(h, hash_value(m_card3d_material_configured));
    if (m_card3d_material_configured) {
        const auto& material = *m_card3d_material;
        h = hash_combine(h, hash_value(material.enabled));
        h = hash_combine(h, hash_float_v2(material.thickness_px));
        h = hash_combine(h, hash_float_v2(material.corner_radius));
        h = hash_combine(h, hash_color_v2(material.front_top_color));
        h = hash_combine(h, hash_color_v2(material.front_bottom_color));
        h = hash_combine(h, hash_color_v2(material.side_color));
        h = hash_combine(h, hash_float_v2(material.edge_highlight_intensity));
        h = hash_combine(h, hash_color_v2(material.edge_highlight_color));
        h = hash_combine(h, hash_float_v2(material.rim_light_intensity));
        h = hash_combine(h, hash_float_v2(material.rim_light_power));
        h = hash_combine(h, hash_color_v2(material.rim_light_color));
        h = hash_combine(h, hash_vec3_v2(material.light_direction));
        h = hash_combine(h, hash_value(material.cast_shadow));
        h = hash_combine(h, hash_value(material.receive_shadow));
        h = hash_combine(h, hash_value(material.casts_shadows));
        h = hash_combine(h, hash_value(material.accepts_shadows));
    }
    h = hash_combine(h, hash_value(backdrop_blur.has_value()));
    if (backdrop_blur) {
        h = hash_combine(h, hash_float_v2(backdrop_blur->blur));
        h = hash_combine(h, hash_float_v2(backdrop_blur->brightness));
        h = hash_combine(h, hash_float_v2(backdrop_blur->saturation));
        h = hash_combine(h, hash_float_v2(backdrop_blur->rim_light));
        h = hash_combine(h, hash_float_v2(backdrop_blur->refraction));
        h = hash_combine(h, hash_float_v2(backdrop_blur->surface_noise));
    }
    for (const auto& node : nodes) {
        h = hash_combine(h, hash_render_node_v2(node));
    }
    if (kind == LayerKind::Precomp) {
        h = hash_combine(h, hash_string(precomp_composition_name));
    }
    if (kind == LayerKind::Video && video_source)
        h = hash_combine(h, hash_video_source_v2(*video_source));
    if (time_remap.active()) {
        h = hash_combine(h, hash_float_v2(time_remap.speed));
        h = hash_combine(h, hash_value(static_cast<int64_t>(time_remap.freeze_frame)));
    }
    h = hash_combine(h, hash_layer_transition_spec_v2(transition_in));
    h = hash_combine(h, hash_layer_transition_spec_v2(transition_out));
    return h;
}

} // namespace chronon3d
