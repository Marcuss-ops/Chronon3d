// test_resource_preparation_mesh.cpp — Split from
// test_resource_preparation.cpp: MeshLoader GLB preparation, mesh
// preparation barrier and the disabled-feature diagnostics (guarded by
// CHRONON3D_ENABLE_MESH exactly as in the origin file).

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void append_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_f32(std::vector<std::byte>& bytes, float value) {
    std::uint32_t raw{};
    std::memcpy(&raw, &value, sizeof(raw));
    append_u32(bytes, raw);
}

std::filesystem::path write_triangle_glb() {
    const std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":193}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":104,"byteLength":89}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"images":[{"bufferView":4,"mimeType":"image/png"}],"textures":[{"source":0}],"materials":[{"name":"red","pbrMetallicRoughness":{"baseColorFactor":[1.0,0.25,0.5,0.75],"baseColorTexture":{"index":0},"metallicFactor":0.2,"roughnessFactor":0.35,"metallicRoughnessTexture":{"index":0}}}],"meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}]})";
    static constexpr std::uint8_t png_payload[] = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x08,0x08,0x02,0x00,0x00,0x00,0x4b,0x6d,0x29,0xdc,
        0x00,0x00,0x00,0x20,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0xfc,0xcf,0x80,0x00,0x8c,0x48,
        0x1c,0x26,0x06,0x1c,0x80,0x74,0x09,0x46,0x06,0x06,0x84,0xc1,0xff,0xff,0x33,0xd2,0xc2,
        0x0e,0x00,0x94,0x43,0x05,0x0c,0x42,0x9e,0x5e,0x54,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
        0x44,0xae,0x42,0x60,0x82};
    std::vector<std::byte> bin;
    for (const auto& point : std::array<std::array<float, 3>, 3>{{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}}})
        for (float component : point) append_f32(bin, component);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) append_f32(bin, j == 2 ? 1.0f : 0.0f);
    for (const auto& uv : std::array<std::array<float, 2>, 3>{{{{0, 0}}, {{1, 0}}, {{0, 1}}}})
        for (float component : uv) append_f32(bin, component);
    append_u32(bin, 0); // overwritten below with compact u16 indices
    bin.resize(96);
    for (std::uint16_t index : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}}) {
        bin.push_back(static_cast<std::byte>(index & 0xffU));
        bin.push_back(static_cast<std::byte>((index >> 8U) & 0xffU));
    }
    bin.resize(104, std::byte{0});
    for (const auto byte : png_payload) bin.push_back(static_cast<std::byte>(byte));
    bin.resize(196, std::byte{0});

    std::vector<std::byte> file;
    append_u32(file, 0x46546C67U); append_u32(file, 2U);
    const auto json_length = static_cast<std::uint32_t>((json.size() + 3U) & ~3U);
    const auto total_length = 12U + 8U + json_length + 8U + static_cast<std::uint32_t>(bin.size());
    append_u32(file, total_length);
    append_u32(file, json_length); append_u32(file, 0x4E4F534AU);
    file.insert(file.end(), reinterpret_cast<const std::byte*>(json.data()),
                reinterpret_cast<const std::byte*>(json.data() + json.size()));
    file.resize(file.size() + (json_length - json.size()), std::byte{' '});
    append_u32(file, static_cast<std::uint32_t>(bin.size())); append_u32(file, 0x004E4942U);
    file.insert(file.end(), bin.begin(), bin.end());

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("chronon3d-preparation-triangle-" + std::to_string(unique) + ".glb");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) throw std::runtime_error("could not create GLB fixture");
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!output.good()) throw std::runtime_error("could not write GLB fixture");
    return path;
}

