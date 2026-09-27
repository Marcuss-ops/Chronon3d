PreparedMeshSourceRef decode(const InternalAssetRef& ref, const std::filesystem::path& path,
                             MeshIdentity identity) {
    auto data = read_glb(path);
    if (!data.document.contains("meshes") || !data.document.at("meshes").is_array())
        throw std::runtime_error("GLB has no mesh array");
    if (data.document.at("meshes").size() > kMeshLoaderV1MaxParts)
        throw std::runtime_error("GLB mesh count exceeds the MeshLoader V1 limit of 8192");
    for (const auto* key : {"extensionsUsed", "extensionsRequired"}) {
        if (data.document.contains(key) &&
            (!data.document.at(key).is_array() || !data.document.at(key).empty()))
            throw std::runtime_error("glTF extensions are not supported by the MeshLoader V1 contract");
    }
    if (data.document.contains("extensions"))
        throw std::runtime_error("glTF root extensions are not supported by MeshLoader V1");
    if (data.document.contains("animations") && !data.document.at("animations").empty())
        throw std::runtime_error("animated GLB clips are not supported by the static MeshLoader contract");
    if (data.document.contains("skins") && !data.document.at("skins").empty())
        throw std::runtime_error("skinned GLB nodes are not supported by the static MeshLoader contract");
    if (data.document.contains("nodes")) {
        for (const auto& node : data.document.at("nodes"))
            if (node.contains("skin") || node.contains("weights"))
                throw std::runtime_error("skinned or morph-weight GLB nodes are not supported by the static MeshLoader contract");
    }

    if (!data.document.contains("buffers") || !data.document.at("buffers").is_array() ||
        data.document.at("buffers").size() != 1)
        throw std::runtime_error("self-contained GLB must contain exactly one buffer");
    const auto& buffer = data.document.at("buffers").at(0);
    if (!buffer.is_object() || buffer.contains("uri") || buffer.contains("extensions"))
        throw std::runtime_error("external GLB buffers and buffer extensions are not supported");
    if (!buffer.contains("byteLength"))
        throw std::runtime_error("GLB buffer byteLength is missing");
    const auto declared_buffer_bytes = buffer.at("byteLength").get<std::size_t>();
    if (declared_buffer_bytes > data.binary.size() ||
        data.binary.size() - declared_buffer_bytes > 3) {
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
        if (data.document.at("scenes").size() > kMeshLoaderV1MaxScenes)
            throw std::runtime_error("GLB scene count exceeds the MeshLoader V1 limit of 4096");
        if (data.document.at("nodes").size() > kMeshLoaderV1MaxNodes)
            throw std::runtime_error("GLB node count exceeds the MeshLoader V1 limit of 100000");
        const auto scene_index = data.document.value("scene", 0);
        if (scene_index < 0 || static_cast<std::size_t>(scene_index) >= data.document.at("scenes").size())
            throw std::runtime_error("GLB default scene index is out of range");

        std::vector<bool> active(data.document.at("nodes").size(), false);
        std::vector<bool> seen(data.document.at("nodes").size(), false);
        std::size_t visited_nodes = 0;
        const auto visit = [&](const auto& self, std::size_t node_index,
                               const Mat4& parent, std::size_t depth) -> void {
            if (node_index >= data.document.at("nodes").size())
                throw std::runtime_error("GLB node index is out of range");
            if (active[node_index]) throw std::runtime_error("GLB node hierarchy contains a cycle");
            if (seen[node_index])
                throw std::runtime_error("GLB scene graph reuses a node from multiple parents");
            if (depth > kMeshLoaderV1MaxSceneDepth)
                throw std::runtime_error("GLB scene hierarchy exceeds the V1 depth limit of 256");
            if (++visited_nodes > kMeshLoaderV1MaxNodes)
                throw std::runtime_error("GLB scene traversal exceeds the V1 node visit limit");
            active[node_index] = true;
            seen[node_index] = true;
            const auto& node = data.document.at("nodes").at(node_index);
            if (node.contains("camera") || node.contains("extensions"))
                throw std::runtime_error("GLB camera nodes and node extensions are not supported by MeshLoader V1");
            const Mat4 world = parent * gltf_node_transform(node);
            if (node.contains("mesh")) {
                const auto mesh_index = node.at("mesh").get<std::size_t>();
                if (mesh_index >= meshes.size()) throw std::runtime_error("GLB node mesh index is out of range");
                if (instances.size() >= kMeshLoaderV1MaxInstances)
                    throw std::runtime_error("GLB scene exceeds the V1 mesh instance limit of 4096");
                instances.emplace_back(mesh_index, gltf_to_chronon_matrix() * world);
            }
            if (node.contains("children")) {
                if (!node.at("children").is_array())
                    throw std::runtime_error("GLB node children must be an array");
                for (const auto& child : node.at("children"))
                    self(self, child.get<std::size_t>(), world, depth + 1);
            }
            active[node_index] = false;
        };

        const auto& roots = data.document.at("scenes").at(scene_index).value("nodes", json::array());
        if (!roots.is_array()) throw std::runtime_error("GLB scene roots must be an array");
        for (const auto& root : roots)
            visit(visit, root.get<std::size_t>(), Mat4{1.0f}, 0);
    }
    if (!has_scene_graph) {
        if (meshes.size() > kMeshLoaderV1MaxInstances)
            throw std::runtime_error("GLB fallback exceeds the V1 mesh instance limit of 4096");
        for (std::size_t mesh_index = 0; mesh_index < meshes.size(); ++mesh_index)
            instances.emplace_back(mesh_index, gltf_to_chronon_matrix());
    }

    std::size_t total_parts = 0;
    std::size_t total_vertices = 0;
    std::size_t total_indices = 0;
    for (const auto& [mesh_index, bake_matrix] : instances) {
        const auto& mesh_json = meshes.at(mesh_index);
        if (mesh_json.contains("weights") || mesh_json.contains("extensions"))
            throw std::runtime_error("GLB mesh weights and extensions are not supported by MeshLoader V1");
        const auto& primitives = mesh_json.at("primitives");
        if (!primitives.is_array())
            throw std::runtime_error("GLB mesh primitives must be an array");
        if (primitives.size() > kMeshLoaderV1MaxParts - total_parts)
            throw std::runtime_error("GLB primitive count exceeds the MeshLoader V1 limit of 8192");
        const auto linear = glm::mat3(bake_matrix);
        const auto determinant = glm::determinant(linear);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-7f)
            throw std::runtime_error("GLB node transform has a degenerate linear matrix");
        const auto normal_matrix = glm::transpose(glm::inverse(linear));
        const bool reverse_winding = determinant < 0.0f;

        for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
            const auto& primitive = primitives.at(primitive_index);
            if (!primitive.is_object())
                throw std::runtime_error("GLB primitive entries must be objects");
            for (auto it = primitive.begin(); it != primitive.end(); ++it) {
                if (it.key() != "attributes" && it.key() != "indices" &&
                    it.key() != "material" && it.key() != "mode" &&
                    it.key() != "targets" && it.key() != "extensions" &&
                    it.key() != "extras")
                    throw std::runtime_error("GLB primitive property is not supported: " + it.key());
            }
            if (primitive.contains("targets"))
                throw std::runtime_error("GLB morph-target primitives are not supported by the static MeshLoader contract");
            if (primitive.contains("extensions"))
                throw std::runtime_error("GLB primitive extensions are not supported by MeshLoader V1");
            if (primitive.value("mode", 4) != 4) throw std::runtime_error("only TRIANGLES GLB primitives are supported");
            const auto& attributes = primitive.at("attributes");
            if (!attributes.is_object())
                throw std::runtime_error("GLB primitive attributes must be an object");
            if (!attributes.contains("POSITION")) throw std::runtime_error("GLB primitive has no POSITION");
            for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                if (it.key() != "POSITION" && it.key() != "NORMAL" && it.key() != "TEXCOORD_0")
                    throw std::runtime_error("GLB primitive attribute is not supported: " + it.key());
            }
            const int position_accessor = attributes.at("POSITION");
            const auto& position = data.document.at("accessors").at(position_accessor);
            if (position.contains("sparse"))
                throw std::runtime_error("sparse GLB accessors are not supported by MeshLoader V1");
            if (position.at("componentType") != 5126 || position.at("type") != "VEC3")
                throw std::runtime_error("POSITION must be a float VEC3 accessor");
            const auto vertex_count = position.at("count").get<std::size_t>();
            if (vertex_count == 0 || vertex_count > kMeshLoaderV1MaxVertices - total_vertices)
                throw std::runtime_error("GLB aggregate vertex count exceeds the MeshLoader V1 limit of 1000000");
            total_vertices += vertex_count;
            const auto instance_name = mesh_json.value("name", "mesh") + "/" + std::to_string(primitive_index);
            auto geometry = std::make_shared<Mesh>(instance_name);
            const auto normal_accessor = attributes.value("NORMAL", -1);
            const auto uv_accessor = attributes.value("TEXCOORD_0", -1);
            if (normal_accessor >= 0) {
                const auto& normal = data.document.at("accessors").at(normal_accessor);
                if (normal.contains("sparse"))
                    throw std::runtime_error("sparse GLB accessors are not supported by MeshLoader V1");
                if (normal.at("componentType") != 5126 || normal.at("type") != "VEC3"
                    || normal.at("count") != vertex_count)
                    throw std::runtime_error("NORMAL must match POSITION as float VEC3");
            }
            if (uv_accessor >= 0) {
                const auto& uv = data.document.at("accessors").at(uv_accessor);
                if (uv.contains("sparse"))
                    throw std::runtime_error("sparse GLB accessors are not supported by MeshLoader V1");
                if (uv.at("componentType") != 5126 || uv.at("type") != "VEC2"
                    || uv.at("count") != vertex_count)
                    throw std::runtime_error("TEXCOORD_0 must match POSITION as float VEC2");
            }
            for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
                const Vec3 source_position{
                    read_float_component(data.document, data.binary, position_accessor, vertex, 0),
                    read_float_component(data.document, data.binary, position_accessor, vertex, 1),
                    read_float_component(data.document, data.binary, position_accessor, vertex, 2)};
                if (!std::isfinite(source_position.x) || !std::isfinite(source_position.y) ||
                    !std::isfinite(source_position.z))
                    throw std::runtime_error("GLB POSITION values must be finite");
                Vec3 normal{};
                if (normal_accessor >= 0) {
                    const Vec3 source_normal{
                        read_float_component(data.document, data.binary, normal_accessor, vertex, 0),
                        read_float_component(data.document, data.binary, normal_accessor, vertex, 1),
                        read_float_component(data.document, data.binary, normal_accessor, vertex, 2)};
                    if (!std::isfinite(source_normal.x) || !std::isfinite(source_normal.y) ||
                        !std::isfinite(source_normal.z))
                        throw std::runtime_error("GLB NORMAL values must be finite");
                    const Vec3 transformed = normal_matrix * source_normal;
                    const f32 normal_length = glm::length(transformed);
                    if (!std::isfinite(normal_length))
                        throw std::runtime_error("GLB transformed NORMAL values must be finite");
                    if (normal_length > 1e-7f) normal = transformed / normal_length;
                }
                Vec2 uv{};
                if (uv_accessor >= 0) {
                    uv = {
                        read_float_component(data.document, data.binary, uv_accessor, vertex, 0),
                        read_float_component(data.document, data.binary, uv_accessor, vertex, 1)};
                    if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                        throw std::runtime_error("GLB TEXCOORD_0 values must be finite");
                }
                const Vec3 baked_position = Vec3{bake_matrix * Vec4(source_position, 1.0f)};
                if (!std::isfinite(baked_position.x) || !std::isfinite(baked_position.y) ||
                    !std::isfinite(baked_position.z))
                    throw std::runtime_error("GLB baked POSITION values must be finite");
                geometry->add_vertex(Vertex{baked_position, normal, uv});
            }
            if (!primitive.contains("indices")) throw std::runtime_error("GLB primitive has no indices");
            const int index_accessor = primitive.at("indices");
            const auto& indices = data.document.at("accessors").at(index_accessor);
            if (indices.contains("sparse"))
                throw std::runtime_error("sparse GLB accessors are not supported by MeshLoader V1");
            if (indices.at("type") != "SCALAR")
                throw std::runtime_error("GLB indices must use a SCALAR accessor");
            const auto index_count = indices.at("count").get<std::size_t>();
            if (index_count == 0 || index_count % 3 != 0)
                throw std::runtime_error("GLB triangle index count must be a non-zero multiple of three");
            if (index_count > kMeshLoaderV1MaxIndices - total_indices)
                throw std::runtime_error("GLB aggregate index count exceeds the MeshLoader V1 limit of 3000000");
            total_indices += index_count;
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
            ++total_parts;
        }
    }
    if (prepared->parts.empty()) throw std::runtime_error("GLB has no mesh primitives");
    return prepared;
}
