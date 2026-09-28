#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>

using Json = nlohmann::json;
using namespace chronon3d;
using namespace chronon3d::render_plan;

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
        {"rim_intensity", 1.2}, {"base_color", {0.8, 0.4, 0.2, 1.0}}
    };
    const auto decoded = decode_render_plan(json);
    REQUIRE(decoded);
    REQUIRE(decoded->lighting_rig);
    REQUIRE(decoded->layers[0].inline_mesh->material);
    CHECK(decoded->layers[0].inline_mesh->material->specular == doctest::Approx(0.5f));
    CHECK(decoded->lighting_rig->rim_enabled);
    const auto repeated = decode_render_plan(json);
    REQUIRE(repeated);
    CHECK(decoded->content_fingerprint == repeated->content_fingerprint);

    auto changed_material = json;
    changed_material["layers"][0]["inline_mesh"]["material"]["shininess"] = 64.0;
    const auto changed = decode_render_plan(changed_material);
    REQUIRE(changed);
    CHECK(decoded->content_fingerprint != changed->content_fingerprint);

    auto invalid = json;
    invalid["layers"][0]["inline_mesh"]["material"]["diffuse"] = 1.5;
    CHECK_FALSE(validate_render_plan(invalid).ok());

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