std::filesystem::path write_node_transform_glb() {
    const std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"translation":[10.0,20.0,30.0],"children":[1]},{"scale":[-2.0,4.0,8.0],"mesh":0}],"buffers":[{"byteLength":80}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"name":"hierarchy-triangle","primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}]})";
    std::vector<std::byte> bin;
    for (const auto& point : std::array<std::array<float, 3>, 3>{{{{1, 2, 3}}, {{2, 2, 3}}, {{1, 3, 3}}}})
        for (float component : point) append_f32(bin, component);
    for (int i = 0; i < 3; ++i)
        for (float component : {1.0f, 1.0f, 1.0f}) append_f32(bin, component);
    for (std::uint16_t index : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}}) {
        bin.push_back(static_cast<std::byte>(index & 0xffU));
        bin.push_back(static_cast<std::byte>((index >> 8U) & 0xffU));
    }
    bin.resize(80, std::byte{0});

    std::vector<std::byte> file;
    append_u32(file, 0x46546C67U); append_u32(file, 2U);
    const auto json_length = static_cast<std::uint32_t>((json.size() + 3U) & ~3U);
    const auto total_length = 12U + 8U + json_length + 8U + static_cast<std::uint32_t>(bin.size());
    append_u32(file, total_length);
    append_u32(file, json_length); append_u32(file, 0x4E4F534AU);
    file.insert(file.end(), reinterpret_cast<const std::byte*>(json.data()),
                reinterpret_cast<const std::byte*>(json.data() + json.size()));
    file.resize(file.size() + (json_length - json.size()), std::byte{' '});
    append_u32(file, static_cast<std::uint32_t>(bin.size())); append_u32(file, 0x004E4942U);
    file.insert(file.end(), bin.begin(), bin.end());

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("chronon3d-preparation-node-transform-" + std::to_string(unique) + ".glb");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) throw std::runtime_error("could not create node transform GLB fixture");
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!output.good()) throw std::runtime_error("could not write node transform GLB fixture");
    return path;
}

std::filesystem::path write_glb_document_fixture(std::string json,
                                                  std::string_view label) {
    while (json.size() % 4 != 0) json.push_back(' ');
    std::vector<std::byte> file;
    const auto total_length = static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + 4);
    append_u32(file, 0x46546C67U);
    append_u32(file, 2U);
    append_u32(file, total_length);
    append_u32(file, static_cast<std::uint32_t>(json.size()));
    append_u32(file, 0x4E4F534AU);
    file.insert(file.end(), reinterpret_cast<const std::byte*>(json.data()),
                reinterpret_cast<const std::byte*>(json.data() + json.size()));
    append_u32(file, 4U);
    append_u32(file, 0x004E4942U);
    file.insert(file.end(), 4, std::byte{0});

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("chronon3d-preparation-" + std::string(label) + "-" +
         std::to_string(unique) + ".glb");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) throw std::runtime_error("could not create GLB fixture");
    output.write(reinterpret_cast<const char*>(file.data()),
                 static_cast<std::streamsize>(file.size()));
    if (!output.good()) throw std::runtime_error("could not write GLB fixture");
    return path;
}

} // namespace

