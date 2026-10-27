#include "test_prepared_render_plan_test_support.hpp"

TEST_CASE("render plan samples inline data-feed values at the requested frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "data-fed"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"},
            {"data_feed", {{"values", {0.0, 0.5, 1.0}}, {"interpolation", "linear"}}}
        }})}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(prepared->compiled_composition, {}, Frame{1});
    const auto repeat = evaluate(prepared->compiled_composition, {}, Frame{1});
    REQUIRE(first);
    REQUIRE(repeat);
    CHECK(first->scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    CHECK(repeat->scene.layers()[0].transform.opacity == first->scene.layers()[0].transform.opacity);
}

TEST_CASE("render plan resolves digest-bound data-feed assets before evaluation") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    const auto series_path = directory.path / "series.json";
    { std::ofstream output(series_path); output << "[0,0.25,0.75,1]"; }
    const auto digest = assets::sha256_file(series_path);
    REQUIRE(digest);
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "asset-fed"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"},
            {"data_feed", {{"asset", "series.json"}, {"sha256", digest->hex()},
                             {"item_count", 4}, {"interpolation", "linear"}}}
        }})}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto prepared = compile_render_plan(*decoded, resolver);
    if (!prepared) MESSAGE(prepared.error().message);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition, {}, Frame{1});
    REQUIRE(evaluated);
    CHECK(evaluated->scene.layers()[0].transform.opacity == doctest::Approx(0.25f));
    auto tampered = source;
    tampered["layers"][0]["animation"]["tracks"][0]["data_feed"]["sha256"] = std::string(64, '0');
    const auto tampered_plan = decode_render_plan(tampered);
    REQUIRE(tampered_plan);
    const auto rejected = compile_render_plan(*tampered_plan, resolver);
    CHECK_FALSE(rejected);
}

TEST_CASE("audio_react bakes audio before evaluation and fails clearly without FFmpeg") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    const auto wav_path = directory.path / "beat.wav";
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::uint32_t sample_count = 12000;
    const auto write_u16 = [](std::ostream& out, std::uint16_t value) {
        const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8)};
        out.write(bytes, sizeof(bytes));
    };
    const auto write_u32 = [](std::ostream& out, std::uint32_t value) {
        const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
            static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
        out.write(bytes, sizeof(bytes));
    };
    {
        std::ofstream out(wav_path, std::ios::binary);
        out.write("RIFF", 4); write_u32(out, 36 + sample_count * 2); out.write("WAVE", 4);
        out.write("fmt ", 4); write_u32(out, 16); write_u16(out, 1); write_u16(out, 1);
        write_u32(out, sample_rate); write_u32(out, sample_rate * 2); write_u16(out, 2); write_u16(out, 16);
        out.write("data", 4); write_u32(out, sample_count * 2);
        for (std::uint32_t i = 0; i < sample_count; ++i) {
            const auto value = static_cast<std::int16_t>(std::sin(2.0 * 3.141592653589793 * 440.0 * i / sample_rate) * 12000.0);
            write_u16(out, static_cast<std::uint16_t>(value));
        }
    }
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "audio-reactive"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"}, {"audio_react", {{"source", "mix:0"}, {"gain", 2.0}}}
        }})}}}
    }});
    source["audio"] = nlohmann::json{{"clips", nlohmann::json::array({
        nlohmann::json{{"id", "beat"}, {"asset", "beat.wav"}}})}};
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto prepared = compile_render_plan(*decoded, resolver);
    if (!chronon3d::render_plan::detail::native_audio_react_available()) {
        CHECK_FALSE(prepared);
        if (!prepared) CHECK(prepared.error().message.find("CHRONON3D_ENABLE_NATIVE_FFMPEG") != std::string::npos);
        return;
    }
    if (!prepared) MESSAGE(prepared.error().message);
    REQUIRE(prepared);
    assets::AssetResolver repeated_resolver;
    repeated_resolver.mount(directory.path);
    const auto repeated_prepared = compile_render_plan(*decoded, repeated_resolver);
    if (!repeated_prepared) MESSAGE(repeated_prepared.error().message);
    REQUIRE(repeated_prepared);
    CHECK(repeated_prepared->fingerprint.content_digest == prepared->fingerprint.content_digest);
    const auto first = evaluate(prepared->compiled_composition, {}, Frame{3});
    const auto second = evaluate(prepared->compiled_composition, {}, Frame{3});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->scene.layers()[0].transform.opacity > 0.45f);
    CHECK(first->scene.layers()[0].transform.opacity < 0.60f);
    CHECK(second->scene.layers()[0].transform.opacity == first->scene.layers()[0].transform.opacity);
}

