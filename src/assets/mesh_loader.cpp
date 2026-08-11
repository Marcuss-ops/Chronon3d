#include <chronon3d/assets/mesh_loader.hpp>

#ifdef CHRONON3D_ENABLE_MESH
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace chronon3d::assets {
namespace {

using json = nlohmann::json;

std::uint32_t read_u32(const std::vector<std::byte>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) throw std::out_of_range("GLB u32 out of bounds");
    return static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset]))
        | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 1])) << 8U)
        | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 2])) << 16U)
        | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 3])) << 24U);
}

float read_f32(const std::vector<std::byte>& bytes, std::size_t offset) {
    const auto raw = read_u32(bytes, offset);
    float value{};
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::size_t component_size(int component_type) {
    switch (component_type) {
        case 5121: return 1;
        case 5123: return 2;
        case 5125: return 4;
        case 5126: return 4;
        default: throw std::runtime_error("unsupported GLB component type");
    }
}

std::size_t checked_add(std::size_t lhs, std::size_t rhs, const char* message) {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) throw std::runtime_error(message);
    return lhs + rhs;
}

std::size_t checked_mul(std::size_t lhs, std::size_t rhs, const char* message) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs)
        throw std::runtime_error(message);
    return lhs * rhs;
}

std::size_t component_count(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    throw std::runtime_error("unsupported GLB accessor type");
}

Vec3 read_vec3(const json& value, const Vec3& fallback) {
    if (value.is_null()) return fallback;
    if (!value.is_array() || value.size() != 3)
        throw std::runtime_error("GLB node vector must contain three values");
    return {value.at(0).get<f32>(), value.at(1).get<f32>(), value.at(2).get<f32>()};
}

Mat4 gltf_node_transform(const json& node) {
    if (node.contains("matrix")) {
        const auto& values = node.at("matrix");
        if (!values.is_array() || values.size() != 16)
            throw std::runtime_error("GLB node matrix must contain 16 values");
        Mat4 matrix{1.0f};
        // glTF matrices are column-major, matching GLM's storage convention.
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                matrix[column][row] = values.at(column * 4 + row).get<f32>();
            }
        }
        return matrix;
    }

    const Vec3 translation = read_vec3(node.value("translation", json(nullptr)), Vec3{0.0f});
    const Vec3 scale = read_vec3(node.value("scale", json(nullptr)), Vec3{1.0f});
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    if (node.contains("rotation")) {
        const auto& values = node.at("rotation");
        if (!values.is_array() || values.size() != 4)
            throw std::runtime_error("GLB node rotation must contain four values");
        // glTF stores quaternions as [x, y, z, w]; GLM constructs [w, x, y, z].
        rotation = Quat{
            values.at(3).get<f32>(), values.at(0).get<f32>(),
            values.at(1).get<f32>(), values.at(2).get<f32>()};
        if (glm::length(rotation) < 1e-7f)
            throw std::runtime_error("GLB node rotation must not be zero");
        rotation = glm::normalize(rotation);
    }
    return glm::translate(Mat4{1.0f}, translation)
        * glm::toMat4(rotation)
        * glm::scale(Mat4{1.0f}, scale);
}

// glTF 2.0 and Chronon both use right-handed +X right, +Y up, -Z camera
// forward coordinates. Keeping this explicit makes the import contract
// reviewable and prevents a future axis conversion from leaking into layers.
Mat4 gltf_to_chronon_matrix() {
    return Mat4{1.0f};
}

struct GlbData {
    json document;
    std::vector<std::byte> binary;
};