#ifdef CHRONON3D_ENABLE_MESH
TEST_CASE("MeshLoader prepares a self-contained GLB and reuses identity cache") {
    const auto path = write_triangle_glb();
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(path.parent_path());
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, path.filename().string(), "mesh/triangle", true};
    chronon3d::assets::MeshPreparationCache cache;

    const auto first = chronon3d::assets::MeshLoader::load(ref, resolver, &cache);
    REQUIRE(first.has_value());
    REQUIRE(first.value()->parts.size() == 1);
    REQUIRE(first.value()->parts[0].geometry->vertices().size() == 3);
    REQUIRE(first.value()->parts[0].geometry->indices().size() == 3);
    CHECK(first.value()->parts[0].geometry->vertices()[1].normal == (chronon3d::Vec3{0.0f, 0.0f, 1.0f}));
    CHECK(first.value()->parts[0].geometry->vertices()[2].uv == (chronon3d::Vec2{0.0f, 1.0f}));
    REQUIRE(first.value()->materials.size() == 1);
    CHECK(first.value()->materials[0].name == "red");
    CHECK(first.value()->materials[0].base_color_factor == (chronon3d::Color{1.0f, 0.25f, 0.5f, 0.75f}));
    REQUIRE(first.value()->materials[0].base_color_texture_index.has_value());
    CHECK(*first.value()->materials[0].base_color_texture_index == 0);
    CHECK(first.value()->materials[0].metallic_factor == doctest::Approx(0.2f));
    CHECK(first.value()->materials[0].roughness_factor == doctest::Approx(0.35f));
    REQUIRE(first.value()->materials[0].metallic_roughness_texture_index.has_value());
    CHECK(*first.value()->materials[0].metallic_roughness_texture_index == 0);
    REQUIRE(first.value()->parts[0].material_index.has_value());
    CHECK(*first.value()->parts[0].material_index == 0);
    REQUIRE(first.value()->images.size() == 1);
    CHECK(first.value()->images[0].mime_type == "image/png");
    CHECK(first.value()->images[0].payload.size() == 89);
    CHECK(first.value()->images[0].decoded_width == 8);
    CHECK(first.value()->images[0].decoded_height == 8);
    CHECK(first.value()->images[0].decoded_rgba.size() == 8 * 8 * 4);
    CHECK(cache.size() == 1);

    const auto second = chronon3d::assets::MeshLoader::load(ref, resolver, &cache);
    REQUIRE(second.has_value());
    CHECK(second.value().get() == first.value().get());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("MeshLoader bakes GLB node hierarchy, inverse-transpose normals, bounds, and winding") {
    const auto path = write_node_transform_glb();
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(path.parent_path());
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, path.filename().string(), "mesh/node-transform", true};

    const auto result = chronon3d::assets::MeshLoader::load(ref, resolver);
    REQUIRE(result.has_value());
    REQUIRE(result.value()->parts.size() == 1);
    const auto& geometry = *result.value()->parts[0].geometry;
    REQUIRE(geometry.vertices().size() == 3);
    REQUIRE(geometry.indices().size() == 3);

    CHECK(geometry.vertices()[0].position == (chronon3d::Vec3{8.0f, 28.0f, 54.0f}));
    CHECK(geometry.vertices()[1].position == (chronon3d::Vec3{6.0f, 28.0f, 54.0f}));
    CHECK(geometry.vertices()[2].position == (chronon3d::Vec3{8.0f, 32.0f, 54.0f}));
    CHECK(geometry.bounds().min == (chronon3d::Vec3{6.0f, 28.0f, 54.0f}));
    CHECK(geometry.bounds().max == (chronon3d::Vec3{8.0f, 32.0f, 54.0f}));
    CHECK(geometry.indices()[0] == 0);
    CHECK(geometry.indices()[1] == 2);
    CHECK(geometry.indices()[2] == 1);

    const auto normal_length = std::sqrt(0.5f * 0.5f + 0.25f * 0.25f + 0.125f * 0.125f);
    CHECK(geometry.vertices()[0].normal.x == doctest::Approx(-0.5f / normal_length));
    CHECK(geometry.vertices()[0].normal.y == doctest::Approx(0.25f / normal_length));
    CHECK(geometry.vertices()[0].normal.z == doctest::Approx(0.125f / normal_length));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("MeshLoader rejects a primitive material index outside the prepared materials") {
    const auto path = write_triangle_glb();
    std::ifstream input(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string needle = R"("material":0)";
    const auto position = bytes.find(needle);
    REQUIRE(position != std::string::npos);
    bytes.replace(position, needle.size(), R"("material":3)");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
    output.close();

    chronon3d::assets::AssetResolver resolver;
    resolver.mount(path.parent_path());
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, path.filename().string(), "mesh/invalid-material", true};
    const auto result = chronon3d::assets::MeshLoader::load(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::assets::MeshLoadErrorCode::InvalidGeometry);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("MeshLoader rejects animated and skinned GLB data instead of silently treating it as static") {
    for (const auto& unsupported_member : {std::string{"\"animations\":[{}],"},
                                           std::string{"\"skins\":[{}],"}}) {
        const auto path = write_triangle_glb();
        std::ifstream input(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const auto insert_at = bytes.find("\"buffers\"");
        REQUIRE(insert_at != std::string::npos);
        bytes.insert(insert_at, unsupported_member);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output.good());
        output.close();

        chronon3d::assets::AssetResolver resolver;
        resolver.mount(path.parent_path());
        const chronon3d::assets::InternalAssetRef ref{
            chronon3d::assets::AssetKind::Mesh, path.filename().string(), "mesh/unsupported-motion", true};
        const auto result = chronon3d::assets::MeshLoader::load(ref, resolver);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == chronon3d::assets::MeshLoadErrorCode::InvalidGeometry);
        CHECK(result.error().message.find("static MeshLoader contract") != std::string::npos);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
}

TEST_CASE("MeshLoader rejects GLB input above its byte budget before hashing or parsing") {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("chronon3d-preparation-oversized-" + std::to_string(unique) + ".glb");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output.seekp(static_cast<std::streamoff>(chronon3d::assets::kMeshLoaderV1MaxGlbBytes));
        output.put('\0');
        REQUIRE(output.good());
    }
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(path.parent_path());
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, path.filename().string(), "mesh/oversized", true};
    const auto result = chronon3d::assets::MeshLoader::load(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::assets::MeshLoadErrorCode::UnsupportedGlb);
    CHECK(result.error().message.find("64 MiB") != std::string::npos);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("MeshLoader bounds GLB JSON expansion and scene node arrays") {
    const auto load = [](const std::filesystem::path& path, std::string logical) {
        chronon3d::assets::AssetResolver resolver;
        resolver.mount(path.parent_path());
        const chronon3d::assets::InternalAssetRef ref{
            chronon3d::assets::AssetKind::Mesh, path.filename().string(),
            std::move(logical), true};
        return chronon3d::assets::MeshLoader::load(ref, resolver);
    };

    const auto oversized_json = write_glb_document_fixture(
        std::string(static_cast<std::size_t>(
            chronon3d::assets::kMeshLoaderV1MaxJsonBytes + 4), ' '), "large-json");
    const auto json_result = load(oversized_json, "mesh/large-json");
    REQUIRE_FALSE(json_result.has_value());
    CHECK(json_result.error().message.find("16 MiB") != std::string::npos);
    std::error_code ignored;
    std::filesystem::remove(oversized_json, ignored);

    std::string node_document =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[]}],"nodes":[)";
    for (std::size_t i = 0; i <= chronon3d::assets::kMeshLoaderV1MaxNodes; ++i) {
        if (i != 0) node_document.push_back(',');
        node_document += "{}";
    }
    node_document += R"(],"meshes":[],"buffers":[{"byteLength":4}]})";
    const auto oversized_nodes = write_glb_document_fixture(
        std::move(node_document), "large-node-array");
    const auto node_result = load(oversized_nodes, "mesh/large-node-array");
    REQUIRE_FALSE(node_result.has_value());
    CHECK(node_result.error().message.find("node count exceeds") != std::string::npos);
    std::filesystem::remove(oversized_nodes, ignored);

    const auto oversized_vertices = write_glb_document_fixture(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4},{"buffer":0,"byteOffset":0,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5126,"count":1000001,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})",
        "large-vertex-count");
    const auto vertex_result = load(oversized_vertices, "mesh/large-vertex-count");
    REQUIRE_FALSE(vertex_result.has_value());
    CHECK(vertex_result.error().message.find("aggregate vertex count exceeds") != std::string::npos);
    std::filesystem::remove(oversized_vertices, ignored);

    const auto reject_document = [&](std::string document, std::string label,
                                     std::string_view expected_error) {
        const auto path = write_glb_document_fixture(std::move(document), label);
        const auto result = load(path, "mesh/" + label);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find(expected_error) != std::string::npos);
        std::filesystem::remove(path, ignored);
    };
    reject_document(
        R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_draco_mesh_compression"],"meshes":[],"buffers":[{"byteLength":4}]})",
        "extension-contract", "extensions are not supported");
    reject_document(
        R"({"asset":{"version":"2.0"},"meshes":[],"buffers":[{"byteLength":4,"uri":"mesh.bin"}]})",
        "external-buffer", "external GLB buffers");
    reject_document(
        R"({"asset":{"version":"2.0"},"materials":[{"pbrMetallicRoughness":{"emissiveFactor":[1,1,1]}}],"meshes":[],"buffers":[{"byteLength":4}]})",
        "material-contract", "PBR property is not supported");
    reject_document(
        R"({"asset":{"version":"2.0"},"materials":[{"pbrMetallicRoughness":{"roughnessFactor":1.2}}],"meshes":[],"buffers":[{"byteLength":4}]})",
        "roughness-factor-range", "must be finite in [0,1]");
    reject_document(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4}],"images":[{"bufferView":0,"mimeType":"image/webp"}],"meshes":[]})",
        "image-mime-contract", "only image/png and image/jpeg");
    reject_document(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TANGENT":0},"indices":0}]}]})",
        "attribute-contract", "primitive attribute is not supported");
    reject_document(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","sparse":{}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":0}]}]})",
        "sparse-accessor", "sparse GLB accessors are not supported");
}

