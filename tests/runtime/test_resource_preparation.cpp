// ═══════════════════════════════════════════════════════════════════════════
// tests/runtime/test_resource_preparation.cpp — TICKET-ASSET-PREP-BARRIER
//
// Locks the canonical 5-phase resource-preparation barrier contract:
//
//   1. Missing required asset → fail-loud with structured
//      `PreparationError{ .code = MissingAsset, ... }` BEFORE encoder.
//   2. All assets present → return `PreparedAssets` with 5 keyed maps.
//   3. Empty manifest → empty PreparedAssets (default-options).
//   4. `WarnAndSkip` policy → diagnostics.warnings populated per asset;
//      PreparedAssets still returned (no abort).
//   5. Per-phase opt-out → matching phase map is empty after preparation.
//   6. `UnresolvableAssetPath` (empty path) → fail-loud error code.
//   7. Each per-phase loader function (font/image/video/audio/layout)
//      returns the right structured error on a missing resolver hit.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/test_utils.hpp>

#include <array>
#include <chrono>
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
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace {

// ── Mock AssetResolver — resolves only the asset paths in `present_set` ──
//
// Default constructor returns a resolver that has NO assets present (all
// `resolve()` calls return std::nullopt → fail-loud path). Tests construct
// with a string set to opt-in to specific paths.
class MockResolver : public chronon3d::assets::AssetResolver {
public:
    MockResolver() = default;
    explicit MockResolver(std::unordered_set<std::string> present)
        : m_present(std::move(present)) {}

    [[nodiscard]] std::optional<std::filesystem::path>
    resolve(const std::filesystem::path& path) const override {
        const auto it = m_present.find(path);
        if (it == m_present.end()) return std::nullopt;
        return std::filesystem::path{*it};
    }

private:
    std::unordered_set<std::string> m_present;
};

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
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

TEST_CASE("ResourcePreparation::prepare — empty manifest → empty PreparedAssets (default policy)") {
    chronon3d::assets::AssetManifest manifest;
    MockResolver                    resolver;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver);

    CHECK(result.has_value());
    if (!result.has_value()) return;
    CHECK(result.value().empty());
    CHECK(result.value().diagnostics.warnings.empty());
    CHECK(result.value().diagnostics.fonts_loaded == 0);
    CHECK(result.value().diagnostics.images_decoded == 0);
}

TEST_CASE("ResourcePreparation::prepare — missing required font → fail-loud with structured error") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("missing/font.ttf", "owner/font/missing");

    // Resolver has NO assets present.
    MockResolver                    resolver;
    chronon3d::runtime::PreparationOptions options;  // defaults: FailLoud + all phases ON

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code ==
          chronon3d::runtime::PreparationError::Code::MissingAsset);
    CHECK(result.error().path == "missing/font.ttf");
    CHECK(result.error().owner == "owner/font/missing");
    CHECK(result.error().phase == "font");
    CHECK(!result.error().message.empty());
}

TEST_CASE("ResourcePreparation::prepare — all assets present → success, 4 keyed maps populated") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font.ttf",         "owner/font");
    manifest.add_image("present/image.png",       "owner/image");
    manifest.add_video("present/video.mp4",       "owner/video");
    manifest.add_audio("present/audio.wav",       "owner/audio");

    MockResolver resolver(std::unordered_set<std::string>{
        "present/font.ttf", "present/image.png",
        "present/video.mp4", "present/audio.wav"
    });
    chronon3d::runtime::PreparationOptions options;
    // layout_preparation is keyed by Image kind (placeholder); opt it OUT
    // for this test so we don't double-count images via layout phase.
    options.prepare_layouts = false;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code ==
          chronon3d::runtime::PreparationError::Code::InternalError);
    CHECK(result.error().phase == "video");
    return;
#else
    REQUIRE(result.has_value());
    CHECK(result.value().fonts.count("owner/font")   == 1);
    CHECK(result.value().images.count("owner/image") == 1);
    CHECK(result.value().video_metadata.count("owner/video") == 1);
    CHECK(result.value().audio_index.count("owner/audio")    == 1);
    CHECK(result.value().diagnostics.warnings.empty());
    CHECK(result.value().diagnostics.fonts_loaded   == 1);
    CHECK(result.value().diagnostics.images_decoded == 1);
    CHECK(result.value().diagnostics.video_metadata_probed == 1);
    CHECK(result.value().diagnostics.audio_indexes_built    == 1);
    CHECK(result.value().diagnostics.layouts_prepared       == 0);
