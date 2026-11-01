#include <doctest/doctest.h>

#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>

#include <array>

using namespace chronon3d;

TEST_CASE("VideoSource V2 identity includes every semantic field") {
    video::VideoSource base;
    base.path = "clip.mov";
    base.size = {1920.0f, 1080.0f};
    base.source_start = Frame{3};
    base.duration = Frame{90};
    base.source_fps = 30.0f;
    base.speed = 1.0f;
    base.loop_mode = video::VideoLoopMode::None;
    base.ocio_config_path = "config.ocio";
    base.ocio_input_space = "camera";
    base.ocio_working_space = "linear";
    const auto original = graph::hash_video_source_v2(base);

    const auto changed = [&](auto mutate) {
        auto source = base;
        mutate(source);
        CHECK(graph::hash_video_source_v2(source) != original);
    };
    changed([](auto& s) { s.path += ".bak"; });
    changed([](auto& s) { s.size.x += 1.0f; });
    changed([](auto& s) { s.size.y += 1.0f; });
    changed([](auto& s) { s.source_start = Frame{4}; });
    changed([](auto& s) { s.duration = Frame{91}; });
    changed([](auto& s) { s.source_fps = 25.0f; });
    changed([](auto& s) { s.speed = 0.5f; });
    changed([](auto& s) { s.loop_mode = video::VideoLoopMode::Loop; });
    changed([](auto& s) { s.ocio_config_path += ".bak"; });
    changed([](auto& s) { s.ocio_input_space += "_v2"; });
    changed([](auto& s) { s.ocio_working_space += "_v2"; });
    CHECK(original != graph::hash_video_source(base));

    auto positive_zero = base;
    auto negative_zero = base;
    positive_zero.size = {0.0f, 0.0f};
    positive_zero.source_fps = 0.0f;
    positive_zero.speed = 0.0f;
    negative_zero.size = {-0.0f, -0.0f};
    negative_zero.source_fps = -0.0f;
    negative_zero.speed = -0.0f;
    CHECK(graph::hash_video_source_v2(positive_zero) ==
          graph::hash_video_source_v2(negative_zero));
}

TEST_CASE("Mask V2 identity includes semantic primary and operation fields") {
    Mask base;
    base.type = MaskType::Path;
    base.initial_mode = MaskCombineMode::Subtract;
    base.pos = {1.0f, 2.0f, 3.0f};
    base.size = {40.0f, 50.0f};
    base.radius = 6.0f;
    base.path = {PathCommand::move_to({1.0f, 2.0f}),
                 PathCommand::line_to({3.0f, 4.0f})};
    base.text_source = "mask-source";
    base.feather = 2.0f;
    base.expansion = 1.0f;
    base.opacity = 0.75f;
    base.inverted = true;
    base.operations.push_back(MaskOperation{
        .mode = MaskCombineMode::Add,
        .type = MaskType::Circle,
        .pos = {4.0f, 5.0f, 6.0f},
        .size = {12.0f, 13.0f},
        .radius = 7.0f,
        .text_source = "operation-source",
        .feather = 1.5f,
        .expansion = 0.5f,
        .opacity = 0.8f,
        .inverted = false,
    });
    const auto original = graph::hash_mask_v2(base);
    const auto changed = [&](auto mutate) {
        auto mask = base;
        mutate(mask);
        CHECK(graph::hash_mask_v2(mask) != original);
    };
    changed([](auto& m) { m.type = MaskType::Ellipse; });
    changed([](auto& m) { m.initial_mode = MaskCombineMode::Intersect; });
    changed([](auto& m) { m.pos.x += 1.0f; });
    changed([](auto& m) { m.size.y += 1.0f; });
    changed([](auto& m) { m.radius += 1.0f; });
    changed([](auto& m) { m.path[1].p0.x += 1.0f; });
    changed([](auto& m) { m.text_source += "_2"; });
    changed([](auto& m) { m.feather += 1.0f; });
    changed([](auto& m) { m.expansion += 1.0f; });
    changed([](auto& m) { m.opacity -= 0.1f; });
    changed([](auto& m) { m.inverted = false; });
    changed([](auto& m) {
        graphics::Field2D field;
        field.seed = 42;
        m.field = field;
    });
    changed([](auto& m) { m.operations[0].mode = MaskCombineMode::Difference; });
    changed([](auto& m) { m.operations[0].type = MaskType::Ellipse; });
    changed([](auto& m) { m.operations[0].pos.z += 1.0f; });
    changed([](auto& m) { m.operations[0].size.x += 1.0f; });
    changed([](auto& m) { m.operations[0].radius += 1.0f; });
    changed([](auto& m) { m.operations[0].feather += 1.0f; });
    changed([](auto& m) { m.operations[0].expansion += 1.0f; });
    changed([](auto& m) { m.operations[0].opacity -= 0.1f; });
    changed([](auto& m) { m.operations[0].text_source += "_2"; });
    changed([](auto& m) { m.operations[0].inverted = true; });
    changed([](auto& m) {
        graphics::Field2D field;
        field.seed = 19;
        m.operations[0].field = field;
    });
}