TEST_CASE("ResourcePreparation::prepare loads GLB into PreparedAssets and reuses cache") {
    const auto path = write_triangle_glb();
    chronon3d::assets::AssetManifest manifest;
    manifest.add_mesh(path.filename().string(), "mesh/prepared");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(path.parent_path());
    chronon3d::assets::MeshPreparationCache cache;
    chronon3d::runtime::PreparationOptions options;
    options.prepare_fonts = false;
    options.prepare_images = false;
    options.prepare_video_metadata = false;
    options.prepare_audio_index = false;
    options.prepare_layouts = false;
    options.mesh_cache = &cache;

    const auto first = chronon3d::runtime::ResourcePreparation::prepare(manifest, resolver, options);
    REQUIRE(first.has_value());
    REQUIRE(first.value().meshes.count("mesh/prepared") == 1);
    REQUIRE(first.value().meshes.at("mesh/prepared").source != nullptr);
    CHECK(first.value().diagnostics.meshes_prepared == 1);

    const auto second = chronon3d::runtime::ResourcePreparation::prepare(manifest, resolver, options);
    REQUIRE(second.has_value());
    CHECK(second.value().meshes.at("mesh/prepared").source.get()
          == first.value().meshes.at("mesh/prepared").source.get());
    CHECK(cache.size() == 1);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("ResourcePreparation::prepare_mesh — invalid MeshRef is explicit") {
    chronon3d::assets::AssetResolver resolver;
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, "", "mesh/empty-reference", true};

    const auto result = chronon3d::runtime::ResourcePreparation::prepare_mesh(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::runtime::PreparationError::Code::UnresolvableAssetPath);
    CHECK(result.error().phase == "mesh");
    CHECK(result.error().path.empty());
    CHECK(result.error().owner == "mesh/empty-reference");
    CHECK(result.error().message.find("empty") != std::string::npos);
}

