void decode_materials_and_images(const json& document,
                                 const std::vector<std::byte>& binary,
                                 PreparedMeshSource& prepared) {
    std::size_t decoded_rgba_total = 0;
    if (document.contains("textures") &&
        (!document.at("textures").is_array() ||
         document.at("textures").size() > kMeshLoaderV1MaxTextures))
        throw std::runtime_error("GLB texture count exceeds the MeshLoader V1 limit of 256");
    if (document.contains("images")) {
        const auto& images = document.at("images");
        if (!images.is_array() || images.size() > kMeshLoaderV1MaxImages)
            throw std::runtime_error("GLB image count exceeds the MeshLoader V1 limit of 256");
        prepared.images.reserve(images.size());
        std::size_t decoded_image_bytes = 0;
        for (const auto& image : images) {
            if (!image.is_object() || image.contains("extensions") || image.contains("uri"))
                throw std::runtime_error("GLB images must be embedded bufferView payloads without extensions");
            for (auto it = image.begin(); it != image.end(); ++it) {
                if (it.key() != "bufferView" && it.key() != "mimeType" &&
                    it.key() != "name" && it.key() != "extras")
                    throw std::runtime_error("GLB image property is not supported: " + it.key());
            }
            if (!image.contains("bufferView"))
                throw std::runtime_error("V1 embedded GLB image requires bufferView");
            const auto view_index = image.at("bufferView").get<int>();
            if (view_index < 0 || !document.contains("bufferViews") ||
                static_cast<std::size_t>(view_index) >= document.at("bufferViews").size())
                throw std::runtime_error("GLB image bufferView index is out of range");
            const auto byte_length = document.at("bufferViews").at(view_index)
                .at("byteLength").get<std::size_t>();
            if (byte_length > kMeshLoaderV1MaxGlbBytes - decoded_image_bytes)
                throw std::runtime_error("decoded GLB image payloads exceed the 64 MiB V1 limit");
            decoded_image_bytes += byte_length;
            const auto mime_type = image.value("mimeType", std::string{});
            if (mime_type != "image/png" && mime_type != "image/jpeg")
                throw std::runtime_error("GLB embedded images support only image/png and image/jpeg");
            prepared.images.push_back(MeshImage{
                .mime_type = mime_type,
                .payload = embedded_bytes(document, binary, view_index),
            });
        }
    }
    if (document.contains("textures")) {
        for (const auto& texture : document.at("textures")) {
            if (!texture.is_object() || texture.contains("sampler") || texture.contains("extensions"))
                throw std::runtime_error("GLB textures support embedded images with default sampling only");
            for (auto it = texture.begin(); it != texture.end(); ++it) {
                if (it.key() != "source" && it.key() != "name" && it.key() != "extras")
                    throw std::runtime_error("GLB texture property is not supported: " + it.key());
            }
            const auto source = texture.at("source").get<std::size_t>();
            if (source >= prepared.images.size())
                throw std::runtime_error("GLB texture source image index is out of range");
        }
    }
    if (document.contains("materials")) {
        const auto& materials = document.at("materials");
        if (!materials.is_array() || materials.size() > kMeshLoaderV1MaxMaterials)
            throw std::runtime_error("GLB material count exceeds the MeshLoader V1 limit of 4096");
        prepared.materials.reserve(materials.size());
        for (const auto& material : materials) {
            if (!material.is_object())
                throw std::runtime_error("GLB material entries must be objects");
            for (auto it = material.begin(); it != material.end(); ++it) {
                if (it.key() != "name" && it.key() != "pbrMetallicRoughness" &&
                    it.key() != "extras")
                    throw std::runtime_error("GLB material property is not supported: " + it.key());
            }
            for (const auto* key : {"normalTexture", "occlusionTexture", "emissiveTexture",
                                     "emissiveFactor", "alphaMode", "alphaCutoff",
                                     "doubleSided", "extensions"}) {
                if (material.contains(key))
                    throw std::runtime_error(std::string("GLB material property is not supported: ") + key);
            }
            MeshMaterial converted;
            converted.name = material.value("name", "");
            const auto& pbr = material.value("pbrMetallicRoughness", json::object());
            if (!pbr.is_object())
                throw std::runtime_error("GLB pbrMetallicRoughness must be an object");
            for (auto it = pbr.begin(); it != pbr.end(); ++it) {
                if (it.key() != "baseColorFactor" && it.key() != "baseColorTexture" &&
                    it.key() != "metallicFactor" && it.key() != "roughnessFactor" &&
                    it.key() != "metallicRoughnessTexture")
                    throw std::runtime_error("GLB PBR property is not supported: " + it.key());
            }
            if (pbr.contains("baseColorFactor")) {
                const auto& factor = pbr.at("baseColorFactor");
                if (!factor.is_array() || factor.size() != 4)
                    throw std::runtime_error("GLB baseColorFactor must contain four values");
                converted.base_color_factor = Color{
                    factor.at(0).get<f32>(), factor.at(1).get<f32>(),
                    factor.at(2).get<f32>(), factor.at(3).get<f32>()};
                for (const f32 channel : {converted.base_color_factor.r,
                                          converted.base_color_factor.g,
                                          converted.base_color_factor.b,
                                          converted.base_color_factor.a}) {
                    if (!std::isfinite(channel) || channel < 0.0f || channel > 1.0f)
                        throw std::runtime_error("GLB baseColorFactor channels must be finite in [0,1]");
                }
            }
            const auto read_pbr_factor = [&](const char* key, float fallback) {
                if (!pbr.contains(key)) return fallback;
                const float value = pbr.at(key).get<float>();
                if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                    throw std::runtime_error(std::string("GLB ") + key + " must be finite in [0,1]");
                return value;
            };
            converted.metallic_factor = read_pbr_factor("metallicFactor", 1.0f);
            converted.roughness_factor = read_pbr_factor("roughnessFactor", 1.0f);
            if (pbr.contains("baseColorTexture")) {
                const auto& texture_info = pbr.at("baseColorTexture");
                if (!texture_info.is_object() || texture_info.contains("extensions") ||
                    texture_info.value("texCoord", 0) != 0)
                    throw std::runtime_error("GLB baseColorTexture supports only TEXCOORD_0 without extensions");
                for (auto it = texture_info.begin(); it != texture_info.end(); ++it) {
                    if (it.key() != "index" && it.key() != "texCoord" && it.key() != "extras")
                        throw std::runtime_error("GLB baseColorTexture property is not supported: " + it.key());
                }
                const auto texture_index = texture_info.at("index").get<int>();
                if (texture_index < 0 || !document.contains("textures")
                    || static_cast<std::size_t>(texture_index) >= document.at("textures").size())
                    throw std::runtime_error("GLB baseColorTexture index is out of range");
                const auto image_index = document.at("textures").at(texture_index).at("source").get<int>();
                if (image_index < 0 || static_cast<std::size_t>(image_index) >= prepared.images.size())
                    throw std::runtime_error("GLB baseColorTexture source is out of range");
                converted.base_color_texture_index = static_cast<std::uint32_t>(image_index);
            }
            if (pbr.contains("metallicRoughnessTexture")) {
                const auto& texture_info = pbr.at("metallicRoughnessTexture");
                if (!texture_info.is_object() || texture_info.contains("extensions") ||
                    texture_info.value("texCoord", 0) != 0)
                    throw std::runtime_error("GLB metallicRoughnessTexture supports only TEXCOORD_0 without extensions");
                for (auto it = texture_info.begin(); it != texture_info.end(); ++it) {
                    if (it.key() != "index" && it.key() != "texCoord" && it.key() != "extras")
                        throw std::runtime_error("GLB metallicRoughnessTexture property is not supported: " + it.key());
                }
                const auto texture_index = texture_info.at("index").get<int>();
                if (texture_index < 0 || !document.contains("textures") ||
                    static_cast<std::size_t>(texture_index) >= document.at("textures").size())
                    throw std::runtime_error("GLB metallicRoughnessTexture index is out of range");
                const auto image_index = document.at("textures").at(texture_index).at("source").get<int>();
                if (image_index < 0 || static_cast<std::size_t>(image_index) >= prepared.images.size())
                    throw std::runtime_error("GLB metallicRoughnessTexture source is out of range");
                auto& image = prepared.images[static_cast<std::size_t>(image_index)];
                if (image.decoded_rgba.empty()) {
                    if (image.payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                        throw std::runtime_error("GLB metallic-roughness image exceeds decoder size limit");
                    int width = 0, height = 0, source_channels = 0;
                    auto* pixels = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(image.payload.data()),
                        static_cast<int>(image.payload.size()),
                        &width, &height, &source_channels, 4);
                    if (!pixels || width <= 0 || height <= 0) {
                        if (pixels) stbi_image_free(pixels);
                        throw std::runtime_error("GLB metallic-roughness image failed to decode");
                    }
                    const auto pixel_count = static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height);
                    if (pixel_count > kMeshLoaderV1MaxGlbBytes / 4 - decoded_rgba_total / 4) {
                        stbi_image_free(pixels);
                        throw std::runtime_error("decoded metallic-roughness images exceed the 64 MiB V1 limit");
                    }
                    const auto rgba_bytes = pixel_count * 4;
                    image.decoded_width = static_cast<std::uint32_t>(width);
                    image.decoded_height = static_cast<std::uint32_t>(height);
                    image.decoded_rgba.assign(pixels, pixels + rgba_bytes);
                    decoded_rgba_total += rgba_bytes;
                    stbi_image_free(pixels);
                }
                converted.metallic_roughness_texture_index = static_cast<std::uint32_t>(image_index);
            }
            prepared.materials.push_back(std::move(converted));
        }
    }
}
