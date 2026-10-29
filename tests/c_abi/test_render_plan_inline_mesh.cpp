#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/hdri_environment.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using Json = nlohmann::json;
using namespace chronon3d;
using namespace chronon3d::render_plan;

namespace {
std::vector<std::byte> tiny_hdri_env_v1() {
    std::vector<std::byte> bytes;
    for (const char c : std::string_view{"HDRIENV1"}) bytes.push_back(static_cast<std::byte>(c));
    const auto u32 = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    };
    const auto f32 = [&](float value) { u32(std::bit_cast<std::uint32_t>(value)); };
    u32(2); u32(2); u32(1); u32(1);
    f32(0.25f); f32(0.75f);
    for (unsigned i = 0; i < 27; ++i) f32(i == 0 ? 1.f : 0.f);
    u32(1);
    for (unsigned face = 0; face < 6; ++face) {
        f32(0.1f); f32(0.2f); f32(0.3f);
    }
    return bytes;
}

TEST_CASE("HDRI environment SH contributes diffuse software illumination") {
    rendering::LightContext light;
    light.enabled = true;
    light.environment_enabled = true;
    // Constant unit radiance convolved with Lambert: L00 = pi * sqrt(4*pi).
    light.environment_irradiance_sh[0] = 11.1366564f;
    light.environment_irradiance_sh[1] = 11.1366564f;
    light.environment_irradiance_sh[2] = 11.1366564f;
    Material2_5D material;
    const auto lit = rendering::evaluate_lighting(Color{0.4f, 0.6f, 0.8f, 1.0f},
        Vec3{0.0f, 0.0f, 1.0f}, light, material);
    CHECK(lit.r == doctest::Approx(0.4f).epsilon(1e-4));
    CHECK(lit.g == doctest::Approx(0.6f).epsilon(1e-4));
    CHECK(lit.b == doctest::Approx(0.8f).epsilon(1e-4));
}
} // namespace

namespace {

Json mesh_layer_json() {
    return {
        {"id", "triangle"}, {"type", "mesh"}, {"color", {1.0, 0.0, 0.0, 1.0}},
        {"inline_mesh", {
            {"vertices", Json::array({
                {{"position", {-0.7, -0.6, 0.0}}, {"normal", {0.0, 0.0, 1.0}}, {"uv", {0.0, 0.0}}},
                {{"position", {0.7, -0.6, 0.0}}, {"normal", {0.0, 0.0, 1.0}}, {"uv", {1.0, 0.0}}},
                {{"position", {0.0, 0.7, 0.0}}, {"normal", {0.0, 0.0, 1.0}}, {"uv", {0.5, 1.0}}}
            })},
            {"indices", {0, 1, 2}}
        }}
    };
}

Json base_mesh_plan(const std::string& schema = kRenderPlanSchemaV3) {
    const int version = schema == kRenderPlanSchemaV3 ? 3 : 2;
    return {
        {"schema", schema}, {"version", version},
        {"job_id", "inline-mesh-test"},
        {"canvas", {{"width", 64}, {"height", 64}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 1}}},
        {"layers", Json::array({mesh_layer_json()})},
        {"output", {{"path", "out.png"}, {"format", "png"}}}
    };
}

