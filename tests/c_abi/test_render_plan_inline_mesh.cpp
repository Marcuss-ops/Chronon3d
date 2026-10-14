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
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "../helpers/test_utils.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstring>
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

TEST_CASE("V3 baked inline mesh animation decodes, fingerprints, interpolates and changes software pixels") {
    auto json = base_mesh_plan();
    json["canvas"]["duration_frames"] = 3;
    auto& mesh = json["layers"][0]["inline_mesh"];
    json["layers"][0]["enable_3d"] = true;
    json["layers"][0]["casts_shadows"] = false;
    json["layers"][0]["accepts_shadows"] = false;
    mesh["vertices"] = Json::array({
        {{"position", {-20.0, -20.0, -1500.0}}, {"normal", {0.0, 0.0, 1.0}}, {"uv", {0.0, 0.0}}},
        {{"position", {20.0, -20.0, -1500.0}}, {"normal", {0.0, 0.0, 1.0}}, {"uv", {1.0, 0.0}}},
        {{"position", {0.0, 20.0, -1500.0}}, {"normal", {0.0, 0.0, 1.0}}, {"uv", {0.5, 1.0}}}
    });
    mesh["material"] = {{"base_color", {0.2, 0.8, 0.3, 0.65}}};
    json["lighting_rig"] = {
        {"key_direction", {-0.2857143, 0.8571429, -0.4285714}},
        {"key_intensity", 0.8}, {"ambient_color", {0.6, 0.65, 0.7, 1.0}},
        {"ambient_intensity", 0.25}, {"rim_enabled", true},
        {"rim_color", {1.0, 1.0, 1.0, 1.0}},
        {"rim_intensity", 0.3}, {"rim_power", 2.2}
    };
    mesh["animation"] = Json::array({
        {{"frame", 0}, {"positions", {{-20.0, -20.0, -1500.0}, {20.0, -20.0, -1500.0}, {0.0, 20.0, -1500.0}}}},
        {{"frame", 2}, {"positions", {{-20.0, -20.0, -1500.0}, {20.0, -20.0, -1500.0}, {0.0, 40.0, -1500.0}}}}
    });
    const auto decoded = decode_render_plan(json);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].inline_mesh);
    CHECK(decoded->layers[0].inline_mesh->animation.size() == 2);
    CHECK(decoded->layers[0].inline_mesh->vertices[2].uv == std::array<float, 2>{0.5f, 1.0f});
    REQUIRE(decoded->layers[0].inline_mesh->material);
    REQUIRE(decoded->layers[0].inline_mesh->material->base_color);
    CHECK((*decoded->layers[0].inline_mesh->material->base_color)[3] == doctest::Approx(0.65f));

    auto changed = json;
    changed["layers"][0]["inline_mesh"]["animation"][1]["positions"][2][1] = 400.0;
    const auto changed_decoded = decode_render_plan(changed);
    REQUIRE(changed_decoded);
    CHECK(decoded->content_fingerprint != changed_decoded->content_fingerprint);
    assets::AssetResolver sampler_resolver;
    const auto sampled_program = compiler::compile(*decoded, sampler_resolver);
    REQUIRE(sampled_program);
    const auto sampled_middle = compiler::TypedProgramSampler{}.sample(*sampled_program, 1.0);
    REQUIRE(sampled_middle.layers.size() == 1);
    const auto* sampled_mesh = std::get_if<compiler::SampledMeshParameters>(&sampled_middle.layers[0].payload);
    REQUIRE(sampled_mesh);
    REQUIRE(sampled_mesh->positions.size() == 3);
    CHECK(sampled_mesh->positions[2][1] == doctest::Approx(30.0f));
    REQUIRE(sampled_mesh->normals.size() == 3);
    CHECK(sampled_mesh->normals[0][2] > 0.0f);
    CHECK(sampled_middle.layers[0].dirty);
    CHECK(sampled_middle.layers[0].kind == compiler::NodeKind::Mesh);
    CHECK(sampled_mesh->inline_mesh->vertices[2].uv[1] == doctest::Approx(1.0f));
    REQUIRE(sampled_mesh->inline_mesh->material->base_color);
    CHECK((*sampled_mesh->inline_mesh->material->base_color)[3] == doctest::Approx(0.65f));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    if (!prepared) INFO(prepared.error().message);
    REQUIRE(prepared);
    auto frame_context = [&](double frame) {
        FrameContextParams params;
        params.global_time = SampleTime::from_frame(frame, decoded->canvas.fps);
        params.duration = decoded->canvas.duration;
        params.width = decoded->canvas.width;
        params.height = decoded->canvas.height;
        return make_frame_context(params);
    };
    auto first_scene = prepared->composition->evaluate(frame_context(0.0));
    auto subframe_scene = prepared->composition->evaluate(frame_context(0.5));
    auto middle_scene = prepared->composition->evaluate(frame_context(1.0));
    auto final_scene = prepared->composition->evaluate(frame_context(2.0));
    const auto& first_mesh = *first_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh;
    const auto& subframe_mesh = *subframe_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh;
    const auto& middle_mesh = *middle_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh;
    const auto& final_mesh = *final_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh;
    CHECK(first_mesh.vertices()[2].position.y == doctest::Approx(20.0f));
    CHECK(subframe_mesh.vertices()[2].position.y == doctest::Approx(25.0f));
    CHECK(middle_mesh.vertices()[2].position.y == doctest::Approx(30.0f));
    CHECK(final_mesh.vertices()[2].position.y == doctest::Approx(40.0f));
    // Scene evaluation owns its sampled mesh values; evaluating another time
    // must not mutate already returned topology or reuse stale vertices.
    CHECK(first_mesh.vertices()[2].position.y == doctest::Approx(20.0f));
    CHECK(subframe_mesh.vertices()[2].position.y == doctest::Approx(25.0f));
    CHECK(middle_mesh.vertices()[2].position.y == doctest::Approx(30.0f));
    CHECK(first_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh !=
          subframe_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh);
    CHECK(subframe_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh !=
          middle_scene.layers()[0].nodes[0].shape.mesh_shape().inline_mesh);
    CHECK(middle_mesh.vertices()[2].uv.y == doctest::Approx(1.0f));
    REQUIRE(middle_scene.layers()[0].nodes[0].inline_mesh_material);
    CHECK(middle_scene.layers()[0].nodes[0].inline_mesh_material->base_color->a == doctest::Approx(0.65f));

    const auto sampled_first = compiler::TypedProgramSampler{}.sample(*sampled_program, Frame{0});
    const auto sampled_last = compiler::TypedProgramSampler{}.sample(*sampled_program, Frame{2});
    const auto sampled_program_metadata = sampled_program->metadata();
    CHECK(sampled_program_metadata.width == 64);
    CHECK(sampled_program_metadata.height == 64);
    const auto* sampled_first_mesh = std::get_if<compiler::SampledMeshParameters>(&sampled_first.layers[0].payload);
    const auto* sampled_last_mesh = std::get_if<compiler::SampledMeshParameters>(&sampled_last.layers[0].payload);
    REQUIRE(sampled_first_mesh);
    REQUIRE(sampled_last_mesh);
    REQUIRE(sampled_first_mesh->positions.size() == 3);
    REQUIRE(sampled_last_mesh->positions.size() == 3);
    CHECK(sampled_first_mesh->positions[2][1] == doctest::Approx(20.0f));
    CHECK(sampled_last_mesh->positions[2][1] == doctest::Approx(40.0f));
    CHECK(sampled_first.layers[0].dirty);
    CHECK(sampled_last.layers[0].dirty);

    using namespace chronon3d::test;
    TestRenderer first_renderer;
    TestRenderer last_renderer;
    const auto first_pixels = first_renderer.render_program(*sampled_program, Frame{0});
    REQUIRE(first_pixels);
    std::size_t first_covered = 0;
    for (int y = 0; y < first_pixels->height(); ++y)
        for (int x = 0; x < first_pixels->width(); ++x)
            first_covered += first_pixels->get_pixel(x, y).a > 0.01f;
    INFO("first mesh raster coverage=" << first_covered);
    CHECK(first_covered > 0);
    const std::vector<std::byte> first_frame_bytes(
        first_pixels->bytes().begin(), first_pixels->bytes().end());
    const auto last_pixels = last_renderer.render_program(*sampled_program, Frame{2});
    REQUIRE(last_pixels);
    CHECK(first_frame_bytes.size() == last_pixels->bytes().size());
    CHECK(std::memcmp(first_frame_bytes.data(), last_pixels->bytes().data(),
                      first_frame_bytes.size()) != 0);
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