TEST_CASE("render plan auto_orient derives z rotation from the position tangent") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "auto-oriented-shape";
    plan.canvas = {.width = 32, .height = 32,
                   .fps = FrameRate{30, 1}, .duration = Frame{11}};
    LayerPlan layer;
    layer.id = "path-follower";
    layer.type = LayerType::Shape;
    layer.size = {8.0f, 8.0f};
    layer.size_dimensions = 2;
    layer.auto_orient = true;
    layer.shape = ShapeContentPlan{};
    AnimationTrackPlan track;
    track.property = "position";
    track.keyframes = {{Frame{0}, {0.0f, 0.0f}}, {Frame{10}, {10.0f, 10.0f}}};
    AnimationPlan animation;
    animation.tracks.push_back(std::move(track));
    layer.animation = std::move(animation);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{5});
    REQUIRE(evaluated);
    CHECK(glm::degrees(glm::eulerAngles(evaluated->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(45.0f).epsilon(0.001f));
}

TEST_CASE("render plan materializes deterministic particle shapes per frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "particle-layer";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{25}};
    LayerPlan layer;
    layer.id = "dust";
    layer.type = LayerType::Particles;
    ParticleEmitterParams emitter;
    emitter.count = 8;
    emitter.seed = 17;
    emitter.emitter_size = {100.0f, 60.0f};
    emitter.velocity_min = {-5.0f, -12.0f};
    emitter.velocity_max = {5.0f, -2.0f};
    emitter.lifetime_min = 2.0f;
    emitter.lifetime_max = 4.0f;
    emitter.size_min = 2.0f;
    emitter.size_max = 10.0f;
    emitter.color_ramp = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {{0.0f, Color{1.0f, 1.0f, 1.0f, 0.0f}},
         {1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}}});
    layer.particle_emitter = emitter;
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    const auto repeated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    REQUIRE(first);
    REQUIRE(repeated);
    REQUIRE(first->scene.layers().size() == 1);
    REQUIRE(repeated->scene.layers().size() == 1);
    const auto& first_nodes = first->scene.layers()[0].nodes;
    const auto& repeated_nodes = repeated->scene.layers()[0].nodes;
    REQUIRE(first_nodes.size() == emitter.count);
    REQUIRE(repeated_nodes.size() == first_nodes.size());
    for (std::size_t index = 0; index < first_nodes.size(); ++index) {
        CHECK(first_nodes[index].shape.type() == ShapeType::Circle);
        CHECK(first_nodes[index].world_transform.position.x ==
              repeated_nodes[index].world_transform.position.x);
        CHECK(first_nodes[index].world_transform.position.y ==
              repeated_nodes[index].world_transform.position.y);
    }

    // Opaque normal-blend particles share one path node while retaining one
    // closed contour per stable particle sample.
    RenderPlan batch_plan;
    batch_plan.job_id = "particle-batch";
    batch_plan.canvas = plan.canvas;
    LayerPlan batch_layer;
    batch_layer.id = "opaque-dust";
    batch_layer.type = LayerType::Particles;
    emitter.color_ramp = {};
    batch_layer.particle_emitter = emitter;
    batch_plan.layers.push_back(std::move(batch_layer));
    const auto batch_prepared = compile_render_plan(batch_plan, resolver);
    REQUIRE(batch_prepared);
    const auto batch_frame = evaluate(
        batch_prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    REQUIRE(batch_frame);
    REQUIRE(batch_frame->scene.layers().size() == 1);
    REQUIRE(batch_frame->scene.layers()[0].nodes.size() == 1);
    const auto& batch_path = batch_frame->scene.layers()[0].nodes[0].shape.path();
    CHECK(batch_frame->scene.layers()[0].nodes[0].shape.type() == ShapeType::Path);
    CHECK(std::count_if(batch_path.commands.begin(), batch_path.commands.end(),
        [](const auto& command) { return command.type == PathCommandType::Close; }) ==
        emitter.count);
    CHECK(batch_path.commands.size() <= emitter.count * 65);
}

