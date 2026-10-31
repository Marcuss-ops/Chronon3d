#include "test_prepared_render_plan_test_support.hpp"

TEST_CASE("prepared fingerprint is stable across asset roots and output metadata") {
    FingerprintTempDir temp;
    const auto root_a = temp.path / "root_a";
    const auto root_b = temp.path / "root_b";
    write_asset(root_a, "same bytes");
    write_asset(root_b, "same bytes");

    auto plan_a = image_plan();
    auto plan_b = plan_a;
    plan_b.job_id = "different-job-id";
    plan_b.output.path = "/different/machine/output.png";
    chronon3d::assets::AssetResolver resolver_a;
    chronon3d::assets::AssetResolver resolver_b;
    resolver_a.mount(root_a);
    resolver_b.mount(root_b);

    const auto first = chronon3d::render_plan::compile_render_plan(plan_a, resolver_a);
    const auto second = chronon3d::render_plan::compile_render_plan(plan_b, resolver_b);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->fingerprint.content_digest == second->fingerprint.content_digest);
    CHECK(first->fingerprint.request_digest == second->fingerprint.request_digest);
}

TEST_CASE("prepared fingerprint changes when asset bytes change") {
    FingerprintTempDir temp;
    const auto root = temp.path / "root";
    write_asset(root, "original bytes");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(root);
    const auto original = chronon3d::render_plan::compile_render_plan(
        image_plan(), resolver);
    REQUIRE(original);

    write_asset(root, "modified bytes with different size");
    const auto modified = chronon3d::render_plan::compile_render_plan(
        image_plan(), resolver);
    REQUIRE(modified);
    CHECK(original->fingerprint.content_digest != modified->fingerprint.content_digest);
    CHECK(original->fingerprint.request_digest != modified->fingerprint.request_digest);
}

// ── FP-1: one job identity, two entry points ────────────────────────────────
//
// `compiler::compile` (the canonical compiler) and `compile_render_plan` (the
// plan/prepared path) must derive the SAME content/request digest for the same
// plan + assets + options. If they could differ, a program/binary cache keyed
// on the digest would be able to hit a program compiled for a non-equivalent
// job — a correctness bug, not a performance one. Before the FP-1 fix these
// were two independent implementations of the same hash sequence, kept in sync
// only by review.

TEST_CASE("the compiler path and the plan path agree on the job fingerprint") {
    FingerprintTempDir temp;
    const auto root = temp.path / "fp1_root";
    write_asset(root, "same bytes");

    auto plan = image_plan();
    plan.output.path = "fp1-job.mp4";
    plan.output.codec = chronon3d::render_plan::VideoCodec::H264;
    plan.output.rate_control = chronon3d::render_plan::RateControlMode::Bitrate;
    plan.output.bitrate = 2'000'000;

    chronon3d::render_plan::RenderPlanFingerprintOptions options;
    options.render_settings.deterministic = true;
    options.render_settings.antialiasing_samples = 2;

    chronon3d::assets::AssetResolver compiler_resolver;
    compiler_resolver.mount(root);
    const auto compiled = chronon3d::compiler::compile(
        plan, compiler_resolver,
        chronon3d::compiler::CompileOptions{.fingerprint = options});
    REQUIRE(compiled);
    const auto& program = compiled.value();
    // One program, one identity: the metadata view and the fingerprint view of
    // the same digests must not drift apart either.
    CHECK(program.fingerprint_data().content_digest ==
          program.metadata().content_digest);
    CHECK(program.fingerprint_data().request_digest ==
          program.metadata().request_digest);

    chronon3d::assets::AssetResolver plan_resolver;
    plan_resolver.mount(root);
    const auto prepared =
        chronon3d::render_plan::compile_render_plan(plan, plan_resolver, options);
    REQUIRE(prepared);

    CHECK(prepared->fingerprint.content_digest ==
          program.fingerprint_data().content_digest);
    CHECK(prepared->fingerprint.request_digest ==
          program.fingerprint_data().request_digest);
}

