#include <chronon3d/rendering/lighting_hashing.hpp>

#include <chronon3d/assets/hdri_environment.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::rendering {
namespace {

using chronon3d::core::hash::HashBuilder;

void add_float(HashBuilder& h, f32 value) {
    h.add(value == 0.0f ? 0.0f : value);
}

void add_vec3(HashBuilder& h, const Vec3& v) {
    add_float(h, v.x);
    add_float(h, v.y);
    add_float(h, v.z);
}

void add_color(HashBuilder& h, const Color& c) {
    add_float(h, c.r);
    add_float(h, c.g);
    add_float(h, c.b);
    add_float(h, c.a);
}

void add_shadows(HashBuilder& h, const ShadowSettings& s) {
    add_float(h, s.opacity);
    add_float(h, s.blur_radius);
    add_float(h, s.px_per_unit);
    add_float(h, s.max_offset);
    add_float(h, s.contact_opacity);
    add_float(h, s.contact_blur_radius);
    add_float(h, s.ambient_opacity);
    add_float(h, s.ambient_blur_radius);
    h.add(s.depth_aware);
    add_float(h, s.blur_per_z);
    add_float(h, s.opacity_falloff);
    add_color(h, s.tint);
}

void add_point_light(HashBuilder& h, const PointLightSource& s) {
    h.add_enum(s.kind);
    add_vec3(h, s.position);
    add_vec3(h, s.direction);
    add_color(h, s.color);
    add_float(h, s.radius);
    add_float(h, s.range);
    add_float(h, s.decay);
    h.add_enum(s.falloff);
    add_float(h, s.angle);
    add_float(h, s.penumbra);
    h.add(s.casts_shadow);
    add_float(h, s.shadow_radius);
    add_float(h, s.shadow_bias);
}

} // namespace

std::uint64_t hash_light_context_v2(const LightContext& light) {
    auto h = HashBuilder{}.add("chronon.light-context.v2");
    h.add(light.enabled).add(light.ambient_enabled).add(light.directional_enabled);
    add_vec3(h, light.direction);
    add_color(h, light.ambient_color);
    add_float(h, light.ambient);
    add_color(h, light.directional_color);
    add_float(h, light.diffuse);
    h.add(light.environment_enabled);
    h.add(light.environment_irradiance_sh.size());
    for (const auto coefficient : light.environment_irradiance_sh)
        add_float(h, coefficient);
    add_float(h, light.environment_intensity);
    add_float(h, light.environment_rotation_y);
    h.add(static_cast<bool>(light.environment_probe));
    if (light.environment_probe) h.add(light.environment_probe->content_hash);
    add_shadows(h, light.shadows);
    h.add(light.point_lights.size());
    for (const auto& source : light.point_lights) add_point_light(h, source);
    return h.finish();
}

std::uint64_t hash_material_2_5d_v2(const Material2_5D& material) {
    auto h = HashBuilder{}.add("chronon.material-2.5d.v2");
    h.add(material.accepts_lights).add(material.casts_shadows)
     .add(material.accepts_shadows).add(material.shadow_catcher);
    add_float(h, material.diffuse);
    add_float(h, material.specular);
    add_float(h, material.shininess);
    add_float(h, material.ambient_multiplier);
    add_color(h, material.emissive_color);
    h.add(material.emissive_enabled).add(material.double_sided);
    h.add(material.alpha_cutoff.has_value());
    if (material.alpha_cutoff) add_float(h, *material.alpha_cutoff);
    h.add(material.metallic.has_value());
    if (material.metallic) add_float(h, *material.metallic);
    h.add(material.roughness.has_value());
    if (material.roughness) add_float(h, *material.roughness);
    return h.finish();
}

} // namespace chronon3d::rendering
