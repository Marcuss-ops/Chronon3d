#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/subtitle_style.hpp>
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

    REQUIRE(prepared.compiled_composition.definition);
    REQUIRE(prepared.compiled_composition.asset_manifest);
    CHECK(prepared.compiled_composition.asset_manifest->assets().empty());
    CHECK(prepared.compiled_composition.definition->composition.name ==
          "prepared-plan-test");
    CHECK(prepared.compiled_composition.definition->composition.width == 320);
    CHECK(prepared.compiled_composition.fingerprint != 0);
    REQUIRE(prepared.composition);
    CHECK(prepared.composition->width() ==
          prepared.compiled_composition.definition->composition.width);

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

    const auto compiled = chronon3d::compile_composition(definition, {});
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

    write_asset(root, "modified bytes");
    const auto modified = chronon3d::render_plan::compile_render_plan(
        image_plan(), resolver);
    REQUIRE(modified);
    CHECK(original->fingerprint.content_digest != modified->fingerprint.content_digest);
    CHECK(original->fingerprint.request_digest != modified->fingerprint.request_digest);
}

// ── Subtitle style parity (resolve_subtitle_style) ──────────────────────────
//
// The SubtitleTrack compiler consumes layer.style with EXACTLY the semantics
// of the text materializer: shared parse_hex_color (color_utils.hpp), same
// shadow defaults (opacity 1.0 / blur 0.0 / offset (0,0) when absent), same
// "absent → keep defaults" contract. These tests lock that parity.

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
    check_color(*resolved.fill, 1.0f, 0.7569f, 0.0275f, 1.0f);
    CHECK_FALSE(resolved.shadow.has_value());
    CHECK_FALSE(resolved.font_size.has_value());
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
    schema_changed.schema_version = "chronon.render-plan.v2";
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