TEST_CASE("an unset render-settings dimension inherits the canvas on both paths") {
    FingerprintTempDir temp;
    const auto root = temp.path / "fp1_canvas_root";
    write_asset(root, "same bytes");

    const auto plan = image_plan(); // canvas 320x180
    chronon3d::render_plan::RenderPlanFingerprintOptions unset;
    unset.render_settings.width = 0;
    unset.render_settings.height = 0;

    auto inherited = unset;
    inherited.render_settings.width = plan.canvas.width;
    inherited.render_settings.height = plan.canvas.height;

    chronon3d::assets::AssetResolver resolver_a;
    resolver_a.mount(root);
    const auto inherited_prepared = chronon3d::render_plan::compile_render_plan(
        plan, resolver_a, inherited);
    REQUIRE(inherited_prepared);

    chronon3d::assets::AssetResolver resolver_b;
    resolver_b.mount(root);
    const auto unset_prepared = chronon3d::render_plan::compile_render_plan(
        plan, resolver_b, unset);
    REQUIRE(unset_prepared);

    CHECK(unset_prepared->fingerprint.content_digest ==
          inherited_prepared->fingerprint.content_digest);
    CHECK(unset_prepared->fingerprint.request_digest ==
          inherited_prepared->fingerprint.request_digest);

    // The same rule has to hold on the compiler path, which is exactly where
    // the wording of the default used to live alone.
    chronon3d::assets::AssetResolver resolver_c;
    resolver_c.mount(root);
    const auto compiled = chronon3d::compiler::compile(
        plan, resolver_c,
        chronon3d::compiler::CompileOptions{.fingerprint = unset});
    REQUIRE(compiled);
    CHECK(compiled.value().fingerprint_data().content_digest ==
          unset_prepared->fingerprint.content_digest);
    CHECK(compiled.value().fingerprint_data().request_digest ==
          unset_prepared->fingerprint.request_digest);
}

// ── Subtitle style parity (resolve_subtitle_style) ──────────────────────────
//
// The SubtitleTrack compiler consumes layer.style with EXACTLY the semantics
// of the text materializer: shared parse_hex_color (color_utils.hpp), same
// shadow defaults (opacity 1.0 / blur 0.0 / offset (0,0) when absent), same
// "absent → keep defaults" contract. These tests lock that parity.
//
// P0-2 (TICKET-TEXT-QUALITY-V2): the shared parser is the single
// authoring/lowering boundary and emits Chronon's LINEAR working domain. A
// CSS hex literal is sRGB, so it is converted exactly once here (see the
// COLOR DOMAIN note in color_utils.hpp). No text backend re-linearizes.

namespace {

void check_color(const chronon3d::Color& got,
                 float r, float g, float b, float a) {
    CHECK(got.r == doctest::Approx(r).epsilon(0.0001f));
    CHECK(got.g == doctest::Approx(g).epsilon(0.0001f));
    CHECK(got.b == doctest::Approx(b).epsilon(0.0001f));
    CHECK(got.a == doctest::Approx(a).epsilon(0.0001f));
}

} // namespace

TEST_CASE("resolve_subtitle_style lowers fill with text-materializer hex semantics") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "#FFC107";

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.fill.has_value());
    // #FFC107 is CSS sRGB; the lowered Color lives in the linear working
    // domain, so the expectation is the canonical sRGB→linear projection.
    const chronon3d::Color expected =
        chronon3d::Color{1.0f, 193.0f / 255.0f, 7.0f / 255.0f, 1.0f}.to_linear();
    check_color(*resolved.fill, expected.r, expected.g, expected.b, expected.a);
    // Pin one explicit value so a silent change to the conversion is caught
    // even if to_linear() itself drifts: linear(#C1) = 0.53326.
    CHECK(resolved.fill->g == doctest::Approx(0.53326f).epsilon(0.0002f));
    // ... and the encoded channel must NOT survive to the consumer.
    CHECK(resolved.fill->g != doctest::Approx(193.0f / 255.0f).epsilon(0.0001f));
    CHECK_FALSE(resolved.shadow.has_value());
    CHECK_FALSE(resolved.font_size.has_value());
}