TEST_CASE("Shape V2 hashes rounded rectangle fields component-wise") {
    Shape base;
    base.set_type(ShapeType::RoundedRect);
    base.rounded_rect().size = {80.0f, 40.0f};
    base.rounded_rect().radius = 8.0f;
    base.rounded_rect().corner_radius = std::array<f32, 4>{1.0f, 2.0f, 3.0f, 4.0f};
    base.rounded_rect().stroke.enabled = true;
    base.rounded_rect().stroke.color = {0.1f, 0.2f, 0.3f, 0.4f};
    base.rounded_rect().stroke.width = 2.0f;
    base.rounded_rect().stroke.alignment = StrokeAlignment::Inside;
    base.rounded_rect().stroke.gradient = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f}, {{0.0f, Color::black()}, {1.0f, Color::white()}});
    const auto original = graph::hash_shape_v2(base);
    const auto changed = [&](auto mutate) {
        auto shape = base;
        mutate(shape);
        CHECK(graph::hash_shape_v2(shape) != original);
    };
    changed([](auto& s) { s.rounded_rect().size.x += 1.0f; });
    changed([](auto& s) { s.rounded_rect().size.y += 1.0f; });
    changed([](auto& s) { s.rounded_rect().radius += 1.0f; });
    changed([](auto& s) { (*s.rounded_rect().corner_radius)[0] += 1.0f; });
    changed([](auto& s) { (*s.rounded_rect().corner_radius)[1] += 1.0f; });
    changed([](auto& s) { (*s.rounded_rect().corner_radius)[2] += 1.0f; });
    changed([](auto& s) { (*s.rounded_rect().corner_radius)[3] += 1.0f; });
    changed([](auto& s) { s.rounded_rect().stroke.enabled = false; });
    changed([](auto& s) { s.rounded_rect().stroke.color.r += 0.1f; });
    changed([](auto& s) { s.rounded_rect().stroke.color.a -= 0.1f; });
    changed([](auto& s) { s.rounded_rect().stroke.width += 1.0f; });
    changed([](auto& s) { s.rounded_rect().stroke.alignment = StrokeAlignment::Outside; });
    changed([](auto& s) { s.rounded_rect().stroke.gradient->angle += 0.1f; });

    auto positive_zero = base;
    auto negative_zero = base;
    positive_zero.rounded_rect().size.x = 0.0f;
    negative_zero.rounded_rect().size.x = -0.0f;
    positive_zero.rounded_rect().stroke.color.r = 0.0f;
    negative_zero.rounded_rect().stroke.color.r = -0.0f;
    CHECK(graph::hash_shape_v2(positive_zero) == graph::hash_shape_v2(negative_zero));
}