InlineMeshPlan triangle_mesh() {
    InlineMeshPlan mesh;
    mesh.vertices = {
        {{{-0.7f, -0.6f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{ 0.7f, -0.6f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{ 0.0f,  0.7f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.5f, 1.0f}}}
    };
    mesh.indices = {0, 1, 2};
    return mesh;
}

RenderPlan typed_mesh_plan() {
    RenderPlan plan;
    plan.schema = kRenderPlanSchemaV3;
    plan.job_id = "typed-inline-mesh";
    plan.canvas = {.width = 64, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "triangle";
    layer.type = LayerType::Mesh;
    layer.color = {1.0f, 0.0f, 0.0f, 1.0f};
    layer.inline_mesh = triangle_mesh();
    plan.layers.push_back(std::move(layer));
    return plan;
}

}  // namespace

TEST_CASE("inline mesh lighting is an opt-in V3 contract and changes plan identity") {
    auto json = base_mesh_plan();
    json["lighting_rig"] = {
        {"key_direction", {-0.4, 0.8, -0.4}},
        {"key_color", {1.0, 0.9, 0.8, 1.0}},
        {"key_intensity", 0.9},
        {"ambient_color", {0.2, 0.3, 0.4, 1.0}},
        {"ambient_intensity", 0.2},
        {"rim_enabled", true}, {"rim_color", {0.2, 0.7, 1.0, 1.0}},
        {"rim_intensity", 0.4}, {"rim_power", 2.5}
    };
    json["layers"][0]["inline_mesh"]["material"] = {
        {"diffuse", 0.8}, {"specular", 0.5}, {"shininess", 48.0},
        {"rim_intensity", 1.2}, {"base_color", {0.8, 0.4, 0.2, 1.0}},
        {"metallic", 0.65}, {"roughness", 0.28},
        {"emissive", {0.1, 0.2, 0.3}}, {"double_sided", true},
        {"alpha_cutoff", 0.25}
    };
    json["layers"][0]["inline_mesh"]["metallic_roughness_texture"] = "textures/orm.png";
    const auto decoded = decode_render_plan(json);
    REQUIRE(decoded);
    REQUIRE(decoded->lighting_rig);
    REQUIRE(decoded->layers[0].inline_mesh->material);
    CHECK(decoded->layers[0].inline_mesh->material->specular == doctest::Approx(0.5f));
    CHECK(decoded->layers[0].inline_mesh->material->metallic == doctest::Approx(0.65f));
    CHECK(decoded->layers[0].inline_mesh->material->roughness == doctest::Approx(0.28f));
    CHECK(decoded->layers[0].inline_mesh->material->emissive->at(2) == doctest::Approx(0.3f));
    CHECK(decoded->layers[0].inline_mesh->material->double_sided == true);
    CHECK(decoded->layers[0].inline_mesh->material->alpha_cutoff == doctest::Approx(0.25f));
    CHECK(decoded->layers[0].inline_mesh->metallic_roughness_texture_asset == "textures/orm.png");
    CHECK(decoded->lighting_rig->rim_enabled);
    const auto repeated = decode_render_plan(json);
    REQUIRE(repeated);
    CHECK(decoded->content_fingerprint == repeated->content_fingerprint);

    auto changed_material = json;
    changed_material["layers"][0]["inline_mesh"]["material"]["shininess"] = 64.0;
    const auto changed = decode_render_plan(changed_material);
    REQUIRE(changed);
    CHECK(decoded->content_fingerprint != changed->content_fingerprint);
    changed_material = json;
    changed_material["layers"][0]["inline_mesh"]["material"]["roughness"] = 0.9;
    const auto changed_roughness = decode_render_plan(changed_material);
    REQUIRE(changed_roughness);
    CHECK(decoded->content_fingerprint != changed_roughness->content_fingerprint);
    changed_material = json;
    changed_material["layers"][0]["inline_mesh"]["metallic_roughness_texture"] = "textures/other.png";
    const auto changed_texture = decode_render_plan(changed_material);
    REQUIRE(changed_texture);
    CHECK(decoded->content_fingerprint != changed_texture->content_fingerprint);

    auto invalid = json;
    invalid["layers"][0]["inline_mesh"]["material"]["diffuse"] = 1.5;
    CHECK_FALSE(decode_render_plan(invalid));
    invalid = json;
    invalid["layers"][0]["inline_mesh"]["metallic_roughness_texture"] = "../unsafe.png";
    CHECK_FALSE(decode_render_plan(invalid));
    invalid = json;
    invalid["layers"][0]["inline_mesh"]["material"]["metallic"] = 1.5;
    CHECK_FALSE(decode_render_plan(invalid));
    invalid = json;
    invalid["layers"][0]["inline_mesh"]["material"]["alpha_cutoff"] = 2.0;
    CHECK_FALSE(decode_render_plan(invalid));

    auto v2 = base_mesh_plan(kRenderPlanSchemaV2);
    v2["lighting_rig"] = json["lighting_rig"];
    CHECK_FALSE(validate_render_plan(v2).ok());
}

TEST_CASE("inline mesh lighting defaults remain unlit and typed materials validate") {
    auto decoded = decode_render_plan(base_mesh_plan());
    REQUIRE(decoded);
    CHECK_FALSE(decoded->layers[0].inline_mesh->material);

    auto plan = typed_mesh_plan();
    plan.layers[0].inline_mesh->material = InlineMeshMaterialPlan{};
    REQUIRE_FALSE(validate_render_budget(plan));
    plan.layers[0].inline_mesh->material->rim_intensity = std::numeric_limits<float>::infinity();
    CHECK(validate_render_budget(plan));
}

TEST_CASE("HDRI environment light decodes, fingerprints, validates asset, and compiles for software shading") {
    auto json = base_mesh_plan();
    Json sh = Json::array();
    for (unsigned i = 0; i < 27; ++i) sh.push_back(i == 0 ? 1.0 : 0.0);
    json["environment_light"] = {
        {"probe_asset", "environment/studio.hdrienv"},
        {"intensity", 0.75}, {"rotation_y", 0.25},
        {"irradiance_sh", sh},
        {"rotation_animation", {
            {"property", "rotation_y"},
            {"keyframes", Json::array({{{"frame", 0}, {"value", {0.25}}}})}
        }}
    };
    const auto decoded = decode_render_plan(json);
    if (!decoded) MESSAGE(decoded.error().path << ": " << decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->environment_light);
    CHECK(decoded->environment_light->probe_asset == "environment/studio.hdrienv");
    CHECK(decoded->environment_light->irradiance_sh[0] == doctest::Approx(1.0f));
    REQUIRE(decoded->environment_light->rotation_animation);
    CHECK(decoded->environment_light->rotation_animation->property == "rotation_y");
    const auto repeated = decode_render_plan(json);
    REQUIRE(repeated);
    CHECK(decoded->content_fingerprint == repeated->content_fingerprint);
    auto changed = json;
    changed["environment_light"]["intensity"] = 0.8;
    const auto changed_plan = decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);

    auto invalid = json;
    invalid["environment_light"]["probe_asset"] = "../outside.hdrienv";
    CHECK_FALSE(decode_render_plan(invalid));
    invalid = json;
    invalid["environment_light"]["irradiance_sh"].erase(0);
    CHECK_FALSE(validate_render_plan(invalid).ok());
    invalid = json;
    invalid["environment_light"]["intensity"] = 17.0;
    CHECK_FALSE(validate_render_plan(invalid).ok());
    invalid = base_mesh_plan(kRenderPlanSchemaV2);
    invalid["environment_light"] = json["environment_light"];
    CHECK_FALSE(validate_render_plan(invalid).ok());

    auto typed = typed_mesh_plan();
    typed.environment_light = decoded->environment_light;
    const auto root = std::filesystem::temp_directory_path() / "chronon-hdri-compile-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "environment");
    const auto assetBytes = tiny_hdri_env_v1();
    {
        std::ofstream file(root / "environment/studio.hdrienv", std::ios::binary);
        file.write(reinterpret_cast<const char*>(assetBytes.data()),
                   static_cast<std::streamsize>(assetBytes.size()));
    }
    assets::AssetResolver resolver;
    resolver.mount(std::filesystem::absolute(root));
    const auto compile_result = compiler::compile(typed, resolver);
    REQUIRE(compile_result);
    const auto prepared_result = chronon3d::render_plan::compile_render_plan(typed, resolver);
    REQUIRE(prepared_result);
    std::filesystem::remove_all(root);
}

TEST_CASE("HDRIENV1 asset decoder validates and reads the deterministic payload") {
    const auto bytes = tiny_hdri_env_v1();
    const auto decoded = assets::decode_hdri_env_v1(bytes);
    CHECK(decoded.source_width == 2);
    CHECK(decoded.source_height == 2);
    CHECK(decoded.face_size == 1);
    CHECK(decoded.rotation_radians == doctest::Approx(0.25f));
    CHECK(decoded.intensity == doctest::Approx(0.75f));
    CHECK(decoded.irradiance_sh[0] == doctest::Approx(1.f));
    REQUIRE(decoded.mips.size() == 1);
    CHECK(decoded.mips[0].faces[5] == std::vector<float>{0.1f, 0.2f, 0.3f});

    const auto rejects = [](std::vector<std::byte> malformed) {
        try { (void)assets::decode_hdri_env_v1(malformed); }
        catch (const std::invalid_argument&) { return true; }
        return false;
    };
    auto malformed = bytes;
    malformed[0] = std::byte{'X'};
    CHECK(rejects(std::move(malformed)));
    malformed = bytes;
    malformed.pop_back();
    CHECK(rejects(std::move(malformed)));
    malformed = bytes;
    malformed.push_back(std::byte{0});
    CHECK(rejects(std::move(malformed)));
    malformed = bytes;
    malformed[28] = std::byte{0}; malformed[29] = std::byte{0};
    malformed[30] = std::byte{0xc0}; malformed[31] = std::byte{0x7f};
    CHECK(rejects(std::move(malformed)));

    const auto root = std::filesystem::temp_directory_path() / "chronon-hdri-env-v1-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream file(root / "studio.hdrienv", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    assets::AssetResolver resolver;
    resolver.mount(std::filesystem::absolute(root));
    const auto loaded = assets::load_hdri_env_v1(resolver, "studio.hdrienv");
    REQUIRE(loaded);
    CHECK(loaded->irradiance_sh[0] == doctest::Approx(1.f));
    CHECK_FALSE(assets::load_hdri_env_v1(resolver, "../studio.hdrienv"));
    std::filesystem::remove_all(root);
}

TEST_CASE("inline mesh validates and decodes in both RenderPlan schema versions") {
    for (const auto* schema : {kRenderPlanSchemaV2, kRenderPlanSchemaV3}) {
        const auto json = base_mesh_plan(schema);
        const auto validation = validate_render_plan(json);
        INFO(validation.format());
        CHECK(validation.ok());
        const auto decoded = decode_render_plan(json);
        REQUIRE(decoded);
        REQUIRE(decoded->layers.size() == 1);
        CHECK(decoded->layers[0].type == LayerType::Mesh);
        REQUIRE(decoded->layers[0].inline_mesh);
        CHECK(decoded->layers[0].inline_mesh->vertices.size() == 3);
        CHECK(decoded->layers[0].inline_mesh->indices == std::vector<std::uint32_t>{0, 1, 2});
        CHECK(decoded->layers[0].inline_mesh->vertices[2].uv[1] == doctest::Approx(1.0f));
    }
}

TEST_CASE("inline mesh rejects missing, cross-typed, referenced, malformed, and non-triangle data") {
    auto json = base_mesh_plan();
    json["layers"][0].erase("inline_mesh");
    CHECK_FALSE(decode_render_plan(json));

    json = base_mesh_plan();
    json["layers"][0]["type"] = "color";
    json["layers"][0]["color"] = {1, 1, 1, 1};
    CHECK_FALSE(validate_render_plan(json).ok());

    json = base_mesh_plan();
    json["layers"][0].erase("inline_mesh");
    json["layers"][0]["asset"] = "mesh.glb";
    CHECK_FALSE(validate_render_plan(json).ok());

    json = base_mesh_plan();
    json["layers"][0]["inline_mesh"]["indices"] = {0, 1, 7};
    CHECK_FALSE(decode_render_plan(json));
    json = base_mesh_plan();
    json["layers"][0]["inline_mesh"]["indices"] = {0, 1};
    CHECK_FALSE(validate_render_plan(json).ok());
    json = base_mesh_plan();
    json["layers"][0]["inline_mesh"]["indices"] = {0, -1, 2};
    CHECK_FALSE(validate_render_plan(json).ok());

    json = base_mesh_plan();
    json["layers"][0]["inline_mesh"]["vertices"][0]["position"] = {0, 0};
    CHECK_FALSE(validate_render_plan(json).ok());
    json = base_mesh_plan();
    json["layers"][0]["inline_mesh"]["vertices"][0]["normal"] = {0, 0, 2};
    CHECK_FALSE(validate_render_plan(json).ok());
    json = base_mesh_plan();
    json["layers"][0]["inline_mesh"]["vertices"][0]["unexpected"] = 1;
    CHECK_FALSE(validate_render_plan(json).ok());

    json = base_mesh_plan();
    json["layers"][0]["shape"] = {{"type", "path"}};
    CHECK_FALSE(validate_render_plan(json).ok());
    json = base_mesh_plan();
    json["layers"][0]["animation"] = {{"tracks", Json::array({
        {{"property", "position_x"}, {"keyframes", Json::array({{{"frame", 0}, {"value", 3}}})}}
    })}};
    CHECK_FALSE(decode_render_plan(json));
}

TEST_CASE("inline mesh budget enforces aggregate bytes and typed structural invariants") {
    auto plan = typed_mesh_plan();
    REQUIRE_FALSE(validate_render_budget(plan));
    auto mesh = triangle_mesh();
    mesh.indices = {0, 1, 2, 0};
    plan.layers[0].inline_mesh = mesh;
    CHECK(validate_render_budget(plan));

    plan = typed_mesh_plan();
    plan.layers[0].inline_mesh->indices[2] = 3;
    CHECK(validate_render_budget(plan));
    plan.layers[0].inline_mesh = triangle_mesh();
    plan.layers[0].inline_mesh->vertices[0].position[0] = std::numeric_limits<float>::infinity();
    CHECK(validate_render_budget(plan));

    plan = typed_mesh_plan();
    plan.layers[0].inline_mesh.reset();
    CHECK(validate_render_budget(plan));
    plan.layers[0].inline_mesh = triangle_mesh();
    plan.layers[0].shape = ShapeContentPlan{};
    CHECK(validate_render_budget(plan));

    plan = typed_mesh_plan();
    plan.layers.clear();
    plan.layers.resize(7);
    for (auto& layer : plan.layers) {
        layer.type = LayerType::Mesh;
        layer.inline_mesh = triangle_mesh();
    }
    for (auto& layer : plan.layers) {
        layer.inline_mesh->vertices.resize(250'000);
        layer.inline_mesh->indices.resize(750'000, 0);
    }
    const auto error = validate_render_budget(plan);
    REQUIRE(error);
    CHECK(error->path == "layers[].inline_mesh");
}

TEST_CASE("inline mesh bytes affect stable plan fingerprint and typed compiler IR") {
    auto json = base_mesh_plan();
    const auto first = decode_render_plan(json);
    REQUIRE(first);
    const auto repeated = decode_render_plan(json);
    REQUIRE(repeated);
    CHECK(first->content_fingerprint == repeated->content_fingerprint);
    json["layers"][0]["inline_mesh"]["vertices"][1]["position"][0] = 0.6;
    const auto changed = decode_render_plan(json);
    REQUIRE(changed);
    CHECK(first->content_fingerprint != changed->content_fingerprint);

    auto plan = typed_mesh_plan();
    plan.layers[0].inline_mesh->material = InlineMeshMaterialPlan{};
    plan.lighting_rig = LightingRigPlan{};
    plan.content_fingerprint = compute_render_plan_content_fingerprint(plan);
    assets::AssetResolver resolver;
    const auto compiled = compiler::compile(plan, resolver);
    REQUIRE(compiled);
    REQUIRE(compiled->nodes().size() == 1);
    CHECK(compiled->nodes()[0].kind == compiler::NodeKind::Mesh);
    REQUIRE(compiled->layers().size() == 1);
    REQUIRE(compiled->layers()[0].inline_mesh);
    CHECK(compiled->layers()[0].inline_mesh->indices == std::vector<std::uint32_t>{0, 1, 2});
    const auto sampled = compiler::TypedProgramSampler{}.sample(*compiled, Frame{0});
    REQUIRE(sampled.layers.size() == 1);
    const auto* mesh = std::get_if<compiler::SampledMeshParameters>(&sampled.layers[0].payload);
    REQUIRE(mesh);
    REQUIRE(mesh->inline_mesh);
    CHECK(mesh->inline_mesh->vertices[1].position[0] == doctest::Approx(0.7f));

    const auto scene = compiler::SceneMaterializer{}.materialize_scene(*compiled);
    CHECK(scene.camera_2_5d().enabled);
    REQUIRE(scene.layers().size() == 1);
    REQUIRE(scene.layers()[0].nodes.size() == 1);
    const auto& node = scene.layers()[0].nodes[0];
    CHECK(node.shape.type() == ShapeType::Mesh);
    REQUIRE(node.shape.mesh_shape().inline_mesh);
    CHECK(node.shape.mesh_shape().inline_mesh->vertices().size() == 3);
    CHECK(node.shape.mesh_shape().inline_mesh->indices() == std::vector<u32>{0, 1, 2});
    REQUIRE(node.inline_mesh_material);
    CHECK(node.inline_mesh_material->specular == doctest::Approx(0.25f));
    CHECK(scene.light_context().enabled);
    CHECK(scene.rim_light().enabled == plan.lighting_rig->rim_enabled);
}

TEST_CASE("inline mesh animation selects adjacent samples and preserves texture asset") {
    auto json = base_mesh_plan();
    json["layers"][0]["inline_mesh"]["metallic_roughness_texture"] = "orm.png";
    json["layers"][0]["inline_mesh"]["animation"] = Json::array({
        {{"frame", 0}, {"positions", {{-0.7, -0.6, 0.0}, {0.7, -0.6, 0.0}, {0.0, 0.7, 0.0}}},
         {"normals", {{0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}}}},
        {{"frame", 10}, {"positions", {{-0.5, -0.6, 0.0}, {0.9, -0.6, 0.0}, {0.2, 0.7, 0.0}}},
         {"normals", {{0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}}}}
    });
    json["layers"][0]["asset"] = "portrait.png";
    const auto decoded = decode_render_plan(json);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].inline_mesh);
    CHECK(decoded->layers[0].asset == "portrait.png");
    CHECK(decoded->layers[0].inline_mesh->animation[1].normals[0][1] == doctest::Approx(1.0f));

    auto plan = decode_render_plan(json);
    REQUIRE(plan);
    const auto root = std::filesystem::temp_directory_path() /
                      "chronon-inline-mesh-texture-test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "portrait.png", std::ios::binary).put('\0');
    std::ofstream(root / "orm.png", std::ios::binary).put('\0');
    assets::AssetResolver resolver;
    resolver.mount(std::filesystem::absolute(root));
    const auto compiled = compiler::compile(*plan, resolver);
    if (!compiled) INFO(compiled.error().message);
    REQUIRE(compiled);
    const auto scene = compiler::SceneMaterializer{}.materialize_scene(*compiled);
    REQUIRE(scene.layers().size() == 1);
    REQUIRE(scene.layers()[0].nodes.size() == 1);
    CHECK(scene.layers()[0].nodes[0].inline_mesh_texture_asset == "portrait.png");
    CHECK(scene.layers()[0].nodes[0].inline_mesh_metallic_roughness_texture_asset == "orm.png");
    CHECK(scene.layers()[0].nodes[0].inline_mesh_animated);
    REQUIRE(scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh);
    CHECK(scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh->vertices()[0].normal.z ==
          doctest::Approx(1.0f));
    const auto sampled = compiler::TypedProgramSampler{}.sample(*compiled, Frame{5});
    REQUIRE(sampled.layers.size() == 1);
    const auto* mesh = std::get_if<compiler::SampledMeshParameters>(&sampled.layers[0].payload);
    REQUIRE(mesh);
    CHECK(sampled.layers[0].dirty);
    REQUIRE(mesh->animation_before);
    REQUIRE(mesh->animation_after);
    CHECK(mesh->animation_before->frame == 0);
    CHECK(mesh->animation_after->frame == 10);
    CHECK(mesh->animation_mix == doctest::Approx(0.5f));
}

TEST_CASE("V3 parametric mesh primitives expand deterministically within the raw mesh contract") {
    InlineMeshPrimitivePlan boxPrimitive;
    boxPrimitive.kind = "box";
    const auto boxA = expand_inline_mesh_primitive(boxPrimitive);
    const auto boxB = expand_inline_mesh_primitive(boxPrimitive);
    CHECK(boxA.vertices.size() == 36);
    CHECK(boxA.indices.size() == 36);
    CHECK(boxA.vertices[0].normal == std::array<float, 3>{0.f, 0.f, 1.f});
    CHECK(boxA.indices == boxB.indices);
    CHECK(boxA.vertices[0].position == boxB.vertices[0].position);

    InlineMeshPrimitivePlan planePrimitive;
    planePrimitive.kind = "plane";
    planePrimitive.width_segments = 2;
    planePrimitive.height_segments = 3;
    const auto plane = expand_inline_mesh_primitive(planePrimitive);
    CHECK(plane.vertices.size() == 12);
    CHECK(plane.indices.size() == 36);

    InlineMeshPrimitivePlan spherePrimitive;
    spherePrimitive.kind = "sphere";
    spherePrimitive.width_segments = 8;
    spherePrimitive.height_segments = 4;
    const auto sphere = expand_inline_mesh_primitive(spherePrimitive);
    CHECK(sphere.vertices.size() == 45);
    CHECK(sphere.indices.size() == 144);

    InlineMeshPrimitivePlan cylinderPrimitive;
    cylinderPrimitive.kind = "cylinder";
    cylinderPrimitive.radial_segments = 8;
    cylinderPrimitive.height_segments = 1;
    const auto cylinder = expand_inline_mesh_primitive(cylinderPrimitive);
    CHECK(cylinder.vertices.size() == 38);
    CHECK(cylinder.indices.size() == 96);

    InlineMeshPrimitivePlan conePrimitive = cylinderPrimitive;
    conePrimitive.kind = "cone";
    const auto cone = expand_inline_mesh_primitive(conePrimitive);
    CHECK(cone.vertices.size() == 28);
    CHECK(cone.indices.size() == 72);

    InlineMeshPrimitivePlan torusPrimitive;
    torusPrimitive.kind = "torus";
    torusPrimitive.radial_segments = 8;
    torusPrimitive.tubular_segments = 6;
    const auto torus = expand_inline_mesh_primitive(torusPrimitive);
    CHECK(torus.vertices.size() == 63);
    CHECK(torus.indices.size() == 288);

    InlineMeshPrimitivePlan extrusion;
    extrusion.kind = "extrude";
    extrusion.depth = 4.0f;
    extrusion.profile = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    const auto extruded = expand_inline_mesh_primitive(extrusion);
    CHECK(extruded.vertices.size() == 36);
    CHECK(extruded.indices.size() == 36);
    CHECK(extruded.vertices[0].position[2] == doctest::Approx(2.0f));
    CHECK(extruded.vertices[0].normal == std::array<float, 3>{0.f, 0.f, 1.f});
    CHECK(extruded.vertices[3].normal == std::array<float, 3>{0.f, 0.f, -1.f});
    CHECK(extruded.vertices.back().position[2] == doctest::Approx(2.0f));

    InlineMeshPrimitivePlan concave = extrusion;
    concave.profile = {{0.0f, 0.0f}, {2.0f, 0.0f}, {1.0f, 0.5f}, {2.0f, 2.0f}, {0.0f, 2.0f}};
    CHECK(expand_inline_mesh_primitive(concave).indices.size() == 48);

    InlineMeshPrimitivePlan overBudget;
    overBudget.kind = "plane";
    overBudget.width_segments = 512;
    overBudget.height_segments = 512;
    CHECK_THROWS_AS(([&] { (void)expand_inline_mesh_primitive(overBudget); }()), std::invalid_argument);
    InlineMeshPrimitivePlan invalid = boxPrimitive;
    invalid.width = 0.f;
    CHECK_THROWS_AS(([&] { (void)expand_inline_mesh_primitive(invalid); }()), std::invalid_argument);
    InlineMeshPrimitivePlan invalidExtrusion = extrusion;
    invalidExtrusion.bevel = 0.5f;
    CHECK_THROWS_AS(([&] { (void)expand_inline_mesh_primitive(invalidExtrusion); }()), std::invalid_argument);
}

TEST_CASE("V3 decoder accepts a primitive descriptor and rejects mixed geometry") {
    auto json = base_mesh_plan();
    json["layers"][0]["inline_mesh"] = {{"primitive", {{"kind", "box"}, {"width", 2.0}}}};
    const auto decoded = decode_render_plan(json);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].inline_mesh);
    CHECK(decoded->layers[0].inline_mesh->vertices.size() == 36);
    CHECK(decoded->layers[0].inline_mesh->indices.size() == 36);
    CHECK(decoded->content_fingerprint == compute_render_plan_content_fingerprint(*decoded));

    auto rawEquivalent = base_mesh_plan();
    Json rawMesh;
    rawMesh["vertices"] = Json::array();
    rawMesh["indices"] = Json::array();
    for (const auto& vertex : decoded->layers[0].inline_mesh->vertices) {
        rawMesh["vertices"].push_back({{"position", vertex.position}, {"normal", vertex.normal}, {"uv", vertex.uv}});
    }
    rawMesh["indices"] = decoded->layers[0].inline_mesh->indices;
    rawEquivalent["layers"][0]["inline_mesh"] = std::move(rawMesh);
    const auto rawDecoded = decode_render_plan(rawEquivalent);
    REQUIRE(rawDecoded);
    CHECK(rawDecoded->content_fingerprint == decoded->content_fingerprint);

    auto changedPrimitive = base_mesh_plan();
    changedPrimitive["layers"][0]["inline_mesh"] = {{"primitive", {{"kind", "box"}, {"width", 3.0}}}};
    const auto changed = decode_render_plan(changedPrimitive);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != decoded->content_fingerprint);

    json["layers"][0]["inline_mesh"]["vertices"] = Json::array({{{"position", {0,0,0}}}});
    CHECK_FALSE(decode_render_plan(json));

    auto v2 = base_mesh_plan(kRenderPlanSchemaV2);
    v2["layers"][0]["inline_mesh"] = {{"primitive", {{"kind", "box"}}}};
    CHECK_FALSE(decode_render_plan(v2));

    auto inapplicable = base_mesh_plan();
    inapplicable["layers"][0]["inline_mesh"] = {
        {"primitive", {{"kind", "box"}, {"radius", 2.0}}}};
    CHECK_FALSE(decode_render_plan(inapplicable));

    auto inapplicableProfile = base_mesh_plan();
    inapplicableProfile["layers"][0]["inline_mesh"] = {
        {"primitive", {{"kind", "box"}, {"profile", {{0, 0}, {1, 0}, {0, 1}}}}}};
    CHECK_FALSE(decode_render_plan(inapplicableProfile));

    auto extrusion = base_mesh_plan();
    extrusion["layers"][0]["inline_mesh"] = {{"primitive", {
        {"kind", "extrude"}, {"depth", 2.0},
        {"profile", {{0, 0}, {2, 0}, {1, 1}, {2, 2}, {0, 2}}}}}};
    const auto extruded = decode_render_plan(extrusion);
    REQUIRE(extruded);
    CHECK(extruded->layers[0].inline_mesh->indices.size() == 48);
}