// P0-2 gate: mid-gray is the smallest fixture that exposes an sRGB/linear
// mix-up on BOTH text authoring paths.  CSS #808080 is sRGB 0.501961; the
// linear working-domain value is 0.215861.  If a future change re-encodes the
// boundary, the two are easy to confuse and this test fails loudly.
TEST_CASE("parse_hex_color lowers CSS sRGB hex to the linear working domain") {
    const auto gray = chronon3d::render_plan::parse_hex_color("#808080");
    REQUIRE(gray.has_value());
    check_color(*gray, 0.2158605f, 0.2158605f, 0.2158605f, 1.0f);
    CHECK(gray->r != doctest::Approx(128.0f / 255.0f).epsilon(0.0001f));

    // Alpha is a coverage factor, not a channel: it survives untouched.
    const auto half = chronon3d::render_plan::parse_hex_color("#808080", 0.5f);
    REQUIRE(half.has_value());
    check_color(*half, 0.2158605f, 0.2158605f, 0.2158605f, 0.5f);

    // Fixed points of the conversion stay exact and keep every existing
    // white-fill / black-shadow production plan byte-stable.
    const auto white = chronon3d::render_plan::parse_hex_color("#FFFFFF");
    const auto black = chronon3d::render_plan::parse_hex_color("#000000");
    REQUIRE(white.has_value());
    REQUIRE(black.has_value());
    check_color(*white, 1.0f, 1.0f, 1.0f, 1.0f);
    check_color(*black, 0.0f, 0.0f, 0.0f, 1.0f);
}

TEST_CASE("resolve_subtitle_style lowers mid-gray to the linear domain") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "#808080";

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.fill.has_value());
    check_color(*resolved.fill, 0.2158605f, 0.2158605f, 0.2158605f, 1.0f);
}

TEST_CASE("resolve_subtitle_style keeps defaults when fill is unusable") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "not-a-hex";  // same guard the text materializer applies

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    CHECK_FALSE(resolved.fill.has_value());
}

TEST_CASE("resolve_subtitle_style lowers shadow with materializer defaults") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "#FFFFFF";
    chronon3d::render_plan::ShadowStyle shadow;
    shadow.color = "#000000";
    shadow.opacity = 0.6f;
    shadow.blur = 14.0f;
    shadow.offset = {0.0f, 8.0f};
    shadow.offset_dimensions = 2;
    style.shadow = shadow;

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.shadow.has_value());
    const auto& got = *resolved.shadow;
    CHECK(got.enabled);
    check_color(got.color, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(got.opacity == doctest::Approx(0.6f));
    CHECK(got.blur == doctest::Approx(14.0f));
    CHECK(got.offset.x == doctest::Approx(0.0f));
    CHECK(got.offset.y == doctest::Approx(8.0f));
}

TEST_CASE("resolve_subtitle_style shadow defaults match the text materializer") {
    chronon3d::render_plan::LayerStylePlan style;
    chronon3d::render_plan::ShadowStyle shadow;
    shadow.color = "#000000";  // no opacity / blur / offset declared
    style.shadow = shadow;

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.shadow.has_value());
    const auto& got = *resolved.shadow;
    CHECK(got.enabled);
    CHECK(got.opacity == doctest::Approx(1.0f));  // absent → 1.0
    CHECK(got.blur == doctest::Approx(0.0f));     // absent → 0.0
    CHECK(got.offset.x == doctest::Approx(0.0f));
    CHECK(got.offset.y == doctest::Approx(0.0f));
}

TEST_CASE("resolve_subtitle_style lowers font_size when positive and absent otherwise") {
    chronon3d::render_plan::LayerStylePlan style;
    style.font_size = 54.0f;
    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.font_size.has_value());
    CHECK(*resolved.font_size == doctest::Approx(54.0f));

    chronon3d::render_plan::LayerStylePlan empty_style;
    const auto empty = chronon3d::render_plan::resolve_subtitle_style(empty_style);
    CHECK_FALSE(empty.font_size.has_value());
    CHECK_FALSE(empty.fill.has_value());
    CHECK_FALSE(empty.shadow.has_value());
}

TEST_CASE("prepared fingerprint includes schema, engine, and render settings") {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "fingerprint-settings";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::RenderPlanFingerprintOptions base;
    const auto baseline = chronon3d::render_plan::compile_render_plan(
        plan, resolver, base);
    REQUIRE(baseline);

    auto schema_changed = base;
    schema_changed.schema_version = "chronon.render-plan.v3";
    auto engine_changed = base;
    engine_changed.engine_compatibility_version = "chronon3d.engine.v2";
    auto settings_changed = base;
    settings_changed.render_settings.antialiasing_samples = 4;
    settings_changed.render_settings.deterministic = true;

    const auto schema = chronon3d::render_plan::compile_render_plan(
        plan, resolver, schema_changed);
    const auto engine = chronon3d::render_plan::compile_render_plan(
        plan, resolver, engine_changed);
    const auto settings = chronon3d::render_plan::compile_render_plan(
        plan, resolver, settings_changed);
    REQUIRE(schema);
    REQUIRE(engine);
    REQUIRE(settings);
    CHECK(baseline->fingerprint.content_digest != schema->fingerprint.content_digest);
    CHECK(baseline->fingerprint.content_digest != engine->fingerprint.content_digest);
    CHECK(baseline->fingerprint.content_digest != settings->fingerprint.content_digest);
}

