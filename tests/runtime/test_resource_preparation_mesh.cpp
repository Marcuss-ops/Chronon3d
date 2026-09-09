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
    const std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":108}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":104,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"images":[{"bufferView":4,"mimeType":"image/png"}],"textures":[{"source":0}],"materials":[{"name":"red","pbrMetallicRoughness":{"baseColorFactor":[1.0,0.25,0.5,0.75],"baseColorTexture":{"index":0}}}],"meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}]})";
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
    bin.insert(bin.end(), {std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'}});

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
    REQUIRE(first.value()->parts[0].material_index.has_value());
    CHECK(*first.value()->parts[0].material_index == 0);
    REQUIRE(first.value()->images.size() == 1);
    CHECK(first.value()->images[0].mime_type == "image/png");
    CHECK(first.value()->images[0].payload.size() == 4);
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