TEST_CASE("render plan compiles radial light into a screen-space gradient circle") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "radial-light-layer";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "warm-light";
    layer.type = LayerType::Light;
    layer.position = {160.0f, 90.0f, 0.0f};
    layer.position_dimensions = 2;
    layer.light = LightLayerPlan{
        .radius = 80.0f, .color = {1.0f, 0.5f, 0.25f, 1.0f}, .intensity = 1.25f};
    layer.blend_mode = BlendMode::Screen;
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.type() == ShapeType::Circle);
}

TEST_CASE("render plan lowers point spot and directional lights to per-pixel scene sources") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    assets::AssetResolver resolver;
    for (const auto kind : {LightKindPlan::Point, LightKindPlan::Spot, LightKindPlan::Directional}) {
        RenderPlan plan;
        plan.job_id = "unrendered-light-kind";
        plan.canvas = {.width = 64, .height = 64,
                       .fps = FrameRate{24, 1}, .duration = Frame{1}};
        LayerPlan layer;
        layer.id = "key-light";
        layer.type = LayerType::Light;
        layer.position = {32.0f, 32.0f, 0.0f};
        layer.position_dimensions = 2;
        LightLayerPlan light;
        light.kind = kind;
        layer.light = light;
        plan.layers.push_back(std::move(layer));
        // Non-radial lights are sources: no glow geometry, one per-pixel source.
        const auto prepared = compile_render_plan(plan, resolver);
        REQUIRE(prepared);
        const auto evaluated = evaluate(
            prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
        REQUIRE(evaluated);
        CHECK(evaluated->scene.layers()[0].nodes.empty());
        REQUIRE(evaluated->scene.light_context().point_lights.size() == 1);
        // Default radius (256) is used as the height of a 2D light.
        CHECK(evaluated->scene.light_context().point_lights[0].position.z == doctest::Approx(256.0f));
    }
}

TEST_CASE("render plan 2D point light defaults its height to the light radius") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    assets::AssetResolver resolver;
    RenderPlan plan;
    plan.job_id = "point-light-height";
    plan.canvas = {.width = 128, .height = 96,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "key";
    layer.type = LayerType::Light;
    layer.position = {40.0f, 30.0f, 0.0f};
    layer.position_dimensions = 2;
    LightLayerPlan light;
    light.kind = LightKindPlan::Point;
    light.radius = 90.0f;
    layer.light = light;
    plan.layers.push_back(std::move(layer));
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.light_context().point_lights.size() == 1);
    const auto& source = evaluated->scene.light_context().point_lights[0];
    CHECK(source.position.x == doctest::Approx(40.0f));
    CHECK(source.position.y == doctest::Approx(30.0f));
    CHECK(source.position.z == doctest::Approx(90.0f));
}

