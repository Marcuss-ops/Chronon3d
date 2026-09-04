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
