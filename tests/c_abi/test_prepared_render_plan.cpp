#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <array>
#include <chronon3d/render_plan/subtitle_style.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace {

class FingerprintTempDir {
public:
    FingerprintTempDir() {
        path = std::filesystem::temp_directory_path() /
               ("chronon3d_fingerprint_" +
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~FingerprintTempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

chronon3d::render_plan::RenderPlan image_plan() {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "fingerprint-plan";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};
    chronon3d::render_plan::LayerPlan image;
    image.id = "image";
    image.type = chronon3d::render_plan::LayerType::Image;
    image.asset = "images/source.png";
    plan.layers.push_back(std::move(image));
    return plan;
}

void write_asset(const std::filesystem::path& root, std::string contents) {
    std::filesystem::create_directories(root / "images");
    std::ofstream output(root / "images/source.png", std::ios::binary);
    output << contents;
}

} // namespace

TEST_CASE("all text backends share one linear-domain color packer") {
    const chronon3d::Color color{0.2158605f, 0.5f, 1.0f, 0.5f};
    const auto packed = chronon3d::text::pack_text_color(color);
    CHECK((packed & 0xFFu) == 55u);
    CHECK(((packed >> 8) & 0xFFu) == 128u);
    CHECK(((packed >> 16) & 0xFFu) == 255u);
    CHECK(((packed >> 24) & 0xFFu) == 128u);
}

TEST_CASE("compile_render_plan fails loudly when the budget is exceeded") {
    chronon3d::render_plan::RenderPlan plan;
    plan.canvas = {.width = 0, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{1}};
    chronon3d::assets::AssetResolver resolver;
    const auto result = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE_FALSE(result);
    CHECK(result.error().path == "canvas.width");
}

TEST_CASE("prepared render plan owns the canonical compiled composition") {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "prepared-plan-test";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};

    chronon3d::render_plan::LayerPlan color;
    color.id = "background";
    color.type = chronon3d::render_plan::LayerType::Color;
    color.color = {0.1f, 0.2f, 0.3f, 1.0f};
    plan.layers.push_back(color);

    chronon3d::assets::AssetResolver resolver;
    const auto prepared_result =
        chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared_result);
    const auto& prepared = prepared_result.value();

    REQUIRE(prepared.compiled_composition.composition);
    REQUIRE(prepared.compiled_composition.asset_manifest);
    CHECK(prepared.compiled_composition.asset_manifest->assets().empty());
    CHECK(prepared.compiled_composition.composition->name() ==
          "prepared-plan-test");
    CHECK(prepared.compiled_composition.composition->width() == 320);
    CHECK(prepared.compiled_composition.fingerprint != 0);
    REQUIRE(prepared.composition);
    CHECK(prepared.composition->width() ==
          prepared.compiled_composition.composition->width());

    // The compiled value owns its definition.  Dropping the source plan and
    // resolver cannot invalidate the scene callback or its metadata.
    const auto compiled = prepared.compiled_composition;
    const auto evaluated = chronon3d::evaluate(
        compiled, chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{1});
    REQUIRE(evaluated);
    CHECK(evaluated->scene.layers().size() == 1);

    chronon3d::assets::AssetResolver second_resolver;
    const auto repeated =
        chronon3d::render_plan::compile_render_plan(plan, second_resolver);
    REQUIRE(repeated);
    CHECK(repeated->compiled_composition.fingerprint ==
          prepared.compiled_composition.fingerprint);
    CHECK(repeated->fingerprint.content_digest == prepared.fingerprint.content_digest);
    CHECK(repeated->fingerprint.request_digest == prepared.fingerprint.request_digest);
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