TEST_CASE("RenderPlan forwards per-light shadow settings to scene lighting") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "cpu-light-shadow";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan caster;
    caster.id = "caster";
    caster.type = LayerType::Color;
    caster.color = {0.8f, 0.4f, 0.2f, 1.0f};
    caster.position = {80.0f, 40.0f, 20.0f};
    caster.position_dimensions = 3;
    // Casting and receiving are independent material policies. A caster that
    // opts out of receiving shadows still projects its own silhouette.
    caster.accepts_shadows = false;
    plan.layers.push_back(caster);
    LayerPlan no_shadow_caster = caster;
    no_shadow_caster.id = "shadow-opt-out";
    no_shadow_caster.position[0] = 140.0f;
    no_shadow_caster.casts_shadows = false;
    plan.layers.push_back(std::move(no_shadow_caster));
    LayerPlan light_layer;
    light_layer.id = "shadow-light";
    light_layer.type = LayerType::Light;
    light_layer.position = {20.0f, 10.0f, 100.0f};
    light_layer.position_dimensions = 3;
    LightLayerPlan light;
    light.kind = LightKindPlan::Point;
    light.cast_shadow = true;
    light.shadow_radius = 7.0f;
    light.shadow_bias = 0.5f;
    light_layer.light = light;
    plan.layers.push_back(std::move(light_layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 3);
    const auto& lights = evaluated->scene.light_context().point_lights;
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].casts_shadow);
    CHECK(lights[0].shadow_radius == doctest::Approx(7.0f));
    CHECK(lights[0].shadow_bias == doctest::Approx(0.5f));
    CHECK(evaluated->scene.layers()[0].effects().empty());
    CHECK(evaluated->scene.layers()[1].effects().empty());
}

TEST_CASE("RenderPlan source lights sample position and direction with deterministic random access") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    for (const auto kind : {LightKindPlan::Point, LightKindPlan::Spot, LightKindPlan::Directional}) {
        RenderPlan plan;
        plan.canvas = {.width = 320, .height = 180,
                       .fps = FrameRate{24, 1}, .duration = Frame{24}};
        LayerPlan layer;
        layer.id = "moving-light";
        layer.type = LayerType::Light;
        layer.position = {40.0f, 30.0f, 100.0f};
        layer.position_dimensions = 3;
        layer.light = LightLayerPlan{};
        layer.light->kind = kind;
        layer.animation = AnimationPlan{{
            AnimationTrackPlan{.property = "position_x",
                .keyframes = {{Frame{0}, {0.0f}}, {Frame{20}, {80.0f}}}},
            AnimationTrackPlan{.property = "light_direction",
                .keyframes = {{Frame{0}, {0.0f, 0.0f, -1.0f}},
                              {Frame{20}, {1.0f, 0.0f, -1.0f}}}}}};
        plan.layers.push_back(layer);
        assets::AssetResolver resolver;
        const auto prepared = compile_render_plan(plan, resolver);
        REQUIRE(prepared);
        for (const int frame : {20, 0, 10, 0, 20}) {
            const auto sampled = evaluate(prepared->compiled_composition,
                CompositionEvaluateContext{}, Frame{frame});
            REQUIRE(sampled);
            const auto& lights = sampled->scene.light_context().point_lights;
            REQUIRE(lights.size() == 1);
            CHECK(lights[0].position.x == doctest::Approx(40.0f + 4.0f * frame));
            CHECK(lights[0].position.y == doctest::Approx(30.0f));
            CHECK(lights[0].position.z == doctest::Approx(100.0f));
            CHECK(lights[0].direction.x == doctest::Approx(frame / 20.0f));
            CHECK(lights[0].direction.z == doctest::Approx(-1.0f));
        }
        // A vector position track is absolute, not an underscore-axis offset.
        plan.layers[0].animation->tracks[0] = AnimationTrackPlan{
            .property = "position",
            .keyframes = {{Frame{0}, {10.0f, 20.0f}}, {Frame{20}, {30.0f, 40.0f}}}};
        const auto vector_prepared = compile_render_plan(plan, resolver);
        REQUIRE(vector_prepared);
        const auto midpoint = evaluate(vector_prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{10});
        REQUIRE(midpoint);
        const auto& position = midpoint->scene.light_context().point_lights[0].position;
        CHECK(position.x == doctest::Approx(20.0f));
        CHECK(position.y == doctest::Approx(30.0f));
        CHECK(position.z == doctest::Approx(100.0f));
    }
}