#endif
}

TEST_CASE("ResourcePreparation::prepare — WarnAndSkip policy → diagnostics.warnings populated, no abort") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("missing/font.ttf", "owner/font/missing");

    MockResolver                    resolver;  // nothing present
    chronon3d::runtime::PreparationOptions options;
    options.failure_mode =
        chronon3d::runtime::PreparationOptions::FailureMode::WarnAndSkip;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(result.has_value());
    CHECK(result.value().fonts.empty());
    CHECK(result.value().diagnostics.warnings.size() == 1);
    CHECK(result.value().diagnostics.warnings[0].code ==
          chronon3d::runtime::PreparationError::Code::MissingAsset);
    CHECK(result.value().diagnostics.warnings[0].phase == "font");
}

TEST_CASE("ResourcePreparation::prepare — empty path → UnresolvableAssetPath fail-loud") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("", "owner/font/empty");
    MockResolver                    resolver;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code ==
          chronon3d::runtime::PreparationError::Code::UnresolvableAssetPath);
    CHECK(result.error().path.empty());
    CHECK(result.error().owner == "owner/font/empty");
    CHECK(result.error().phase == "font");
}

TEST_CASE("ResourcePreparation::prepare — per-phase opt-out disables mapping") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font.ttf",   "owner/font");
    manifest.add_image("present/image.png", "owner/image");

    MockResolver resolver(std::unordered_set<std::string>{
        "present/font.ttf", "present/image.png"
    });
    chronon3d::runtime::PreparationOptions options;
    options.prepare_fonts  = false;  // skip phase 1
    options.prepare_images = false;  // skip phase 2

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(result.has_value());
    CHECK(result.value().fonts.empty());   // phase disabled
    CHECK(result.value().images.empty());  // phase disabled
    CHECK(result.value().diagnostics.warnings.empty());
    CHECK(result.value().diagnostics.fonts_loaded   == 0);
    CHECK(result.value().diagnostics.images_decoded == 0);
}

TEST_CASE("ResourcePreparation — per-phase loader functions lock structured errors") {
    MockResolver resolver;  // nothing present

    const chronon3d::assets::InternalAssetRef bad_font{
        chronon3d::assets::AssetKind::Font,
        "missing/font.ttf",
        "owner/missing",
        /*required=*/true
    };

    SUBCASE("load_font returns MissingAsset") {
        auto r = chronon3d::runtime::ResourcePreparation::load_font(bad_font, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code ==
              chronon3d::runtime::PreparationError::Code::MissingAsset);
        CHECK(r.error().phase == "font");
    }

    SUBCASE("decode_image returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_image{
            chronon3d::assets::AssetKind::Image, "missing/image.png", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::decode_image(bad_image, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "image");
    }

    SUBCASE("probe_video_metadata returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_video{
            chronon3d::assets::AssetKind::Video, "missing/video.mp4", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::probe_video_metadata(
            bad_video, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "video");
    }

    SUBCASE("build_audio_index returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_audio{
            chronon3d::assets::AssetKind::Audio, "missing/audio.wav", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::build_audio_index(
            bad_audio, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "audio");
    }

    SUBCASE("prepare_layout returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_layout{
            chronon3d::assets::AssetKind::Image, "missing/layout.png", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::prepare_layout(
            bad_layout, resolver, chronon3d::Frame{0});
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "layout");
    }
}

TEST_CASE("ResourcePreparation::prepare is idempotent") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font.ttf", "owner/font");

    MockResolver resolver(std::unordered_set<std::string>{"present/font.ttf"});
    chronon3d::runtime::PreparationOptions options;
    options.prepare_layouts = false;

    const auto first = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);
    const auto second = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first.value().fonts.size() == second.value().fonts.size());
    CHECK(first.value().fonts.at("owner/font").path ==
          second.value().fonts.at("owner/font").path);
    CHECK(first.value().diagnostics.fonts_loaded ==
          second.value().diagnostics.fonts_loaded);
    CHECK(first.value().diagnostics.warnings.size() ==
          second.value().diagnostics.warnings.size());
}