GlbData read_glb(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open GLB");
    const std::vector<char> raw{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    if (bytes.size() < 20 || read_u32(bytes, 0) != 0x46546C67U || read_u32(bytes, 4) != 2U) {
        throw std::runtime_error("invalid GLB header");
    }
    const auto declared_length = read_u32(bytes, 8);
    if (declared_length != bytes.size()) throw std::runtime_error("invalid GLB length");

    std::size_t offset = 12;
    std::string json_chunk;
    std::vector<std::byte> binary;
    while (offset + 8 <= bytes.size()) {
        const auto length = read_u32(bytes, offset);
        const auto type = read_u32(bytes, offset + 4);
        offset += 8;
        if (offset + length > bytes.size()) throw std::runtime_error("invalid GLB chunk");
        if (type == 0x4E4F534AU) {
            json_chunk.reserve(length);
            for (std::size_t i = 0; i < length; ++i) {
                json_chunk.push_back(static_cast<char>(bytes[offset + i]));
            }
        } else if (type == 0x004E4942U) {
            binary.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        }
        offset += length;
    }
    if (json_chunk.empty() || binary.empty()) throw std::runtime_error("GLB requires JSON and BIN chunks");
    return {json::parse(json_chunk), std::move(binary)};
}

std::size_t accessor_offset(const json& document, const std::vector<std::byte>& binary,
                            int accessor_index, std::size_t element, std::size_t component) {
    if (accessor_index < 0 || !document.contains("accessors")
        || static_cast<std::size_t>(accessor_index) >= document.at("accessors").size()) {
        throw std::runtime_error("GLB accessor index is out of range");
    }
    const auto& accessor = document.at("accessors").at(accessor_index);
    const auto view_index = accessor.at("bufferView").get<int>();
    if (view_index < 0 || !document.contains("bufferViews")
        || static_cast<std::size_t>(view_index) >= document.at("bufferViews").size()) {
        throw std::runtime_error("GLB bufferView index is out of range");
    }
    const auto& view = document.at("bufferViews").at(view_index);
    if (view.value("buffer", 0) != 0) throw std::runtime_error("GLB uses an unsupported buffer index");
    const auto element_bytes = checked_mul(
        component_size(accessor.at("componentType")),
        component_count(accessor.at("type")), "GLB accessor element size overflow");
    const auto stride = view.value("byteStride", element_bytes);
    const auto count = accessor.at("count").get<std::size_t>();
    if (component >= component_count(accessor.at("type")) || element >= count
        || stride < element_bytes) throw std::runtime_error("GLB accessor shape is invalid");
    const auto view_offset = view.value("byteOffset", 0U);
    const auto view_length = view.at("byteLength").get<std::size_t>();
    const auto accessor_offset_in_view = accessor.value("byteOffset", 0U);
    if (view_offset > binary.size() || view_length > binary.size() - view_offset
        || accessor_offset_in_view > view_length) {
        throw std::runtime_error("GLB bufferView exceeds binary chunk");
    }
    const auto element_offset = checked_add(
        checked_add(accessor_offset_in_view,
                    checked_mul(element, stride, "GLB accessor offset overflow"),
                    "GLB accessor offset overflow"),
        checked_mul(component, component_size(accessor.at("componentType")),
                    "GLB accessor component offset overflow"),
        "GLB accessor offset overflow");
    const auto component_bytes = component_size(accessor.at("componentType"));
    if (element_offset > view_length || component_bytes > view_length - element_offset) {
        throw std::runtime_error("GLB accessor exceeds bufferView");
    }
    const auto offset = checked_add(view_offset, element_offset, "GLB binary offset overflow");
    if (offset > binary.size() || component_bytes > binary.size() - offset) {
        throw std::runtime_error("GLB accessor exceeds binary chunk");
    }
    return offset;
}

float read_float_component(const json& document, const std::vector<std::byte>& binary,
                           int accessor_index, std::size_t element, std::size_t component) {
    const auto& accessor = document.at("accessors").at(accessor_index);
    if (accessor.at("componentType") != 5126) throw std::runtime_error("mesh attributes must be float32");
    return read_f32(binary, accessor_offset(document, binary, accessor_index, element, component));
}

std::uint32_t read_index(const json& document, const std::vector<std::byte>& binary,
                         int accessor_index, std::size_t element) {
    const auto& accessor = document.at("accessors").at(accessor_index);
    const auto offset = accessor_offset(document, binary, accessor_index, element, 0);
    switch (accessor.at("componentType").get<int>()) {
        case 5121: return std::to_integer<unsigned char>(binary[offset]);
        case 5123: return static_cast<std::uint32_t>(std::to_integer<unsigned char>(binary[offset]))
            | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(binary[offset + 1])) << 8U);
        case 5125: return read_u32(binary, offset);
        default: throw std::runtime_error("unsupported GLB index component type");
    }
}