TEST_CASE("RenderPlan light direction rejects an interpolated zero vector") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{24, 1}, .duration = Frame{24}};
    LayerPlan layer;
    layer.id = "opposite-directions";
    layer.type = LayerType::Light;
    layer.light = LightLayerPlan{};
    layer.light->kind = LightKindPlan::Directional;
    layer.animation = AnimationPlan{{AnimationTrackPlan{
        .property = "light_direction",
        .keyframes = {{Frame{0}, {0.0f, 0.0f, -1.0f}},
                      {Frame{20}, {0.0f, 0.0f, 1.0f}}}}}};
    plan.layers.push_back(layer);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto midpoint = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{10});
    REQUIRE_FALSE(midpoint);
    CHECK(midpoint.error().message.find("finite and non-zero") != std::string::npos);
    CHECK(evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0}));
    CHECK(evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{20}));
}

TEST_CASE("RenderPlan light intensity and color tracks sample on requested frames") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-render-plan-light";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{24}};
    LayerPlan layer;
    layer.id = "animated-light";
    layer.type = LayerType::Light;
    LightLayerPlan light;
    light.intensity = 1.0f;
    light.color = {1.0f, 1.0f, 1.0f, 1.0f};
    layer.light = light;
    layer.animation = AnimationPlan{{
        AnimationTrackPlan{.property = "light_intensity",
            .keyframes = {{Frame{0}, {0.5f}}, {Frame{23}, {1.5f}}}},
        AnimationTrackPlan{.property = "light_color",
            .keyframes = {{Frame{0}, {1.0f, 0.4f, 0.2f, 1.0f}},
                          {Frame{23}, {0.2f, 0.5f, 1.0f, 1.0f}}}}}};
    layer.blend_mode = BlendMode::Screen;
    plan.layers.push_back(std::move(layer));
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto end = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{23});
    REQUIRE(start);
    REQUIRE(end);
    const auto& start_color = start->scene.layers()[0].nodes[0].color;
    const auto& end_color = end->scene.layers()[0].nodes[0].color;
    CHECK(start_color.r == doctest::Approx(0.5f));
    CHECK(start_color.g == doctest::Approx(0.2f));
    CHECK(start_color.b == doctest::Approx(0.1f));
    CHECK(end_color.r == doctest::Approx(0.3f));
    CHECK(end_color.g == doctest::Approx(0.75f));
    CHECK(end_color.b == doctest::Approx(1.5f));
}

TEST_CASE("render plan lowers fractal noise to the generate effect stack") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "procedural-background";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{24, 1}, .duration = Frame{24}};
    LayerPlan layer;
    layer.id = "background";
    layer.type = LayerType::Color;
    layer.color = {0.0f, 0.0f, 0.0f, 1.0f};
    layer.effects.push_back({
        .kind = LayerPlan::EffectKindPlan::FractalNoise,
        .seed = 7, .rgb_noise = true, .octaves = 5,
        .frequency = 0.03f, .amplitude = 0.9f,
        .evolution = 0.5f, .contrast = 1.1f, .brightness = -0.1f,
        });
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 1);
    const auto* noise = std::get_if<FractalNoiseParams>(&effects[0].params);
    REQUIRE(noise);
    CHECK(noise->seed == 7);
    CHECK(noise->octaves == 5);
    CHECK(noise->evolution == doctest::Approx(0.5f));
    CHECK(noise->color_mode == NoiseColorMode::RGB);
}

