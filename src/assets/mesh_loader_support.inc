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