TEST_CASE("render_plan_compiler: animation tracks map to animated transforms") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "anim_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 0.0f, 0.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track;
    track.property = "opacity";
    track.easing = "bezier";
    track.bezier_curve = std::array<float, 4>{0.16f, 1.0f, 0.3f, 1.0f};
    track.keyframes.push_back({chronon3d::Frame{0}, {0.0f}});
    track.keyframes.push_back({chronon3d::Frame{15}, {1.0f}});
    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_anim";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{},
        chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated.value().scene.layers().size() == 1u);
    CHECK(evaluated.value().scene.layers().front().anim_transform.opacity.is_animated());
}

TEST_CASE("render_plan_compiler: scalar temporal handles and AutoBezier lower to AnimatedValue") {
    using namespace chronon3d;
    assets::AssetResolver resolver;
    render_plan::LayerPlan layer;
    layer.id = "scalar-timing";
    layer.type = render_plan::LayerType::Color;
    layer.color = {1, 1, 1, 1};
    render_plan::AnimationTrackPlan track;
    track.property = "opacity";
    render_plan::AnimationKeyframePlan start;
    start.frame = Frame{0};
    start.value = {0.0f};
    start.interpolation = "auto_bezier";
    render_plan::AnimationKeyframePlan middle;
    middle.frame = Frame{10};
    middle.value = {0.5f};
    middle.roving = true;
    render_plan::AnimationKeyframePlan end;
    end.frame = Frame{20};
    end.value = {1.0f};
    track.keyframes = {start, middle, end};
    layer.animation = render_plan::AnimationPlan{.tracks = {track}};
    render_plan::RenderPlan plan;
    plan.job_id = "scalar-timing";
    plan.canvas = {.width = 320, .height = 180, .fps = FrameRate{30, 1},
                   .duration = Frame{20}};
    plan.layers.push_back(std::move(layer));
    auto prepared = render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto evaluated = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{5});
    REQUIRE(evaluated);
    const auto& opacity = evaluated.value().scene.layers().front().anim_transform.opacity;
    REQUIRE(opacity.keyframes().size() == 3);
    CHECK(opacity.keyframes()[0].interp == InterpMode::AutoBezier);
    CHECK(opacity.keyframes()[1].roving);
    CHECK(opacity.keyframes()[1].frame == Frame{10});
}