TEST_CASE("render plan lowers LightRays parameters into the effect stack") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "light-rays-plan";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "rays";
    layer.type = LayerType::Color;
    layer.color = {0.0f, 0.0f, 0.0f, 1.0f};
    LayerPlan::EffectPlan rays;
    rays.kind = LayerPlan::EffectKindPlan::LightRays;
    rays.light_rays_origin = {0.2f, 0.7f};
    rays.light_rays_length = 240.0f;
    rays.light_rays_density = 1.25f;
    rays.light_rays_color = {0.9f, 0.8f, 0.6f, 0.75f};
    rays.light_rays_decay = 0.85f;
    layer.effects.push_back(rays);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 1);
    const auto* decoded = std::get_if<LightRaysParams>(&effects[0].params);
    REQUIRE(decoded);
    CHECK(decoded->origin.x == doctest::Approx(0.2f));
    CHECK(decoded->origin.y == doctest::Approx(0.7f));
    CHECK(decoded->length == doctest::Approx(240.0f));
    CHECK(decoded->density == doctest::Approx(1.25f));
    CHECK(decoded->color.r == doctest::Approx(0.9f));
    CHECK(decoded->color.a == doctest::Approx(0.75f));
    CHECK(decoded->decay == doctest::Approx(0.85f));
}

TEST_CASE("render plan adjustment layers reject drawable and compositor state in direct plans") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const auto rejected = [](const LayerPlan& invalid_layer) {
        RenderPlan plan;
        plan.job_id = "invalid-adjustment";
        plan.canvas = {.width = 16, .height = 16,
                       .fps = FrameRate{24, 1}, .duration = Frame{1}};
        LayerPlan base;
        base.id = "base";
        base.type = LayerType::Color;
        base.color = {0, 0, 0, 1};
        plan.layers.push_back(base);
        plan.layers.push_back(invalid_layer);
        assets::AssetResolver resolver;
        return compile_render_plan(plan, resolver);
    };
    LayerPlan valid;
    valid.id = "adjustment";
    valid.type = LayerType::Adjustment;
    valid.effects.push_back({.kind = LayerPlan::EffectKindPlan::Noise});
    REQUIRE(rejected(valid));

    auto with_opacity = valid;
    with_opacity.opacity = 1.0f;
    CHECK_FALSE(rejected(with_opacity));
    auto with_position = valid;
    with_position.position = {1.0f, 0.0f, 0.0f};
    with_position.position_dimensions = 2;
    CHECK_FALSE(rejected(with_position));
    auto with_compositing = valid;
    with_compositing.blend_mode = BlendMode::Normal;
    CHECK_FALSE(rejected(with_compositing));
    auto without_effect = valid;
    without_effect.effects.clear();
    CHECK_FALSE(rejected(without_effect));
}

TEST_CASE("render plan adjustment layer applies grain without drawing content") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "background-grain-adjustment";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan background;
    background.id = "background";
    background.type = LayerType::Color;
    background.color = {0.1f, 0.15f, 0.2f, 1.0f};
    plan.layers.push_back(std::move(background));
    LayerPlan adjustment;
    adjustment.id = "film-grain";
    adjustment.type = LayerType::Adjustment;
    adjustment.effects.push_back({
        .kind = LayerPlan::EffectKindPlan::Noise,
        .amount = 0.05f, .seed = 123, .animated = true,
        .noise_size = 1.5f});
    plan.layers.push_back(std::move(adjustment));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 2);
    CHECK(evaluated->scene.layers()[1].kind == LayerKind::Adjustment);
    CHECK(evaluated->scene.layers()[1].nodes.empty());
    const auto& effects = evaluated->scene.layers()[1].effects();
    REQUIRE(effects.size() == 1);
    const auto* noise = std::get_if<NoiseParams>(&effects[0].params);
    REQUIRE(noise);
    CHECK(noise->seed == 123);
    CHECK(noise->animated);
    CHECK(noise->size == doctest::Approx(1.5f));
}