TEST_CASE("prepare_render promotes preflight failures to structured errors") {
    auto renderer = chronon3d::test::make_renderer_shared();
    const chronon3d::Composition missing_asset(
        chronon3d::CompositionSpec{
            .name = "missing-asset",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx);
            scene.layer("missing-image", [](chronon3d::LayerBuilder& layer) {
                layer.image("image", {
                    .asset_path = "assets/missing.png",
                    .size = {64.0f, 64.0f},
                });
            });
            return scene.build();
        });

    const auto result = chronon3d::runtime::prepare_render(
        renderer.get(), missing_asset,
        chronon3d::runtime::RenderPreparationOptions{
            .warmup_renderer = false,
        });

    CHECK_FALSE(result.ok());
    REQUIRE(result.preparation_error.has_value());
    CHECK(result.preparation_error->code ==
          chronon3d::runtime::PreparationError::Code::PreflightFailed);
    CHECK(result.preparation_error->phase == "preflight");
    CHECK(result.preparation_error->path == "assets/missing.png");
    CHECK(result.preparation_error->owner == "missing-image/image");
    CHECK(result.preparation_error->cause_code == "ASSET_NOT_FOUND");
    CHECK(result.preparation_error->message.find("Asset not found") != std::string::npos);
    CHECK(result.prepared_assets == std::nullopt);
    CHECK(result.diagnostic().find("ASSET_NOT_FOUND") != std::string::npos);
    CHECK(result.diagnostic().find("assets/missing.png") != std::string::npos);
}

TEST_CASE("prepare_render recovers after a preflight failure") {
    auto renderer = chronon3d::test::make_renderer_shared();
    const chronon3d::Composition missing_asset(
        chronon3d::CompositionSpec{
            .name = "missing-asset",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx);
            scene.layer("missing-image", [](chronon3d::LayerBuilder& layer) {
                layer.image("image", {
                    .asset_path = "assets/missing.png",
                    .size = {64.0f, 64.0f},
                });
            });
            return scene.build();
        });
    const chronon3d::Composition valid(
        chronon3d::CompositionSpec{
            .name = "valid-color",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx);
            scene.layer("color", [](chronon3d::LayerBuilder& layer) {
                layer.rect("background", {
                    .size = {64.0f, 64.0f},
                    .color = chronon3d::Color::white(),
                });
            });
            return scene.build();
        });

    const auto failed = chronon3d::runtime::prepare_render(
        renderer.get(), missing_asset,
        chronon3d::runtime::RenderPreparationOptions{.warmup_renderer = false});
    REQUIRE_FALSE(failed.ok());
    REQUIRE(failed.preparation_error.has_value());
    CHECK(failed.preparation_error->code ==
          chronon3d::runtime::PreparationError::Code::PreflightFailed);

    const auto recovered = chronon3d::runtime::prepare_render(
        renderer.get(), valid,
        chronon3d::runtime::RenderPreparationOptions{.warmup_renderer = false});
    CHECK(recovered.ok());
    CHECK_FALSE(recovered.preparation_error.has_value());
    CHECK(recovered.prepared_assets.has_value());
}

TEST_CASE("prepare_render rejects a missing renderer before execution") {
    const chronon3d::Composition composition(
        chronon3d::CompositionSpec{
            .name = "preparation-test",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext&) {
            return chronon3d::Scene{};
        });

    const auto result = chronon3d::runtime::prepare_render(
        nullptr, composition,
        chronon3d::runtime::RenderPreparationOptions{
            .warmup_renderer = false,
        });

    CHECK_FALSE(result.ok());
    REQUIRE(result.preparation_error.has_value());
    CHECK(result.preparation_error->code ==
          chronon3d::runtime::PreparationError::Code::InternalError);
    CHECK(result.preparation_error->phase == "setup");
}

TEST_CASE("ResourcePreparation::prepare — keyed-by-owner maps (1 owner → 1 entry even with duplicates)") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font1.ttf", "owner/font");
    manifest.add_font("present/font1.ttf", "owner/font");  // duplicate owner
    manifest.add_font("present/font1.ttf", "owner/font");  // duplicate owner

    MockResolver resolver(std::unordered_set<std::string>{"present/font1.ttf"});
    chronon3d::runtime::PreparationOptions options;
    options.prepare_layouts = false;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(result.has_value());
    // owner-keyed: 1 owner → 1 entry, even if asset path appears 3 times.
    CHECK(result.value().fonts.count("owner/font") == 1);
    CHECK(result.value().diagnostics.fonts_loaded == 1);
    CHECK(result.value().diagnostics.warnings.empty());
}
