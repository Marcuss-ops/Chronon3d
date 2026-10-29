#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/inline_mesh.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace chronon3d::render_plan::detail {

void decode_layer_inline_mesh(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("inline_mesh")) {
        if (layer.type != LayerType::Mesh)
            throw std::runtime_error("inline_mesh is only valid for mesh layers");
        const auto& raw_mesh = value.at("inline_mesh");
        InlineMeshPlan mesh;
        if (raw_mesh.contains("metallic_roughness_texture")) {
            if (!raw_mesh["metallic_roughness_texture"].is_string())
                throw std::runtime_error("inline_mesh.metallic_roughness_texture must be a logical path string");
            mesh.metallic_roughness_texture_asset = raw_mesh["metallic_roughness_texture"].get<std::string>();
            if (invalid_logical_path(mesh.metallic_roughness_texture_asset))
                throw std::runtime_error("inline_mesh.metallic_roughness_texture must be a safe logical path");
        }
        if (raw_mesh.contains("primitive")) {
            if (raw_mesh.contains("vertices") || raw_mesh.contains("indices"))
                throw std::runtime_error("inline_mesh primitive and raw vertices/indices are mutually exclusive");
            const auto& raw = raw_mesh.at("primitive");
            if (!raw.is_object()) throw std::runtime_error("inline_mesh.primitive must be an object");
            static const std::unordered_set<std::string> primitive_fields{
                "kind", "width", "height", "depth", "radius", "radius_top", "radius_bottom", "tube",
                "width_segments", "height_segments", "radial_segments", "tubular_segments", "profile", "bevel"};
            InlineMeshPrimitivePlan primitive;
            primitive.kind = raw.value("kind", primitive.kind);
            const auto allowed_for_kind = [&]() -> std::unordered_set<std::string> {
                if (primitive.kind == "box") return {"kind", "width", "height", "depth"};
                if (primitive.kind == "plane") return {"kind", "width", "height", "width_segments", "height_segments"};
                if (primitive.kind == "sphere") return {"kind", "radius", "width_segments", "height_segments"};
                if (primitive.kind == "cylinder") return {"kind", "height", "radius_top", "radius_bottom", "radial_segments", "height_segments"};
                if (primitive.kind == "cone") return {"kind", "height", "radius_bottom", "radial_segments", "height_segments"};
                if (primitive.kind == "torus") return {"kind", "radius", "tube", "radial_segments", "tubular_segments"};
                if (primitive.kind == "extrude") return {"kind", "depth", "profile", "bevel"};
                return {"kind"};
            }();
            for (auto it = raw.begin(); it != raw.end(); ++it)
                if (!primitive_fields.contains(it.key()) || !allowed_for_kind.contains(it.key()))
                    throw std::runtime_error("inline_mesh.primitive has unknown or inapplicable field '" + it.key() + "'");
            primitive.width = raw.value("width", primitive.width);
            primitive.height = raw.value("height", primitive.height);
            primitive.depth = raw.value("depth", primitive.depth);
            primitive.radius = raw.value("radius", primitive.radius);
            primitive.radius_top = raw.value("radius_top", primitive.radius_top);
            primitive.radius_bottom = raw.value("radius_bottom", primitive.radius_bottom);
            primitive.tube = raw.value("tube", primitive.tube);
            primitive.width_segments = raw.value("width_segments", primitive.width_segments);
            primitive.height_segments = raw.value("height_segments", primitive.height_segments);
            primitive.radial_segments = raw.value("radial_segments", primitive.radial_segments);
            primitive.tubular_segments = raw.value("tubular_segments", primitive.tubular_segments);
            primitive.bevel = raw.value("bevel", primitive.bevel);
            if (primitive.kind == "extrude") {
                if (!raw.contains("profile") || !raw["profile"].is_array() ||
                    raw["profile"].size() < 3 || raw["profile"].size() > 4096)
                    throw std::runtime_error("inline_mesh extrusion profile must contain 3..4096 points");
                primitive.profile.reserve(raw["profile"].size());
                for (const auto& point : raw["profile"]) {
                    if (!point.is_array() || point.size() != 2 ||
                        !point[0].is_number() || !point[1].is_number())
                        throw std::runtime_error("inline_mesh extrusion profile points must be numeric pairs");
                    primitive.profile.push_back({point[0].get<float>(), point[1].get<float>()});
                }
            }
            mesh = expand_inline_mesh_primitive(primitive);
        } else {
            const auto& vertices = raw_mesh.at("vertices");
            if (!vertices.is_array() || vertices.size() < 3 || vertices.size() > kInlineMeshMaxVertices)
                throw std::runtime_error("inline_mesh.vertices count must be in [3, 250000]");
            mesh.vertices.reserve(vertices.size());
            for (const auto& raw_vertex : vertices) {
                InlineMeshVertex vertex;
                const auto read_components = [](const nlohmann::json& raw, auto& destination,
                                                const char* field, float limit) {
                    if (!raw.is_array() || raw.size() != destination.size())
                        throw std::runtime_error(std::string("inline_mesh vertex ") + field +
                                                 " has an invalid component count");
                    for (std::size_t i = 0; i < destination.size(); ++i) {
                        destination[i] = raw.at(i).get<float>();
                        if (!std::isfinite(destination[i]) || std::abs(destination[i]) > limit)
                            throw std::runtime_error(std::string("inline_mesh vertex ") + field +
                                                     " components are outside the finite contract range");
                    }
                };
                read_components(raw_vertex.at("position"), vertex.position, "position", 1'000'000.0f);
                if (raw_vertex.contains("normal"))
                    read_components(raw_vertex.at("normal"), vertex.normal, "normal", 1.0f);
                if (raw_vertex.contains("uv"))
                    read_components(raw_vertex.at("uv"), vertex.uv, "uv", 1'000'000.0f);
                mesh.vertices.push_back(vertex);
            }
            const auto& indices = raw_mesh.at("indices");
            if (!indices.is_array() || indices.size() < 3 ||
                indices.size() > kInlineMeshMaxIndices || indices.size() % 3 != 0)
                throw std::runtime_error("inline_mesh.indices count must be divisible by three and in [3, 750000]");
            mesh.indices.reserve(indices.size());
            for (const auto& raw_index : indices) {
                if (!raw_index.is_number_unsigned() &&
                    (!raw_index.is_number_integer() || raw_index.get<std::int64_t>() < 0))
                    throw std::runtime_error("inline_mesh indices must be non-negative integers");
                const auto index = raw_index.get<std::uint64_t>();
                if (index > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("inline_mesh index exceeds uint32 range");
                if (index >= mesh.vertices.size())
                    throw std::runtime_error("inline_mesh index is outside the vertex array");
                mesh.indices.push_back(static_cast<std::uint32_t>(index));
            }
        }
        if (raw_mesh.contains("material")) {
            const auto& raw_material = raw_mesh.at("material");
            if (!raw_material.is_object()) throw std::runtime_error("inline_mesh.material must be an object");
            static const std::unordered_set<std::string> material_fields{
                "diffuse", "specular", "shininess", "rim_intensity", "base_color",
                "metallic", "roughness", "emissive", "double_sided"};
            static const std::unordered_set<std::string> alpha_material_fields{"alpha_cutoff"};
            for (auto it = raw_material.begin(); it != raw_material.end(); ++it)
                if (!material_fields.contains(it.key()) && !alpha_material_fields.contains(it.key()))
                    throw std::runtime_error("inline_mesh.material has unknown field '" + it.key() + "'");
            InlineMeshMaterialPlan material;
            material.diffuse = raw_material.value("diffuse", material.diffuse);
            material.specular = raw_material.value("specular", material.specular);
            material.shininess = raw_material.value("shininess", material.shininess);
            material.rim_intensity = raw_material.value("rim_intensity", material.rim_intensity);
            if (raw_material.contains("base_color")) {
                const auto& color = raw_material.at("base_color");
                std::array<float, 4> decoded{};
                for (std::size_t component = 0; component < decoded.size(); ++component)
                    decoded[component] = color.at(component).get<float>();
                material.base_color = decoded;
            }
            if (raw_material.contains("metallic")) material.metallic = raw_material.at("metallic").get<float>();
            if (raw_material.contains("roughness")) material.roughness = raw_material.at("roughness").get<float>();
            if (raw_material.contains("emissive")) {
                const auto& color = raw_material.at("emissive");
                std::array<float, 3> decoded{};
                for (std::size_t component = 0; component < decoded.size(); ++component)
                    decoded[component] = color.at(component).get<float>();
                material.emissive = decoded;
            }
            if (raw_material.contains("double_sided")) material.double_sided = raw_material.at("double_sided").get<bool>();
            if (raw_material.contains("alpha_cutoff")) material.alpha_cutoff = raw_material.at("alpha_cutoff").get<float>();
            if (!std::isfinite(material.diffuse) || material.diffuse < 0.0f || material.diffuse > 1.0f ||
                !std::isfinite(material.specular) || material.specular < 0.0f || material.specular > 1.0f ||
                !std::isfinite(material.shininess) || material.shininess < 1.0f || material.shininess > 1024.0f ||
                !std::isfinite(material.rim_intensity) || material.rim_intensity < 0.0f || material.rim_intensity > 16.0f)
                throw std::runtime_error("inline_mesh.material values are outside their supported ranges");
            if (material.base_color && !std::all_of(material.base_color->begin(), material.base_color->end(),
                    [](float component) { return std::isfinite(component) && component >= 0.0f && component <= 1.0f; }))
                throw std::runtime_error("inline_mesh.material.base_color components must be finite and in [0, 1]");
            if ((material.metallic && (!std::isfinite(*material.metallic) || *material.metallic < 0.0f || *material.metallic > 1.0f)) ||
                (material.roughness && (!std::isfinite(*material.roughness) || *material.roughness < 0.0f || *material.roughness > 1.0f)))
                throw std::runtime_error("inline_mesh.material metallic/roughness must be finite and in [0, 1]");
            if (material.emissive && !std::all_of(material.emissive->begin(), material.emissive->end(),
                    [](float component) { return std::isfinite(component) && component >= 0.0f && component <= 10000.0f; }))
                throw std::runtime_error("inline_mesh.material.emissive components must be finite and in [0, 10000]");
            if (material.alpha_cutoff && (!std::isfinite(*material.alpha_cutoff) || *material.alpha_cutoff < 0.0f || *material.alpha_cutoff > 1.0f))
                throw std::runtime_error("inline_mesh.material.alpha_cutoff must be finite and in [0, 1]");
            mesh.material = material;
        }
        if (raw_mesh.contains("animation")) {
            const auto& raw_animation = raw_mesh.at("animation");
            if (!raw_animation.is_array() || raw_animation.empty() ||
                raw_animation.size() > kInlineMeshMaxAnimationSamples)
                throw std::runtime_error("inline_mesh.animation sample count must be in [1, 4096]");
            std::int64_t previous_frame = -1;
            mesh.animation.reserve(raw_animation.size());
            for (const auto& raw_sample : raw_animation) {
                InlineMeshFrameSamplePlan sample;
                sample.frame = raw_sample.at("frame").get<std::int64_t>();
                if (sample.frame < 0 || sample.frame > kInlineMeshMaxAnimationFrame ||
                    sample.frame <= previous_frame)
                    throw std::runtime_error(
                        "inline_mesh.animation frames must be in [0, 1000000] and strictly increasing");
                previous_frame = sample.frame;
                const auto& raw_positions = raw_sample.at("positions");
                if (!raw_positions.is_array() || raw_positions.size() != mesh.vertices.size())
                    throw std::runtime_error(
                        "inline_mesh.animation positions must carry one entry per base vertex");
                sample.positions.reserve(raw_positions.size());
                for (const auto& raw_position : raw_positions) {
                    if (!raw_position.is_array() || raw_position.size() != 3)
                        throw std::runtime_error("inline_mesh.animation positions must be 3D vectors");
                    std::array<float, 3> position{};
                    for (std::size_t i = 0; i < 3; ++i) {
                        position[i] = raw_position.at(i).get<float>();
                        if (!std::isfinite(position[i]) || std::abs(position[i]) > 1'000'000.0f)
                            throw std::runtime_error(
                                "inline_mesh.animation positions are outside the finite contract range");
                    }
                    sample.positions.push_back(position);
                }
                if (raw_sample.contains("normals")) {
                    const auto& raw_normals = raw_sample.at("normals");
                    if (!raw_normals.is_array() || raw_normals.size() != mesh.vertices.size())
                        throw std::runtime_error("inline_mesh.animation normals must carry one entry per base vertex");
                    sample.normals.reserve(raw_normals.size());
                    for (const auto& raw_normal : raw_normals) {
                        if (!raw_normal.is_array() || raw_normal.size() != 3)
                            throw std::runtime_error("inline_mesh.animation normals must be 3D vectors");
                        std::array<float, 3> normal{};
                        for (std::size_t i = 0; i < 3; ++i) {
                            normal[i] = raw_normal.at(i).get<float>();
                            if (!std::isfinite(normal[i]) || std::abs(normal[i]) > 1.0f)
                                throw std::runtime_error("inline_mesh.animation normals must be finite and within [-1, 1]");
                        }
                        sample.normals.push_back(normal);
                    }
                }
                mesh.animation.push_back(std::move(sample));
            }
        }
        layer.inline_mesh = std::move(mesh);
    } else if (layer.type == LayerType::Mesh) {
        throw std::runtime_error("mesh layers require inline_mesh geometry");
    }

}

} // namespace chronon3d::render_plan::detail