MeshIdentity identity_for(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) throw std::runtime_error("could not stat GLB");
    const auto timestamp = std::filesystem::last_write_time(path, ec);
    if (ec) throw std::runtime_error("could not timestamp GLB");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not read GLB for identity");
    const std::string contents{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (contents.size() != size) throw std::runtime_error("GLB changed while reading identity");
    return {
        path.lexically_normal().string(), size,
        static_cast<std::int64_t>(timestamp.time_since_epoch().count()),
        sha256_string(contents)};
}

std::vector<std::byte> embedded_bytes(const json& document,
                                      const std::vector<std::byte>& binary,
                                      int view_index) {
    if (view_index < 0 || !document.contains("bufferViews")
        || static_cast<std::size_t>(view_index) >= document.at("bufferViews").size()) {
        throw std::runtime_error("GLB image bufferView index is out of range");
    }
    const auto& view = document.at("bufferViews").at(view_index);
    if (view.value("buffer", 0) != 0) throw std::runtime_error("GLB image uses an unsupported buffer index");
    const auto offset = view.value("byteOffset", 0U);
    const auto length = view.at("byteLength").get<std::size_t>();
    if (offset > binary.size() || length > binary.size() - offset)
        throw std::runtime_error("GLB image bufferView exceeds binary chunk");
    return {binary.begin() + static_cast<std::ptrdiff_t>(offset),
            binary.begin() + static_cast<std::ptrdiff_t>(offset + length)};
}

void decode_materials_and_images(const json& document,
                                 const std::vector<std::byte>& binary,
                                 PreparedMeshSource& prepared) {
    if (document.contains("images")) {
        const auto& images = document.at("images");
        prepared.images.reserve(images.size());
        for (const auto& image : images) {
            if (!image.contains("bufferView"))
                throw std::runtime_error("V1 embedded GLB image requires bufferView");
            prepared.images.push_back(MeshImage{
                .mime_type = image.value("mimeType", "application/octet-stream"),
                .payload = embedded_bytes(document, binary, image.at("bufferView").get<int>()),
            });
        }
    }
    if (document.contains("materials")) {
        const auto& materials = document.at("materials");
        prepared.materials.reserve(materials.size());
        for (const auto& material : materials) {
            MeshMaterial converted;
            converted.name = material.value("name", "");
            const auto& pbr = material.value("pbrMetallicRoughness", json::object());
            if (pbr.contains("baseColorFactor")) {
                const auto& factor = pbr.at("baseColorFactor");
                if (!factor.is_array() || factor.size() != 4)
                    throw std::runtime_error("GLB baseColorFactor must contain four values");
                converted.base_color_factor = Color{
                    factor.at(0).get<f32>(), factor.at(1).get<f32>(),
                    factor.at(2).get<f32>(), factor.at(3).get<f32>()};
            }
            if (pbr.contains("baseColorTexture")) {
                const auto texture_index = pbr.at("baseColorTexture").at("index").get<int>();
                if (texture_index < 0 || !document.contains("textures")
                    || static_cast<std::size_t>(texture_index) >= document.at("textures").size())
                    throw std::runtime_error("GLB baseColorTexture index is out of range");
                const auto image_index = document.at("textures").at(texture_index).at("source").get<int>();
                if (image_index < 0 || static_cast<std::size_t>(image_index) >= prepared.images.size())
                    throw std::runtime_error("GLB baseColorTexture source is out of range");
                converted.base_color_texture_index = static_cast<std::uint32_t>(image_index);
            }
            prepared.materials.push_back(std::move(converted));
        }
    }
}