TEST_CASE("ResourcePreparation::prepare_mesh — .gltf is explicitly unsupported") {
    const auto temp_root = std::filesystem::temp_directory_path() / "chronon3d-preparation-gltf";
    std::error_code ignored;
    std::filesystem::create_directories(temp_root, ignored);
    std::ofstream fixture(temp_root / "phone.gltf");
    fixture << "{}";
    fixture.close();
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(temp_root);
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, "phone.gltf", "mesh/gltf", true};

    const auto result = chronon3d::runtime::ResourcePreparation::prepare_mesh(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::runtime::PreparationError::Code::PreflightFailed);
    CHECK(result.error().phase == "mesh");
    CHECK(result.error().cause_code == "UNSUPPORTED_GLB");
    CHECK(result.error().message.find(".glb") != std::string::npos);
    std::filesystem::remove_all(temp_root, ignored);
}

TEST_CASE("ResourcePreparation::prepare_mesh — missing GLB preserves MissingAsset") {
    chronon3d::assets::AssetResolver resolver;
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, "missing/triangle.glb", "mesh/missing-direct", true};

    const auto result = chronon3d::runtime::ResourcePreparation::prepare_mesh(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::runtime::PreparationError::Code::MissingAsset);
    CHECK(result.error().phase == "mesh");
    CHECK(result.error().message.find("not found") != std::string::npos);
}