TEST_CASE("mesh layers accept the same post effects as other rasterized layers") {
    auto supported = base_mesh_plan();
    supported["layers"][0]["effects"] = Json::array({Json{
        {"type", "catalog"}, {"effect_id", "color.tint"},
        {"params", Json::array({Json{{"param", "amount"}, {"value", 0.25}}})}
    }});
    const auto decoded = decode_render_plan(supported);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    CHECK(decoded->layers[0].effects[0].canonical_effect_id == "color.tint");

    auto generic_post_effect = supported;
    generic_post_effect["layers"][0]["effects"][0]["effect_id"] = "blur.gaussian";
    generic_post_effect["layers"][0]["effects"][0]["params"] = Json::array({Json{
        {"param", "radius"}, {"value", 12.0}
    }});
    const auto generic_decoded = decode_render_plan(generic_post_effect);
    if (!generic_decoded) INFO(generic_decoded.error().message);
    REQUIRE(generic_decoded);
    CHECK(generic_decoded->layers[0].effects[0].canonical_effect_id == "blur.gaussian");
}

TEST_CASE("mesh shadow participation is explicit and fingerprinted") {
    auto source = base_mesh_plan();
    source["layers"][0]["casts_shadows"] = true;
    source["layers"][0]["accepts_shadows"] = false;
    const auto decoded = decode_render_plan(source);
    INFO(decoded.error().message);
    REQUIRE(decoded);
    CHECK(decoded->layers[0].casts_shadows == true);
    CHECK(decoded->layers[0].accepts_shadows == false);

    source["layers"][0]["accepts_shadows"] = true;
    const auto changed = decode_render_plan(source);
    REQUIRE(changed);
    CHECK(decoded->content_fingerprint != changed->content_fingerprint);
}