PreparedMeshSourceRef decode(const InternalAssetRef& ref, const std::filesystem::path& path,
                             MeshIdentity identity) {
    auto data = read_glb(path);
    if (!data.document.contains("meshes")) throw std::runtime_error("GLB has no meshes");

    if (!data.document.contains("buffers") || data.document.at("buffers").empty()
        || data.document.at("buffers").at(0).value("byteLength", 0U) > data.binary.size()) {
        throw std::runtime_error("GLB buffer byteLength is invalid");
    }
    auto prepared = std::make_shared<PreparedMeshSource>();
    prepared->logical_path = ref.path;
    prepared->resolved_path = path;
    prepared->identity = identity;
    decode_materials_and_images(data.document, data.binary, *prepared);

    // Resolve mesh instances from the active glTF scene. Only a GLB without
    // nodes/scenes uses the identity fallback, preserving V1 flat-mesh behavior.
    std::vector<std::pair<std::size_t, Mat4>> instances;
    const auto& meshes = data.document.at("meshes");
    const bool has_nodes = data.document.contains("nodes");
    const bool has_scenes = data.document.contains("scenes");
    if (has_nodes != has_scenes)
        throw std::runtime_error("GLB scene graph must provide both nodes and scenes");
    const bool has_scene_graph = has_nodes && has_scenes;
    if (has_scene_graph) {
        if (!data.document.at("nodes").is_array() || !data.document.at("scenes").is_array()
            || data.document.at("scenes").empty())
            throw std::runtime_error("GLB scene graph arrays are invalid");
        const auto scene_index = data.document.value("scene", 0);
        if (scene_index < 0 || static_cast<std::size_t>(scene_index) >= data.document.at("scenes").size())
            throw std::runtime_error("GLB default scene index is out of range");

        std::vector<bool> active(data.document.at("nodes").size(), false);
        const auto visit = [&](const auto& self, std::size_t node_index, const Mat4& parent) -> void {
            if (node_index >= data.document.at("nodes").size())
                throw std::runtime_error("GLB node index is out of range");
            if (active[node_index]) throw std::runtime_error("GLB node hierarchy contains a cycle");
            active[node_index] = true;
            const auto& node = data.document.at("nodes").at(node_index);
            const Mat4 world = parent * gltf_node_transform(node);
            if (node.contains("mesh")) {
                const auto mesh_index = node.at("mesh").get<std::size_t>();
                if (mesh_index >= meshes.size()) throw std::runtime_error("GLB node mesh index is out of range");
                instances.emplace_back(mesh_index, gltf_to_chronon_matrix() * world);
            }
            if (node.contains("children")) {
                for (const auto& child : node.at("children"))
                    self(self, child.get<std::size_t>(), world);
            }
            active[node_index] = false;
        };

        const auto& roots = data.document.at("scenes").at(scene_index).value("nodes", json::array());
        for (const auto& root : roots) visit(visit, root.get<std::size_t>(), Mat4{1.0f});
    }
    if (!has_scene_graph) {
        for (std::size_t mesh_index = 0; mesh_index < meshes.size(); ++mesh_index)
            instances.emplace_back(mesh_index, gltf_to_chronon_matrix());
    }

    for (const auto& [mesh_index, bake_matrix] : instances) {
        const auto& mesh_json = meshes.at(mesh_index);
        const auto& primitives = mesh_json.at("primitives");
        const auto linear = glm::mat3(bake_matrix);
        const auto determinant = glm::determinant(linear);
        if (std::abs(determinant) < 1e-7f)
            throw std::runtime_error("GLB node transform has a degenerate linear matrix");
        const auto normal_matrix = glm::transpose(glm::inverse(linear));
        const bool reverse_winding = determinant < 0.0f;

        for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
            const auto& primitive = primitives.at(primitive_index);
            if (primitive.value("mode", 4) != 4) throw std::runtime_error("only TRIANGLES GLB primitives are supported");
            const auto& attributes = primitive.at("attributes");
            if (!attributes.contains("POSITION")) throw std::runtime_error("GLB primitive has no POSITION");
            const int position_accessor = attributes.at("POSITION");
            const auto& position = data.document.at("accessors").at(position_accessor);
            if (position.at("componentType") != 5126 || position.at("type") != "VEC3")
                throw std::runtime_error("POSITION must be a float VEC3 accessor");
            const auto vertex_count = position.at("count").get<std::size_t>();
            const auto instance_name = mesh_json.value("name", "mesh") + "/" + std::to_string(primitive_index);
            auto geometry = std::make_shared<Mesh>(instance_name);
            const auto normal_accessor = attributes.value("NORMAL", -1);
            const auto uv_accessor = attributes.value("TEXCOORD_0", -1);
            if (normal_accessor >= 0) {
                const auto& normal = data.document.at("accessors").at(normal_accessor);
                if (normal.at("componentType") != 5126 || normal.at("type") != "VEC3"
                    || normal.at("count") != vertex_count)
                    throw std::runtime_error("NORMAL must match POSITION as float VEC3");
            }
            if (uv_accessor >= 0) {
                const auto& uv = data.document.at("accessors").at(uv_accessor);
                if (uv.at("componentType") != 5126 || uv.at("type") != "VEC2"
                    || uv.at("count") != vertex_count)
                    throw std::runtime_error("TEXCOORD_0 must match POSITION as float VEC2");
            }
            for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
                const Vec3 source_position{
                    read_float_component(data.document, data.binary, position_accessor, vertex, 0),
                    read_float_component(data.document, data.binary, position_accessor, vertex, 1),
                    read_float_component(data.document, data.binary, position_accessor, vertex, 2)};
                Vec3 normal{};
                if (normal_accessor >= 0) {
                    const Vec3 source_normal{
                        read_float_component(data.document, data.binary, normal_accessor, vertex, 0),
                        read_float_component(data.document, data.binary, normal_accessor, vertex, 1),
                        read_float_component(data.document, data.binary, normal_accessor, vertex, 2)};
                    const Vec3 transformed = normal_matrix * source_normal;
                    if (glm::length(transformed) > 1e-7f) normal = glm::normalize(transformed);
                }
                Vec2 uv{};
                if (uv_accessor >= 0) {
                    uv = {
                        read_float_component(data.document, data.binary, uv_accessor, vertex, 0),
                        read_float_component(data.document, data.binary, uv_accessor, vertex, 1)};
                }
                const Vec3 baked_position = Vec3{bake_matrix * Vec4(source_position, 1.0f)};
                geometry->add_vertex(Vertex{baked_position, normal, uv});
            }
            if (!primitive.contains("indices")) throw std::runtime_error("GLB primitive has no indices");
            const int index_accessor = primitive.at("indices");
            const auto& indices = data.document.at("accessors").at(index_accessor);
            if (indices.at("type") != "SCALAR")
                throw std::runtime_error("GLB indices must use a SCALAR accessor");
            const auto index_count = indices.at("count").get<std::size_t>();
            if (index_count == 0 || index_count % 3 != 0)
                throw std::runtime_error("GLB triangle index count must be a non-zero multiple of three");
            for (std::size_t index = 0; index < index_count; index += 3) {
                const auto i0 = read_index(data.document, data.binary, index_accessor, index);
                const auto i1 = read_index(data.document, data.binary, index_accessor, index + 1);
                const auto i2 = read_index(data.document, data.binary, index_accessor, index + 2);
                if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
                    throw std::runtime_error("GLB index exceeds POSITION vertex count");
                geometry->add_index(i0);
                geometry->add_index(reverse_winding ? i2 : i1);
                geometry->add_index(reverse_winding ? i1 : i2);
            }
            std::optional<std::uint32_t> material_index;
            if (primitive.contains("material")) {
                const auto index = primitive.at("material").get<int>();
                if (index < 0 || static_cast<std::size_t>(index) >= prepared->materials.size())
                    throw std::runtime_error("GLB primitive material index is out of range");
                material_index = static_cast<std::uint32_t>(index);
            }
            prepared->parts.push_back(MeshPart{
                .name = geometry->name(),
                .geometry = std::move(geometry),
                .material_index = material_index});
        }
    }
    if (prepared->parts.empty()) throw std::runtime_error("GLB has no mesh primitives");
    return prepared;
}

} // namespace
#endif // CHRONON3D_ENABLE_MESH