TEST_CASE("render plan prepares and lowers content-addressed 3D LUT assets") {
    FingerprintTempDir temp;
    std::filesystem::create_directories(temp.path / "looks");
    {
        std::ofstream cube(temp.path / "looks/invert.cube");
        cube << "LUT_3D_SIZE 2\n"
             << "1 1 1\n0 1 1\n1 0 1\n0 0 1\n"
             << "1 1 0\n0 1 0\n1 0 0\n0 0 0\n";
    }
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "lut-plan";
    plan.canvas = {.width = 16, .height = 16,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{1}};
    chronon3d::render_plan::LayerPlan color;
    color.id = "graded";
    color.type = chronon3d::render_plan::LayerType::Color;
    color.color = {0.2f, 0.3f, 0.4f, 1.0f};
    color.effects.push_back({.kind = chronon3d::render_plan::LayerPlan::EffectKindPlan::Lut3D,
                             .asset = "looks/invert.cube", .amount = 1.0f});
    plan.layers.push_back(std::move(color));
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(temp.path);
    const auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    REQUIRE(prepared->compiled_composition.asset_manifest);
    CHECK(prepared->compiled_composition.asset_manifest->assets().size() == 1);
    const auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    CHECK(evaluated->scene.layers()[0].effects().size() == 1);
}

TEST_CASE("render plan lowers luma/chroma key parameters into the effect stack") {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "keying-plan";
    plan.canvas = {.width = 16, .height = 16,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{1}};
    chronon3d::render_plan::LayerPlan color;
    color.id = "keyed";
    color.type = chronon3d::render_plan::LayerType::Color;
    color.color = {0.2f, 0.7f, 0.1f, 1.0f};
    color.effects.push_back({
        .kind = chronon3d::render_plan::LayerPlan::EffectKindPlan::Keying,
        .keying_mode = chronon3d::render_plan::LayerPlan::KeyingModePlan::Chroma,
        .key_color = {0.0f, 0.75f, 0.0f}, .threshold = 0.18f,
        .softness = 0.12f, .spill = 0.6f});
    plan.layers.push_back(std::move(color));
    chronon3d::assets::AssetResolver resolver;
    const auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 1);
    const auto* keying = std::get_if<chronon3d::KeyingParams>(&effects[0].params);
    REQUIRE(keying);
    CHECK(keying->mode == chronon3d::KeyingMode::Chroma);
    CHECK(keying->threshold == doctest::Approx(0.18f));
    CHECK(keying->softness == doctest::Approx(0.12f));
    CHECK(keying->spill == doctest::Approx(0.6f));
}

TEST_CASE("compiled composition preserves fractional evaluation time") {
    double observed_frame = -1.0;
    chronon3d::CompositionDefinition definition;
    definition.composition = {
        .name = "fractional-evaluation",
        .width = 320,
        .height = 180,
        .frame_rate = {24, 1},
        .duration = chronon3d::Frame{10}};
    definition.scene = [&observed_frame](const chronon3d::FrameContext& context) {
        observed_frame = context.effective_frame();
        return chronon3d::Scene{};
    };

    // Canonical path: the deprecated compile_composition(CompositionDefinition)
    // overload was removed — project onto a Composition first.
    const chronon3d::Composition definition_composition(
        definition.composition, definition.scene,
        definition.scene_content_fingerprint);
    const auto compiled =
        chronon3d::compile_composition(definition_composition, {});
    REQUIRE(compiled);

    const auto base_context = chronon3d::make_frame_context({
        .global_time = chronon3d::SampleTime::from_frame_int(
            chronon3d::Frame{0}, {24, 1}),
        .duration = chronon3d::Frame{10},
        .width = 320,
        .height = 180});
    const auto evaluated = chronon3d::evaluate(
        compiled.value(),
        chronon3d::CompositionEvaluateContext{.frame_context = base_context},
        chronon3d::SampleTime::from_frame(2.5, {24, 1}));

    REQUIRE(evaluated);
    CHECK(observed_frame == doctest::Approx(2.5));
}