TEST_CASE("ResourcePreparation::prepare — required corrupted mesh fails loud under WarnAndSkip (AUDIT N-03)") {
    // A truncated GLB fails MeshLoader with CorruptedAsset. Under WarnAndSkip
    // this used to be swallowed into diagnostics.warnings (silent partial
    // snapshot → black frames); it must abort preparation like the missing/
    // unresolvable cases.
    chronon3d::assets::AssetManifest manifest;
    manifest.add_mesh("broken/triangle.glb", "mesh/broken");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(std::filesystem::temp_directory_path());

    // Write a truncated (non-GLB) file at the resolved path under the
    // mounted temp root (resolver joins mount point + relative path).
    const auto broken_dir = std::filesystem::temp_directory_path() / "broken";
    std::error_code ignored;
    std::filesystem::create_directories(broken_dir, ignored);
    {
        std::ofstream fixture(broken_dir / "triangle.glb", std::ios::binary | std::ios::trunc);
        fixture << "glTFnot-a-real-binary-chunk";
    }

    chronon3d::runtime::PreparationOptions options;
    options.failure_mode =
        chronon3d::runtime::PreparationOptions::FailureMode::WarnAndSkip;
    options.prepare_fonts = false;
    options.prepare_images = false;
    options.prepare_video_metadata = false;
    options.prepare_audio_index = false;
    options.prepare_layouts = false;

    const auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::runtime::PreparationError::Code::CorruptedAsset);
    CHECK(result.error().phase == "mesh");

    std::filesystem::remove_all(broken_dir, ignored);
}

TEST_CASE("ResourcePreparation::prepare — optional corrupted mesh degrades to warning under WarnAndSkip") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_mesh("broken/triangle.glb", "mesh/broken-optional", /*required=*/false);
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(std::filesystem::temp_directory_path());

    const auto broken_dir = std::filesystem::temp_directory_path() / "broken";
    std::error_code ignored;
    std::filesystem::create_directories(broken_dir, ignored);
    {
        std::ofstream fixture(broken_dir / "triangle.glb", std::ios::binary | std::ios::trunc);
        fixture << "glTFnot-a-real-binary-chunk";
    }

    chronon3d::runtime::PreparationOptions options;
    options.failure_mode =
        chronon3d::runtime::PreparationOptions::FailureMode::WarnAndSkip;
    options.prepare_fonts = false;
    options.prepare_images = false;
    options.prepare_video_metadata = false;
    options.prepare_audio_index = false;
    options.prepare_layouts = false;

    const auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(result.has_value());
    CHECK(result.value().meshes.empty());
    REQUIRE(result.value().diagnostics.warnings.size() == 1);
    CHECK(result.value().diagnostics.warnings[0].code ==
          chronon3d::runtime::PreparationError::Code::CorruptedAsset);
    CHECK(result.value().diagnostics.warnings[0].phase == "mesh");

    std::filesystem::remove_all(broken_dir, ignored);
}

TEST_CASE("ResourcePreparation::prepare — mesh missing GLB fails loud") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_mesh("missing/triangle.glb", "mesh/missing");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(std::filesystem::temp_directory_path());

    chronon3d::runtime::PreparationOptions options;
    options.prepare_fonts = false;
    options.prepare_images = false;
    options.prepare_video_metadata = false;
    options.prepare_audio_index = false;
    options.prepare_layouts = false;

    const auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::runtime::PreparationError::Code::MissingAsset);
    CHECK(result.error().phase == "mesh");
    CHECK(result.error().owner == "mesh/missing");
}
#else
TEST_CASE("MeshLoader reports the explicit disabled-feature diagnostic") {
    chronon3d::assets::AssetResolver resolver;
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, "models/triangle.glb", "mesh/off", true};

    const auto result = chronon3d::assets::MeshLoader::load(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::assets::MeshLoadErrorCode::UnsupportedGlb);
    CHECK(result.error().message ==
          "Mesh support is disabled (CHRONON3D_ENABLE_MESH=OFF)");
}

TEST_CASE("ResourcePreparation::prepare_mesh preserves the disabled-feature diagnostic") {
    chronon3d::assets::AssetResolver resolver;
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh, "models/triangle.glb", "mesh/off", true};

    const auto result = chronon3d::runtime::ResourcePreparation::prepare_mesh(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::runtime::PreparationError::Code::PreflightFailed);
    CHECK(result.error().phase == "mesh");
    CHECK(result.error().cause_code == "UNSUPPORTED_GLB");
    CHECK(result.error().message.find("disabled") != std::string::npos);
}
#endif