TEST_CASE("render_plan_compiler: position_x and position_y merge correctly (diagonal drift)") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "drift_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track_x;
    track_x.property = "position_x";
    track_x.easing = "linear";
    track_x.keyframes.push_back({chronon3d::Frame{0}, {0.0f}});
    track_x.keyframes.push_back({chronon3d::Frame{30}, {100.0f}});

    chronon3d::render_plan::AnimationTrackPlan track_y;
    track_y.property = "position_y";
    track_y.easing = "linear";
    track_y.keyframes.push_back({chronon3d::Frame{0}, {0.0f}});
    track_y.keyframes.push_back({chronon3d::Frame{30}, {200.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track_x), std::move(track_y)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_drift";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);

    auto eval0 = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    REQUIRE(eval0);
    CHECK(eval0.value().scene.layers().front().anim_transform.position.is_animated());
}

TEST_CASE("render_plan_compiler: rejects mixing position with component tracks") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "bad_mix_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track_full;
    track_full.property = "position";
    track_full.keyframes.push_back({chronon3d::Frame{0}, {0.0f, 0.0f, 0.0f}});

    chronon3d::render_plan::AnimationTrackPlan track_x;
    track_x.property = "position_x";
    track_x.keyframes.push_back({chronon3d::Frame{0}, {50.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track_full), std::move(track_x)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_bad_mix";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto eval = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    CHECK(!eval);
    CHECK(eval.error().message.find("mixes 'position' with component tracks") != std::string::npos);
}

TEST_CASE("render_plan_compiler: rejects duplicate component tracks") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "dup_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track_x1;
    track_x1.property = "position_x";
    track_x1.keyframes.push_back({chronon3d::Frame{0}, {10.0f}});

    chronon3d::render_plan::AnimationTrackPlan track_x2;
    track_x2.property = "position_x";
    track_x2.keyframes.push_back({chronon3d::Frame{0}, {20.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track_x1), std::move(track_x2)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_dup";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto eval = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    CHECK(!eval);
    CHECK(eval.error().message.find("duplicate position_x tracks") != std::string::npos);
}

TEST_CASE("render_plan_compiler: rotation_x and rotation_y merge correctly (dual gimbal)") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "gimbal_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan rot_x;
    rot_x.property = "rotation_x";
    rot_x.keyframes.push_back({chronon3d::Frame{0}, {10.0f}});
    rot_x.keyframes.push_back({chronon3d::Frame{30}, {20.0f}});

    chronon3d::render_plan::AnimationTrackPlan rot_y;
    rot_y.property = "rotation_y";
    rot_y.keyframes.push_back({chronon3d::Frame{0}, {-5.0f}});
    rot_y.keyframes.push_back({chronon3d::Frame{30}, {5.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(rot_x), std::move(rot_y)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_gimbal";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto eval0 = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    REQUIRE(eval0);
    CHECK(eval0.value().scene.layers().front().anim_transform.rotation_euler.is_animated());
}

TEST_CASE("render plan flow layout reaches the runtime scene configuration") {
    using namespace chronon3d;
    assets::AssetResolver resolver;
    render_plan::RenderPlan plan;
    plan.job_id="flow-runtime";
    plan.canvas={.width=600,.height=300,.fps=FrameRate{30,1},.duration=Frame{10}};
    for (int i=0;i<3;++i) {
        render_plan::LayerPlan layer;
        layer.id="flow-"+std::to_string(i); layer.type=render_plan::LayerType::Color;
        layer.color={1,1,1,1};
        layer.flow_layout=render_plan::LayerPlan::FlowLayoutPlan{
            .group_id="cards",.axis="row",.align="center",.justify="center",
            .gap=12,.wrap=false,.cell_size={100,80}};
        plan.layers.push_back(std::move(layer));
    }
    auto prepared=render_plan::compile_render_plan(plan,resolver);
    REQUIRE(prepared);
    auto evaluated=evaluate(prepared->compiled_composition,{},Frame{0});
    REQUIRE(evaluated);
    const auto& layers=evaluated.value().scene.layers();
    REQUIRE(layers.size()==3);
    CHECK(layers[0].layout.flow.has_value());
    CHECK(layers[1].layout.flow->group_id=="cards");
    CHECK(layers[0].layout.flow->gap == doctest::Approx(12.0f));
    CHECK(layers[0].layout.flow->cell_size.x == doctest::Approx(100.0f));
    CHECK(layers[0].layout.flow->cell_size.y == doctest::Approx(80.0f));

    auto changed=plan;
    changed.layers[0].flow_layout->gap=20;
    auto changed_prepared=compile_render_plan(changed,resolver);
    REQUIRE(changed_prepared);
    CHECK(changed_prepared->fingerprint.content_digest != prepared->fingerprint.content_digest);
}

TEST_CASE("runtime camera provider samples exact fractional frames and fingerprints its source") {
    using namespace chronon3d;
    assets::AssetResolver resolver;
    render_plan::RenderPlan plan;
    plan.job_id = "runtime-camera-provider";
    plan.canvas = {.width = 64, .height = 64, .fps = FrameRate{30, 1},
                   .duration = Frame{2}};
    plan.camera = render_plan::CameraPlan{};
    render_plan::LayerPlan layer;
    layer.id = "camera-subject";
    layer.type = render_plan::LayerType::Color;
    layer.color = {1.f, 1.f, 1.f, 1.f};
    plan.layers.push_back(layer);

    double sampled_frame = -1.0;
    auto sample = [&](double frame) {
        sampled_frame = frame;
        auto camera = *plan.camera;
        camera.position[0] = static_cast<float>(frame * 4.0);
        return camera;
    };
    render_plan::RuntimeCameraSampleProvider provider{
        .fingerprint = "cm3d-rig-test-a", .sample = sample};
    const auto prepared = render_plan::compile_render_plan(
        plan, resolver, {}, nullptr, &provider);
    REQUIRE(prepared);

    const auto evaluated = evaluate(prepared->compiled_composition, {},
        SampleTime::from_frame(0.5, FrameRate{30, 1}));
    REQUIRE(evaluated);
    CHECK(sampled_frame == doctest::Approx(0.5));
    CHECK(evaluated->scene.camera_2_5d().position.x == doctest::Approx(2.f));
    CHECK(evaluated->scene.camera_2_5d().is_animated);

    const auto preRoll = evaluate(prepared->compiled_composition, {},
        SampleTime::from_frame(-0.25, FrameRate{30, 1}));
    REQUIRE(preRoll);
    CHECK(sampled_frame == doctest::Approx(-0.25));
    CHECK(preRoll->scene.camera_2_5d().position.x == doctest::Approx(-1.f));

    provider.fingerprint = "cm3d-rig-test-b";
    const auto other_source = render_plan::compile_render_plan(
        plan, resolver, {}, nullptr, &provider);
    REQUIRE(other_source);
    CHECK(other_source->fingerprint.content_digest != prepared->fingerprint.content_digest);
    CHECK(other_source->composition->scene_content_fingerprint() !=
          prepared->composition->scene_content_fingerprint());

    auto conflicting_plan = plan;
    conflicting_plan.camera_samples = std::vector<render_plan::CameraSamplePlan>{{0.0, {}}};
    const auto conflicting = render_plan::compile_render_plan(
        conflicting_plan, resolver, {}, nullptr, &provider);
    REQUIRE_FALSE(conflicting);
    CHECK(conflicting.error().code == "runtime_camera_conflicts_with_samples");
}

TEST_CASE("offline interpolated camera samples match exact runtime sampling at shutter sub-frames") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;

    // G6 sub-frame parity fixture: a linear CameraRig segment between two
    // adjacent integer-frame samples must resolve to the same renderer camera
    // whether it is reconstructed by the offline-sample interpolation path or
    // requested through the runtime CM3D sample provider at a shutter
    // sub-frame time.
    RenderPlan plan;
    assets::AssetResolver resolver;
    plan.job_id = "camera-subframe-parity";
    plan.canvas = {.width = 64, .height = 64, .fps = FrameRate{30, 1},
                   .duration = Frame{4}};
    plan.camera = CameraPlan{};
    LayerPlan layer;
    layer.id = "camera-subject";
    layer.type = LayerType::Color;
    layer.color = {1.f, 1.f, 1.f, 1.f};
    plan.layers.push_back(layer);

    // Complete resolved poses baked at integer presentation frames, as CM3D's
    // bakeResolvedCameraSamples emits them inside one linear camera segment.
    constexpr float kStartX = 0.0f;
    constexpr float kSpanX = 120.0f;
    auto sample_camera = [](const double frame) {
        CameraPlan resolved;
        resolved.position = {static_cast<float>(frame * kSpanX), 40.0f, -1000.0f};
        resolved.rotation_deg = {0.0f, static_cast<float>(frame * 2.0), 0.0f};
        resolved.fov_deg = static_cast<float>(50.0 - frame);
        resolved.focal_length_mm = static_cast<float>(50.0 + frame * 2.0);
        resolved.sensor_width_mm = static_cast<float>(36.0 + frame * 0.5);
        resolved.f_stop = static_cast<float>(2.0 + frame * 0.1);
        resolved.lens_shift = {static_cast<float>(frame * 0.01),
                               static_cast<float>(frame * -0.005)};
        resolved.dof_enabled = true;
        resolved.dof_focus_distance = static_cast<float>(1000.0 + frame * 100.0);
        resolved.dof_highlight_gain = static_cast<float>(frame * 0.1);
        return resolved;
    };
    plan.camera_samples = std::vector<CameraSamplePlan>{
        {0.0, sample_camera(0.0)},
        {1.0, sample_camera(1.0)},
        {4.0, sample_camera(4.0)}};

    const auto offline = compile_render_plan(plan, resolver);
    REQUIRE(offline);

    auto runtime_plan = plan;
    runtime_plan.camera_samples.reset();
    RuntimeCameraSampleProvider provider{
        .fingerprint = "cm3d-subframe-parity-linear",
        .sample = [=](double frame) {
            // Exact CM3D sampling of the same linear segment.
            return sample_camera(frame);
        }};
    const auto runtime = compile_render_plan(runtime_plan, resolver, {}, nullptr, &provider);
    REQUIRE(runtime);
    // Provider identity must not nonce the offline bake; both compile from the
    // same plan body otherwise.
    CHECK(runtime->fingerprint.content_digest != offline->fingerprint.content_digest);

    for (const double shutter_frame : {0.25, 0.5, 0.75, 1.25, 2.5, 3.25}) {
        const auto offline_eval = evaluate(offline->compiled_composition, {},
            SampleTime::from_frame(shutter_frame, FrameRate{30, 1}));
        REQUIRE(offline_eval);
        const auto runtime_eval = evaluate(runtime->compiled_composition, {},
            SampleTime::from_frame(shutter_frame, FrameRate{30, 1}));
        REQUIRE(runtime_eval);

        const auto& offline_camera = offline_eval->scene.camera_2_5d();
        const auto& runtime_camera = runtime_eval->scene.camera_2_5d();
        const float expected_x = static_cast<float>(kStartX + shutter_frame * kSpanX);
        const float expected_yaw = static_cast<float>(shutter_frame * 2.0);
        const float expected_fov = static_cast<float>(50.0 - shutter_frame);
        const float expected_focal = static_cast<float>(50.0 + shutter_frame * 2.0);
        const float expected_sensor = static_cast<float>(36.0 + shutter_frame * 0.5);
        const float expected_f_stop = static_cast<float>(2.0 + shutter_frame * 0.1);
        const float expected_shift_x = static_cast<float>(shutter_frame * 0.01);
        const float expected_shift_y = static_cast<float>(shutter_frame * -0.005);
        const float expected_focus = static_cast<float>(1000.0 + shutter_frame * 100.0);
        const float expected_highlight = static_cast<float>(shutter_frame * 0.1);
        // Exact CM3D sampling.
        CHECK(runtime_camera.position.x == doctest::Approx(expected_x).epsilon(1.0e-4));
        CHECK(runtime_camera.rotation.y == doctest::Approx(expected_yaw).epsilon(1.0e-4));
        CHECK(runtime_camera.fov_deg == doctest::Approx(expected_fov).epsilon(1.0e-4));
        CHECK(runtime_camera.lens.focal_length == doctest::Approx(expected_focal).epsilon(1.0e-4));
        CHECK(runtime_camera.lens.sensor_width == doctest::Approx(expected_sensor).epsilon(1.0e-4));
        CHECK(runtime_camera.lens.f_stop == doctest::Approx(expected_f_stop).epsilon(1.0e-4));
        CHECK(runtime_camera.lens_shift.x == doctest::Approx(expected_shift_x).epsilon(1.0e-4));
        CHECK(runtime_camera.lens_shift.y == doctest::Approx(expected_shift_y).epsilon(1.0e-4));
        CHECK(runtime_camera.dof.focus_distance == doctest::Approx(expected_focus).epsilon(1.0e-4));
        CHECK(runtime_camera.dof.highlight_gain == doctest::Approx(expected_highlight).epsilon(1.0e-4));
        // Offline integer-sample interpolation of the same linear segment.
        CHECK(offline_camera.position.x == doctest::Approx(expected_x).epsilon(1.0e-3));
        CHECK(offline_camera.rotation.y == doctest::Approx(expected_yaw).epsilon(1.0e-3));
        CHECK(offline_camera.fov_deg == doctest::Approx(expected_fov).epsilon(1.0e-3));
        CHECK(offline_camera.lens.focal_length == doctest::Approx(expected_focal).epsilon(1.0e-3));
        CHECK(offline_camera.lens.sensor_width == doctest::Approx(expected_sensor).epsilon(1.0e-3));
        CHECK(offline_camera.lens.f_stop == doctest::Approx(expected_f_stop).epsilon(1.0e-3));
        CHECK(offline_camera.lens_shift.x == doctest::Approx(expected_shift_x).epsilon(1.0e-3));
        CHECK(offline_camera.lens_shift.y == doctest::Approx(expected_shift_y).epsilon(1.0e-3));
        CHECK(offline_camera.dof.focus_distance == doctest::Approx(expected_focus).epsilon(1.0e-3));
        CHECK(offline_camera.dof.highlight_gain == doctest::Approx(expected_highlight).epsilon(1.0e-3));
        // Parity between the two authorities at the shutter sub-frame.
        CHECK(std::abs(offline_camera.position.x - runtime_camera.position.x)
              < 0.05f);
        CHECK(std::abs(offline_camera.position.y - runtime_camera.position.y)
              < 0.05f);
        CHECK(std::abs(offline_camera.position.z - runtime_camera.position.z)
              < 0.05f);
        CHECK(offline_camera.is_animated);
        CHECK(runtime_camera.is_animated);
    }
}