#ifndef CHRONON3D_ENABLE_MESH
namespace chronon3d::assets {
#endif

std::string MeshIdentity::cache_key() const {
    return resolved_path + "\n" + std::to_string(byte_size) + "\n"
        + std::to_string(write_time) + "\n" + content_digest.hex();
}

#ifdef CHRONON3D_ENABLE_MESH
Result<PreparedMeshSourceRef, MeshLoadError> MeshLoader::load(
    const InternalAssetRef& ref, const AssetResolver& resolver, MeshPreparationCache* cache) {
    if (ref.kind != AssetKind::Mesh) {
        return MeshLoadError{MeshLoadErrorCode::InvalidReference, ref.path,
                             "mesh preparation requires AssetKind::Mesh"};
    }
    if (ref.path.empty()) {
        return MeshLoadError{MeshLoadErrorCode::InvalidReference, ref.path,
                             "mesh reference path is empty"};
    }
    const auto extension = std::filesystem::path{ref.path}.extension().string();
    if (extension != ".glb" && extension != ".GLB") {
        return MeshLoadError{MeshLoadErrorCode::UnsupportedGlb, ref.path,
                             "unsupported mesh format '" + extension
                                 + "'; V1 accepts only self-contained .glb (not .gltf)"};
    }
    const auto resolved = resolver.resolve(ref.path);
    if (!resolved.has_value()) {
        return MeshLoadError{MeshLoadErrorCode::MissingAsset, ref.path,
                             "mesh asset not found: " + ref.path};
    }
    MeshIdentity identity;
    try {
        identity = identity_for(*resolved);
        if (cache) {
            if (const auto cached = cache->find(identity); cached.has_value()) return *cached;
        }
        auto loaded = decode(ref, *resolved, identity);
        if (!(identity == identity_for(*resolved))) {
            return MeshLoadError{MeshLoadErrorCode::ReadFailed, ref.path,
                                 "GLB changed while it was being prepared"};
        }
        if (cache) cache->store(identity, loaded);
        return loaded;
    } catch (const json::exception& e) {
        return MeshLoadError{MeshLoadErrorCode::InvalidGlb, ref.path, std::string{"invalid GLB JSON: "} + e.what()};
    } catch (const std::ios_base::failure& e) {
        return MeshLoadError{MeshLoadErrorCode::ReadFailed, ref.path, e.what()};
    } catch (const std::exception& e) {
        return MeshLoadError{MeshLoadErrorCode::InvalidGeometry, ref.path, e.what()};
    }
}
#else
Result<PreparedMeshSourceRef, MeshLoadError> MeshLoader::load(
    const InternalAssetRef& ref, const AssetResolver&, MeshPreparationCache*) {
    return MeshLoadError{
        MeshLoadErrorCode::UnsupportedGlb,
        ref.path,
        "Mesh support is disabled (CHRONON3D_ENABLE_MESH=OFF)"};
}
#endif

} // namespace chronon3d::assets