TEST_CASE("Shape V2 canonicalizes scalar fields and frames image paths") {
    Shape circle;
    circle.set_type(ShapeType::Circle);
    circle.circle().radius = 12.0f;
    circle.circle().stroke.alignment = StrokeAlignment::Inside;
    const auto circle_hash = graph::hash_shape_v2(circle);
    auto changed_circle = circle;
    changed_circle.circle().radius += 1.0f;
    CHECK(graph::hash_shape_v2(changed_circle) != circle_hash);
    changed_circle = circle;
    changed_circle.circle().stroke.alignment = StrokeAlignment::Outside;
    CHECK(graph::hash_shape_v2(changed_circle) != circle_hash);
    auto positive_zero = circle;
    auto negative_zero = circle;
    positive_zero.circle().radius = 0.0f;
    negative_zero.circle().radius = -0.0f;
    CHECK(graph::hash_shape_v2(positive_zero) == graph::hash_shape_v2(negative_zero));

    Shape image_a;
    image_a.set_type(ShapeType::Image);
    image_a.image().path = "asset";
    Shape image_b = image_a;
    image_b.image().path = "asset2";
    CHECK(graph::hash_shape_v2(image_a) != graph::hash_shape_v2(image_b));
}

TEST_CASE("TextRun V2 identity covers rendered fields and vector boundaries") {
    TextRunShape base;
    GlyphInstanceState glyph;
    glyph.glyph_id = 17;
    glyph.anchor = {1.0f, 2.0f, 3.0f};
    base.glyphs.push_back(glyph);
    base.material.inner_shadow_enabled = true;
    base.material.inner_shadow_blur = 3.0f;
    base.paint.stroke_dash_array = {2.0f, 4.0f};
    base.shadows.push_back(TextShadow{.enabled = true, .blur = 1.0f});
    const auto original = hash_text_run_shape_v2(base);
    const auto changed = [&](auto mutate) {
        auto value = base;
        mutate(value);
        CHECK(hash_text_run_shape_v2(value) != original);
    };
    changed([](auto& value) { value.glyphs[0].anchor.x += 1.0f; });
    changed([](auto& value) { value.material.inner_shadow_blur += 1.0f; });
    changed([](auto& value) { value.paint.stroke_dash_array.push_back(6.0f); });
    changed([](auto& value) { value.shadows[0].opacity -= 0.1f; });
    changed([](auto& value) { value.dissolve_mix = 0.25f; });

    auto positive_zero = base;
    auto negative_zero = base;
    positive_zero.glyphs[0].position.x = 0.0f;
    negative_zero.glyphs[0].position.x = -0.0f;
    CHECK(hash_text_run_shape_v2(positive_zero) == hash_text_run_shape_v2(negative_zero));
}

TEST_CASE("GPU glyph identity retains full variation axes") {
    using runtime::GlyphRepresentation;
    using runtime::GpuGlyphKey;
    auto base = GpuGlyphKey::make("font.ttf", 65, std::string{"wght=400"},
        GlyphRepresentation::Mtsdf, 7, 32);
    auto same = GpuGlyphKey::make("font.ttf", 65, std::string{"wght=400"},
        GlyphRepresentation::Mtsdf, 7, 96);
    CHECK(base == same);
    CHECK(runtime::GpuGlyphKeyHash{}(base) == runtime::GpuGlyphKeyHash{}(same));

    auto changed = base;
    changed.variation_axes = "wght=700";
    // Even an identical 64-bit variation digest cannot alias full-axis keys.
    changed.variation_hash = base.variation_hash;
    CHECK_FALSE(base == changed);
    CHECK(runtime::GpuGlyphKeyHash{}(base) != runtime::GpuGlyphKeyHash{}(changed));

    const auto coverage = GpuGlyphKey::make("font.ttf", 65, std::string{"wght=400"},
        GlyphRepresentation::Coverage, 7, 32);
    auto coverage_changed = coverage;
    coverage_changed.font_size++;
    CHECK_FALSE(coverage == coverage_changed);
}
