#include <doctest/doctest.h>

#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/rendering/lighting_hashing.hpp>
#include <chronon3d/assets/hdri_environment.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include "src/backends/text/font_bbox_cache_key.hpp"
#include "src/backends/text/mtsdf_glyph_cache_key.hpp"

#include <array>
#include <memory>
#include <vector>

using namespace chronon3d;

TEST_CASE("Lighting V2 identity covers all LightContext dependencies") {
    rendering::LightContext base;
    base.enabled = true;
    base.ambient_enabled = true;
    base.directional_enabled = true;
    base.direction = {1.0f, 2.0f, 3.0f};
    base.ambient_color = {0.1f, 0.2f, 0.3f, 0.4f};
    base.ambient = 0.5f;
    base.directional_color = {0.6f, 0.7f, 0.8f, 0.9f};
    base.diffuse = 1.1f;
    base.environment_enabled = true;
    for (std::size_t i = 0; i < base.environment_irradiance_sh.size(); ++i)
        base.environment_irradiance_sh[i] = static_cast<float>(i + 1);
    base.environment_intensity = 1.2f;
    base.environment_rotation_y = 0.3f;
    auto probe = std::make_shared<assets::HdriEnvV1>();
    probe->content_hash = 99;
    base.environment_probe = std::move(probe);
    base.shadows.opacity = 0.4f;
    base.shadows.blur_radius = 2.0f;
    base.shadows.px_per_unit = 3.0f;
    base.shadows.max_offset = 4.0f;
    base.shadows.contact_opacity = 0.5f;
    base.shadows.contact_blur_radius = 6.0f;
    base.shadows.ambient_opacity = 0.7f;
    base.shadows.ambient_blur_radius = 8.0f;
    base.shadows.depth_aware = false;
    base.shadows.blur_per_z = 0.9f;
    base.shadows.opacity_falloff = 0.01f;
    base.shadows.tint = {0.2f, 0.3f, 0.4f, 0.5f};
    rendering::PointLightSource point;
    point.kind = rendering::PointLightKind::Spot;
    point.position = {1.0f, 2.0f, 3.0f};
    point.direction = {4.0f, 5.0f, 6.0f};
    point.color = {0.2f, 0.4f, 0.6f, 0.8f};
    point.radius = 7.0f;
    point.range = 8.0f;
    point.decay = 2.0f;
    point.angle = 0.7f;
    point.penumbra = 0.3f;
    point.casts_shadow = true;
    point.shadow_radius = 1.5f;
    point.shadow_bias = 0.02f;
    base.point_lights.push_back(point);

    const auto original = rendering::hash_light_context_v2(base);
    const auto changed = [&](auto mutate) {
        auto value = base;
        mutate(value);
        CHECK(rendering::hash_light_context_v2(value) != original);
    };
    changed([](auto& l) { l.enabled = false; });
    changed([](auto& l) { l.ambient_enabled = false; });
    changed([](auto& l) { l.directional_enabled = false; });
    changed([](auto& l) { l.direction.x += 1.0f; });
    changed([](auto& l) { l.direction.y += 1.0f; });
    changed([](auto& l) { l.direction.z += 1.0f; });
    changed([](auto& l) { l.ambient_color.r += 0.01f; });
    changed([](auto& l) { l.ambient_color.g += 0.01f; });
    changed([](auto& l) { l.ambient_color.b += 0.01f; });
    changed([](auto& l) { l.ambient_color.a += 0.01f; });
    changed([](auto& l) { l.ambient += 0.01f; });
    changed([](auto& l) { l.directional_color.r += 0.01f; });
    changed([](auto& l) { l.directional_color.g += 0.01f; });
    changed([](auto& l) { l.directional_color.b += 0.01f; });
    changed([](auto& l) { l.directional_color.a += 0.01f; });
    changed([](auto& l) { l.diffuse += 0.01f; });
    changed([](auto& l) { l.environment_enabled = false; });
    for (std::size_t i = 0; i < base.environment_irradiance_sh.size(); ++i)
        changed([i](auto& l) { l.environment_irradiance_sh[i] += 1.0f; });
    changed([](auto& l) { l.environment_intensity += 0.1f; });
    changed([](auto& l) { l.environment_rotation_y += 0.1f; });
    changed([](auto& l) { l.environment_probe.reset(); });
    changed([](auto& l) {
        auto probe = std::make_shared<assets::HdriEnvV1>();
        probe->content_hash = 100;
        l.environment_probe = std::move(probe);
    });
    changed([](auto& l) { l.shadows.opacity += 0.01f; });
    changed([](auto& l) { l.shadows.blur_radius += 0.1f; });
    changed([](auto& l) { l.shadows.px_per_unit += 0.1f; });
    changed([](auto& l) { l.shadows.max_offset += 0.1f; });
    changed([](auto& l) { l.shadows.contact_opacity += 0.01f; });
    changed([](auto& l) { l.shadows.contact_blur_radius += 0.1f; });
    changed([](auto& l) { l.shadows.ambient_opacity += 0.01f; });
    changed([](auto& l) { l.shadows.ambient_blur_radius += 0.1f; });
    changed([](auto& l) { l.shadows.depth_aware = true; });
    changed([](auto& l) { l.shadows.blur_per_z += 0.01f; });
    changed([](auto& l) { l.shadows.opacity_falloff += 0.001f; });
    changed([](auto& l) { l.shadows.tint.r += 0.01f; });
    changed([](auto& l) { l.shadows.tint.g += 0.01f; });
    changed([](auto& l) { l.shadows.tint.b += 0.01f; });
    changed([](auto& l) { l.shadows.tint.a += 0.01f; });
    changed([](auto& l) { l.point_lights[0].kind = rendering::PointLightKind::Point; });
    changed([](auto& l) { l.point_lights[0].position.x += 1.0f; });
    changed([](auto& l) { l.point_lights[0].position.y += 1.0f; });
    changed([](auto& l) { l.point_lights[0].position.z += 1.0f; });
    changed([](auto& l) { l.point_lights[0].direction.x += 1.0f; });
    changed([](auto& l) { l.point_lights[0].direction.y += 1.0f; });
    changed([](auto& l) { l.point_lights[0].direction.z += 1.0f; });
    changed([](auto& l) { l.point_lights[0].color.r += 0.01f; });
    changed([](auto& l) { l.point_lights[0].color.g += 0.01f; });
    changed([](auto& l) { l.point_lights[0].color.b += 0.01f; });
    changed([](auto& l) { l.point_lights[0].color.a += 0.01f; });
    changed([](auto& l) { l.point_lights[0].radius += 0.1f; });
    changed([](auto& l) { l.point_lights[0].range += 0.1f; });
    changed([](auto& l) { l.point_lights[0].decay += 0.1f; });
    changed([](auto& l) { l.point_lights[0].angle += 0.1f; });
    changed([](auto& l) { l.point_lights[0].penumbra += 0.1f; });
    changed([](auto& l) { l.point_lights[0].casts_shadow = false; });
    changed([](auto& l) { l.point_lights[0].shadow_radius += 0.1f; });
    changed([](auto& l) { l.point_lights[0].shadow_bias += 0.01f; });
    changed([](auto& l) { l.point_lights.push_back(rendering::PointLightSource{}); });

    auto positive_zero = base;
    auto negative_zero = base;
    positive_zero.direction.x = 0.0f;
    negative_zero.direction.x = -0.0f;
    CHECK(rendering::hash_light_context_v2(positive_zero) ==
          rendering::hash_light_context_v2(negative_zero));
}

TEST_CASE("Lighting V2 identity covers all Material2_5D dependencies") {
    Material2_5D base;
    base.accepts_lights = false;
    base.casts_shadows = true;
    base.accepts_shadows = false;
    base.diffuse = 0.2f;
    base.specular = 0.3f;
    base.shininess = 4.0f;
    base.ambient_multiplier = 0.5f;
    base.emissive_color = {0.1f, 0.2f, 0.3f, 0.4f};
    base.emissive_enabled = true;
    base.double_sided = true;
    base.alpha_cutoff = 0.6f;
    base.metallic = 0.7f;
    base.roughness = 0.8f;
    const auto original = rendering::hash_material_2_5d_v2(base);
    const auto changed = [&](auto mutate) {
        auto value = base;
        mutate(value);
        CHECK(rendering::hash_material_2_5d_v2(value) != original);
    };
    changed([](auto& m) { m.accepts_lights = true; });
    changed([](auto& m) { m.casts_shadows = false; });
    changed([](auto& m) { m.accepts_shadows = true; });
    changed([](auto& m) { m.diffuse += 0.01f; });
    changed([](auto& m) { m.specular += 0.01f; });
    changed([](auto& m) { m.shininess += 1.0f; });
    changed([](auto& m) { m.ambient_multiplier += 0.01f; });
    changed([](auto& m) { m.emissive_color.r += 0.01f; });
    changed([](auto& m) { m.emissive_color.g += 0.01f; });
    changed([](auto& m) { m.emissive_color.b += 0.01f; });
    changed([](auto& m) { m.emissive_color.a += 0.01f; });
    changed([](auto& m) { m.emissive_enabled = false; });
    changed([](auto& m) { m.double_sided = false; });
    changed([](auto& m) { m.alpha_cutoff = 0.9f; });
    changed([](auto& m) { m.alpha_cutoff.reset(); });
    changed([](auto& m) { m.metallic = 0.9f; });
    changed([](auto& m) { m.metallic.reset(); });
    changed([](auto& m) { m.roughness = 0.4f; });
    changed([](auto& m) { m.roughness.reset(); });
}

TEST_CASE("Transform V2 identity includes every rendered transform field") {
    Transform base;
    base.position = {1.0f, 2.0f, 3.0f};
    base.rotation = Quat{0.9f, 0.1f, 0.2f, 0.3f};
    base.scale = {2.0f, 3.0f, 4.0f};
    base.anchor = {5.0f, 6.0f, 7.0f};
    base.skew = {8.0f, 9.0f};
    base.opacity = 0.8f;
    base.blur = 1.5f;
    const auto original = graph::hash_transform_v2(base);
    const auto changed = [&](auto mutate) {
        auto value = base;
        mutate(value);
        CHECK(graph::hash_transform_v2(value) != original);
    };
    changed([](auto& t) { t.position.x += 1.0f; });
    changed([](auto& t) { t.position.y += 1.0f; });
    changed([](auto& t) { t.position.z += 1.0f; });
    changed([](auto& t) { t.rotation.w += 0.01f; });
    changed([](auto& t) { t.rotation.x += 0.01f; });
    changed([](auto& t) { t.rotation.y += 0.01f; });
    changed([](auto& t) { t.rotation.z += 0.01f; });
    changed([](auto& t) { t.scale.x += 1.0f; });
    changed([](auto& t) { t.scale.y += 1.0f; });
    changed([](auto& t) { t.scale.z += 1.0f; });
    changed([](auto& t) { t.anchor.x += 1.0f; });
    changed([](auto& t) { t.anchor.y += 1.0f; });
    changed([](auto& t) { t.anchor.z += 1.0f; });
    changed([](auto& t) { t.skew.x += 1.0f; });
    changed([](auto& t) { t.skew.y += 1.0f; });
    changed([](auto& t) { t.opacity -= 0.1f; });
    changed([](auto& t) { t.blur += 1.0f; });
    CHECK(graph::hash_transform_v2(base) != graph::hash_transform(base));

    auto positive_zero = base;
    auto negative_zero = base;
    positive_zero.blur = 0.0f;
    negative_zero.blur = -0.0f;
    CHECK(graph::hash_transform_v2(positive_zero) ==
          graph::hash_transform_v2(negative_zero));
}

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

    image_a.image().fit = FitMode::Contain;
    image_a.image().focal_point = {0.25f, 0.75f};
    image_a.image().crop = ImageCrop{true, {0.1f, 0.2f}, {0.7f, 0.6f}, true};
    image_a.image().radius = 5.0f;
    image_a.image().decode_options.color_space = ColorSpace::LinearSRGB;
    image_a.image().decode_options.premultiply = PremultiplyAlpha::No;
    image_a.image().decode_options.orientation = OrientationPolicy::IgnoreExif;
    image_a.image().decode_options.max_dimension = 512;
    image_a.image().decode_options.ocio_config_path = "config.ocio";
    image_a.image().decode_options.ocio_input_space = "camera";
    image_a.image().decode_options.ocio_working_space = "linear";
    image_a.image().embedded_payload = std::make_shared<const std::vector<std::byte>>(
        std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}});
    const auto image_full_hash = graph::hash_shape_v2(image_a);
    const auto changed_image = [&](auto mutate) {
        auto shape = image_a;
        mutate(shape.image());
        CHECK(graph::hash_shape_v2(shape) != image_full_hash);
    };
    changed_image([](auto& i) { i.fit = FitMode::Cover; });
    changed_image([](auto& i) { i.focal_point.x += 0.1f; });
    changed_image([](auto& i) { i.focal_point.y += 0.1f; });
    changed_image([](auto& i) { i.crop.enabled = false; });
    changed_image([](auto& i) { i.crop.origin.x += 0.1f; });
    changed_image([](auto& i) { i.crop.origin.y += 0.1f; });
    changed_image([](auto& i) { i.crop.size.x += 0.1f; });
    changed_image([](auto& i) { i.crop.size.y += 0.1f; });
    changed_image([](auto& i) { i.crop.flip_x = false; });
    changed_image([](auto& i) { i.radius += 1.0f; });
    changed_image([](auto& i) { i.decode_options.color_space = ColorSpace::DisplayP3; });
    changed_image([](auto& i) { i.decode_options.premultiply = PremultiplyAlpha::Yes; });
    changed_image([](auto& i) { i.decode_options.orientation = OrientationPolicy::ApplyExif; });
    changed_image([](auto& i) { i.decode_options.max_dimension++; });
    changed_image([](auto& i) { i.decode_options.ocio_config_path += ".v2"; });
    changed_image([](auto& i) { i.decode_options.ocio_input_space += "_v2"; });
    changed_image([](auto& i) { i.decode_options.ocio_working_space += "_v2"; });
    changed_image([](auto& i) {
        i.embedded_payload = std::make_shared<const std::vector<std::byte>>(
            std::vector<std::byte>{std::byte{0x01}, std::byte{0x03}});
    });

    Shape tiled_image;
    tiled_image.set_type(ShapeType::TiledImage);
    tiled_image.tiled_image().image.path = "tile.png";
    tiled_image.tiled_image().image.fit = FitMode::Contain;
    const auto tiled_hash = graph::hash_shape_v2(tiled_image);
    tiled_image.tiled_image().image.fit = FitMode::Stretch;
    CHECK(graph::hash_shape_v2(tiled_image) != tiled_hash);

    Shape mesh;
    mesh.set_type(ShapeType::Mesh);
    mesh.mesh_shape().reference = assets::MeshRef{"mesh.glb", "scene", true};
    auto prepared = std::make_shared<assets::PreparedMeshSource>();
    prepared->identity.resolved_path = "/assets/mesh.glb";
    prepared->identity.byte_size = 64;
    prepared->identity.write_time = 128;
    prepared->identity.content_digest.bytes[0] = std::byte{0x12};
    mesh.mesh_shape().prepared = prepared;
    const auto mesh_hash = graph::hash_shape_v2(mesh);
    const auto changed_mesh = [&](auto mutate_identity) {
        auto shape = mesh;
        auto next = std::make_shared<assets::PreparedMeshSource>(*prepared);
        mutate_identity(next->identity);
        shape.mesh_shape().prepared = std::move(next);
        CHECK(graph::hash_shape_v2(shape) != mesh_hash);
    };
    changed_mesh([](auto& id) { id.resolved_path += ".replacement"; });
    changed_mesh([](auto& id) { id.byte_size++; });
    changed_mesh([](auto& id) { id.write_time++; });
    changed_mesh([](auto& id) { id.content_digest.bytes[0] = std::byte{0x13}; });
}

TEST_CASE("Shape V2 covers the remaining primitive and analytic shape fields") {
    const auto check_mutations = [](const Shape& base, auto... mutations) {
        const auto original = graph::hash_shape_v2(base);
        const auto changed = [&](auto mutate) {
            auto value = base;
            mutate(value);
            CHECK(graph::hash_shape_v2(value) != original);
        };
        (changed(mutations), ...);
    };

    Shape rect;
    rect.set_type(ShapeType::Rect);
    rect.rect().stroke.enabled = true;
    rect.rect().stroke.gradient = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f}, {{0.0f, Color::black()}, {1.0f, Color::white()}});
    check_mutations(rect,
        [](auto& s) { s.rect().size.x += 1.0f; },
        [](auto& s) { s.rect().size.y += 1.0f; },
        [](auto& s) { s.rect().stroke.enabled = false; },
        [](auto& s) { s.rect().stroke.color.r += 0.1f; },
        [](auto& s) { s.rect().stroke.color.g += 0.1f; },
        [](auto& s) { s.rect().stroke.color.b += 0.1f; },
        [](auto& s) { s.rect().stroke.color.a -= 0.1f; },
        [](auto& s) { s.rect().stroke.color.g += 0.1f; },
        [](auto& s) { s.rect().stroke.color.b += 0.1f; },
        [](auto& s) { s.rect().stroke.color.a -= 0.1f; },
        [](auto& s) { s.rect().stroke.width += 1.0f; },
        [](auto& s) { s.rect().stroke.alignment = StrokeAlignment::Inside; },
        [](auto& s) { s.rect().stroke.gradient->angle += 0.1f; });

    Shape line;
    line.set_type(ShapeType::Line);
    line.line().stroke.enabled = true;
    check_mutations(line,
        [](auto& s) { s.line().to.x += 1.0f; },
        [](auto& s) { s.line().to.y += 1.0f; },
        [](auto& s) { s.line().to.z += 1.0f; },
        [](auto& s) { s.line().thickness += 1.0f; },
        [](auto& s) { s.line().stroke.trim_start += 0.1f; },
        [](auto& s) { s.line().stroke.trim_end += 0.1f; },
        [](auto& s) { s.line().stroke.enabled = false; },
        [](auto& s) { s.line().stroke.color.r += 0.1f; },
        [](auto& s) { s.line().stroke.width += 1.0f; },
        [](auto& s) { s.line().stroke.alignment = StrokeAlignment::Inside; });

    Shape grid;
    grid.set_type(ShapeType::GridBackground);
    check_mutations(grid,
        [](auto& s) { s.grid_background().size.x += 1.0f; },
        [](auto& s) { s.grid_background().size.y += 1.0f; },
        [](auto& s) { s.grid_background().offset.x += 1.0f; },
        [](auto& s) { s.grid_background().offset.y += 1.0f; },
        [](auto& s) { s.grid_background().bg_color.r += 0.1f; },
        [](auto& s) { s.grid_background().bg_color.g += 0.1f; },
        [](auto& s) { s.grid_background().bg_color.b += 0.1f; },
        [](auto& s) { s.grid_background().bg_color.a -= 0.1f; },
        [](auto& s) { s.grid_background().grid_color.r += 0.1f; },
        [](auto& s) { s.grid_background().grid_color.g += 0.1f; },
        [](auto& s) { s.grid_background().grid_color.b += 0.1f; },
        [](auto& s) { s.grid_background().grid_color.a += 0.1f; },
        [](auto& s) { s.grid_background().spacing += 1.0f; },
        [](auto& s) { s.grid_background().minor_thickness += 0.1f; },
        [](auto& s) { s.grid_background().major_thickness += 0.1f; },
        [](auto& s) { s.grid_background().major_every++; },
        [](auto& s) { s.grid_background().centered = false; },
        [](auto& s) { s.grid_background().edge_fade += 0.1f; },
        [](auto& s) { s.grid_background().major_glow += 0.1f; },
        [](auto& s) { s.grid_background().style = GridStyle::Isometric; });

    Shape box;
    box.set_type(ShapeType::FakeBox3D);
    check_mutations(box,
        [](auto& s) { s.fake_box3d().world_pos.x += 1.0f; },
        [](auto& s) { s.fake_box3d().world_pos.y += 1.0f; },
        [](auto& s) { s.fake_box3d().world_pos.z += 1.0f; },
        [](auto& s) { s.fake_box3d().size.x += 1.0f; },
        [](auto& s) { s.fake_box3d().size.y += 1.0f; },
        [](auto& s) { s.fake_box3d().depth += 1.0f; },
        [](auto& s) { s.fake_box3d().color.r += 0.1f; },
        [](auto& s) { s.fake_box3d().color.g += 0.1f; },
        [](auto& s) { s.fake_box3d().color.b += 0.1f; },
        [](auto& s) { s.fake_box3d().color.a -= 0.1f; },
        [](auto& s) { s.fake_box3d().top_tint += 0.1f; },
        [](auto& s) { s.fake_box3d().side_tint += 0.1f; });

    Shape plane;
    plane.set_type(ShapeType::GridPlane);
    check_mutations(plane,
        [](auto& s) { s.grid_plane().world_pos.x += 1.0f; },
        [](auto& s) { s.grid_plane().world_pos.y += 1.0f; },
        [](auto& s) { s.grid_plane().world_pos.z += 1.0f; },
        [](auto& s) { s.grid_plane().axis = PlaneAxis::XY; },
        [](auto& s) { s.grid_plane().extent += 1.0f; },
        [](auto& s) { s.grid_plane().spacing += 1.0f; },
        [](auto& s) { s.grid_plane().color.a += 0.1f; },
        [](auto& s) { s.grid_plane().color.r += 0.1f; },
        [](auto& s) { s.grid_plane().color.g += 0.1f; },
        [](auto& s) { s.grid_plane().color.b += 0.1f; },
        [](auto& s) { s.grid_plane().fade_distance += 1.0f; },
        [](auto& s) { s.grid_plane().fade_min_alpha += 0.1f; });

    Shape tiled;
    tiled.set_type(ShapeType::TiledImage);
    tiled.tiled_image().image.path = "tile.png";
    check_mutations(tiled,
        [](auto& s) { s.tiled_image().image.path += ".v2"; },
        [](auto& s) { s.tiled_image().image.size.x += 1.0f; },
        [](auto& s) { s.tiled_image().image.size.y += 1.0f; },
        [](auto& s) { s.tiled_image().image.opacity -= 0.1f; },
        [](auto& s) { s.tiled_image().image.tint.r -= 0.1f; });
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

TEST_CASE("GPU glyph V2 digests respond to every semantic field") {
    using runtime::GlyphRepresentation;
    using runtime::GpuGlyphKey;
    const auto base = GpuGlyphKey::make("font.ttf", 65, std::string{"wght=400"},
        GlyphRepresentation::Coverage, 7, 32);
    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK_FALSE(key == base);
        CHECK(runtime::GpuGlyphKeyHash{}(key) != runtime::GpuGlyphKeyHash{}(base));
        CHECK(runtime::glyph_key_hash64(key) != runtime::glyph_key_hash64(base));
    };
    changed([](auto& key) { key.font_path += ".replacement"; });
    changed([](auto& key) { key.glyph_id++; });
    changed([](auto& key) { key.variation_axes = "wght=700"; });
    changed([](auto& key) { key.representation = GlyphRepresentation::CoverageRgba; });
    changed([](auto& key) { key.generation_profile++; });
    changed([](auto& key) { key.font_size++; });
}

TEST_CASE("FreeType glyph bbox V2 identity hashes every semantic field") {
    using chronon3d::GlyphBBoxCacheKey;
    using chronon3d::GlyphBBoxCacheKeyHash;
    const GlyphBBoxCacheKey base{17, 65, 32, "wght=400"};
    CHECK(GlyphBBoxCacheKeyHash{}(base) ==
          GlyphBBoxCacheKeyHash{}(GlyphBBoxCacheKey{17, 65, 32, "wght=400"}));
    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK_FALSE(key == base);
        CHECK(GlyphBBoxCacheKeyHash{}(key) != GlyphBBoxCacheKeyHash{}(base));
    };
    changed([](auto& key) { key.face_id++; });
    changed([](auto& key) { key.glyph_id++; });
    changed([](auto& key) { key.pixel_size++; });
    changed([](auto& key) { key.variation_axes = "wght=700"; });
}

TEST_CASE("Styled glyph cache V2 identity distinguishes every semantic field") {
    using runtime::GpuStyledGlyphCache;
    using runtime::StyledGlyphKey;
    StyledGlyphKey base;
    base.font_id = 17;
    base.glyph_id = 65;
    base.font_size = 32;
    base.stroke_radius = 2;
    base.background_padding = 3;
    base.fill = {.2f, .3f, .4f, 1.0f};
    base.background = {.1f, .2f, .3f, .8f};
    base.stroke = {.8f, .7f, .6f, 1.0f};
    base.stroke_width = 1.5f;
    base.shadow_hash = 91;
    GpuStyledGlyphCache cache;
    auto pixels = std::make_shared<const std::vector<float>>(4, 1.0f);
    cache.store_styled(base, 1, 1, pixels);
    REQUIRE(cache.find_styled(base));

    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK_FALSE(key == base);
        CHECK_FALSE(cache.find_styled(key));
    };
    changed([](auto& key) { key.font_id++; });
    changed([](auto& key) { key.glyph_id++; });
    changed([](auto& key) { key.font_size++; });
    changed([](auto& key) { key.stroke_radius++; });
    changed([](auto& key) { key.background_padding++; });
    changed([](auto& key) { key.fill.r += 0.1f; });
    changed([](auto& key) { key.fill.g += 0.1f; });
    changed([](auto& key) { key.fill.b += 0.1f; });
    changed([](auto& key) { key.fill.a -= 0.1f; });
    changed([](auto& key) { key.background.r += 0.1f; });
    changed([](auto& key) { key.background.g += 0.1f; });
    changed([](auto& key) { key.background.b += 0.1f; });
    changed([](auto& key) { key.background.a -= 0.1f; });
    changed([](auto& key) { key.stroke.r += 0.1f; });
    changed([](auto& key) { key.stroke.g += 0.1f; });
    changed([](auto& key) { key.stroke.b += 0.1f; });
    changed([](auto& key) { key.stroke.a -= 0.1f; });
    changed([](auto& key) { key.stroke_width += 0.1f; });
    changed([](auto& key) { key.shadow_hash++; });

    auto positive_zero = base;
    positive_zero.stroke_width = 0.0f;
    auto negative_zero = positive_zero;
    negative_zero.stroke_width = -0.0f;
    GpuStyledGlyphCache zero_cache;
    zero_cache.store_styled(positive_zero, 1, 1, pixels);
    CHECK(zero_cache.find_styled(negative_zero));
}

TEST_CASE("MTSDF prepared glyph V2 identity covers raster dependencies") {
    using text::MtsdfGlyphCacheKey;
    using text::MtsdfGlyphCacheKeyHash;
    MtsdfGlyphCacheKey base;
    base.font_digest.bytes[0] = std::byte{0x12};
    base.glyph_id = 65;
    base.variation_axes = "wght=400";
    base.profile = {64, 16, 24, 1};
    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK_FALSE(key == base);
        CHECK(MtsdfGlyphCacheKeyHash{}(key) != MtsdfGlyphCacheKeyHash{}(base));
    };
    changed([](auto& key) { key.font_digest.bytes[0] = std::byte{0x13}; });
    changed([](auto& key) { key.glyph_id++; });
    changed([](auto& key) { key.variation_axes = "wght=700"; });
    changed([](auto& key) { key.profile.em_px++; });
    changed([](auto& key) { key.profile.distance_range_px++; });
    changed([](auto& key) { key.profile.padding_px++; });
    changed([](auto& key) { key.profile.generation_id++; });
}

TEST_CASE("Semantic transform hashes cover each matrix field") {
    Mat4 base{1.0f};
    const auto original = graph::hash_mat4_v2(base);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            auto changed = base;
            changed[column][row] += 0.25f;
            INFO("matrix[", column, "][", row, "]");
            CHECK(graph::hash_mat4_v2(changed) != original);
        }
    }
    auto positive_zero = base;
    auto negative_zero = base;
    positive_zero[1][2] = 0.0f;
    negative_zero[1][2] = -0.0f;
    CHECK(graph::hash_mat4_v2(positive_zero) == graph::hash_mat4_v2(negative_zero));
    CHECK(graph::hash_vec2_v2(Vec2{0.0f, 2.0f}) ==
          graph::hash_vec2_v2(Vec2{-0.0f, 2.0f}));
    CHECK(graph::hash_vec3_v2(Vec3{0.0f, 2.0f, 3.0f}) ==
          graph::hash_vec3_v2(Vec3{-0.0f, 2.0f, 3.0f}));
}

TEST_CASE("Semantic V2 scalar and color hashes canonicalize and cover fields") {
    CHECK(graph::hash_float_v2(0.0f) == graph::hash_float_v2(-0.0f));
    const Color base{0.1f, 0.2f, 0.3f, 0.4f};
    const auto original = graph::hash_color_v2(base);
    for (int channel = 0; channel < 4; ++channel) {
        auto changed = base;
        switch (channel) {
            case 0: changed.r += 0.1f; break;
            case 1: changed.g += 0.1f; break;
            case 2: changed.b += 0.1f; break;
            default: changed.a += 0.1f; break;
        }
        CHECK(graph::hash_color_v2(changed) != original);
    }
    CHECK(graph::hash_color_v2(Color{0.0f, 0.2f, 0.3f, 0.4f}) ==
          graph::hash_color_v2(Color{-0.0f, 0.2f, 0.3f, 0.4f}));
}

TEST_CASE("Font identity memo stamp V2 key frames metadata validity") {
    using text::FontIdentityStampKey;
    using text::FontIdentityStampKeyHash;
    const FontIdentityStampKey base{"/font/inter.ttf", 2048, 1234, true, true};
    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK_FALSE(key == base);
        CHECK(FontIdentityStampKeyHash{}(key) != FontIdentityStampKeyHash{}(base));
    };
    changed([](auto& key) { key.resolved_path += ".new"; });
    changed([](auto& key) { key.byte_size++; });
    changed([](auto& key) { key.write_time_ticks++; });
    changed([](auto& key) { key.byte_size_known = false; });
    changed([](auto& key) { key.write_time_known = false; });
}

TEST_CASE("Mesh preparation cache compares the complete resolved identity") {
    using assets::MeshIdentity;
    using assets::MeshPreparationCache;
    MeshIdentity base;
    base.resolved_path = "/assets/mesh.glb";
    base.byte_size = 64;
    base.write_time = 128;
    base.content_digest.bytes[0] = std::byte{0x12};
    const auto changed = [&](auto mutate) {
        auto identity = base;
        mutate(identity);
        CHECK_FALSE(identity == base);
        CHECK(assets::MeshIdentityHash{}(identity) != assets::MeshIdentityHash{}(base));
    };
    changed([](auto& key) { key.resolved_path += ".copy"; });
    changed([](auto& key) { key.byte_size++; });
    changed([](auto& key) { key.write_time++; });
    changed([](auto& key) { key.content_digest.bytes[31] = std::byte{0x34}; });

    MeshPreparationCache cache;
    auto source = std::make_shared<const assets::PreparedMeshSource>();
    cache.store(base, source);
    CHECK(cache.find(base).value().get() == source.get());
    auto other = base;
    other.resolved_path += "\nline";
    CHECK_FALSE(cache.find(other).has_value());
}

TEST_CASE("Layer V2 identity covers pixel-affecting layer fields") {
    Layer base;
    base.name = "layer";
    base.duration = Frame{100};
    base.card3d_material().thickness_px = 10.0f;
    base.backdrop_blur = BackdropBlurParams{};
    base.backdrop_blur->blur = 12.0f;
    base.backdrop_blur->brightness = 0.8f;
    base.backdrop_blur->saturation = 0.7f;
    base.backdrop_blur->rim_light = 0.2f;
    base.backdrop_blur->refraction = 1.0f;
    base.backdrop_blur->surface_noise = 0.03f;
    const auto original = base.get_static_hash();
    const auto changed = [&](auto mutate) {
        auto value = base;
        mutate(value);
        CHECK(value.get_static_hash() != original);
    };
    changed([](auto& l) { l.transform.skew.x += 1.0f; });
    changed([](auto& l) { l.time_offset = Frame{2}; });
    changed([](auto& l) { l.track_matte.type = TrackMatteType::Alpha; l.track_matte.source_layer = "matte"; });
    changed([](auto& l) { l.card3d_material().enabled = false; });
    changed([](auto& l) { l.card3d_material().thickness_px += 1.0f; });
    changed([](auto& l) { l.card3d_material().corner_radius += 1.0f; });
    changed([](auto& l) { l.card3d_material().front_top_color.r -= 0.1f; });
    changed([](auto& l) { l.card3d_material().front_bottom_color.g -= 0.1f; });
    changed([](auto& l) { l.card3d_material().side_color.b -= 0.1f; });
    changed([](auto& l) { l.card3d_material().edge_highlight_intensity += 0.1f; });
    changed([](auto& l) { l.card3d_material().edge_highlight_color.a -= 0.1f; });
    changed([](auto& l) { l.card3d_material().rim_light_intensity += 0.1f; });
    changed([](auto& l) { l.card3d_material().rim_light_power += 0.1f; });
    changed([](auto& l) { l.card3d_material().rim_light_color.r -= 0.1f; });
    changed([](auto& l) { l.card3d_material().light_direction.z -= 0.1f; });
    changed([](auto& l) { l.card3d_material().cast_shadow = false; });
    changed([](auto& l) { l.card3d_material().receive_shadow = false; });
    changed([](auto& l) { l.card3d_material().casts_shadows = false; });
    changed([](auto& l) { l.card3d_material().accepts_shadows = false; });
    changed([](auto& l) { l.backdrop_blur->blur += 1.0f; });
    changed([](auto& l) { l.backdrop_blur->brightness += 0.1f; });
    changed([](auto& l) { l.backdrop_blur->saturation += 0.1f; });
    changed([](auto& l) { l.backdrop_blur->rim_light += 0.1f; });
    changed([](auto& l) { l.backdrop_blur->refraction += 0.1f; });
    changed([](auto& l) { l.backdrop_blur->surface_noise += 0.01f; });
    changed([](auto& l) { l.kind = LayerKind::Video; l.video_source = std::make_unique<video::VideoSource>(); l.video_source->path = "changed.mp4"; });
}

TEST_CASE("SceneHasher V2 includes layer material, matte, source and remapped time") {
    const auto fingerprint = [](Layer layer) {
        Scene scene;
        scene.add_layer(std::move(layer));
        graph::SceneHasher hasher;
        return hasher.compute_fingerprint(scene, Frame{10});
    };
    Layer base;
    base.name = "layer";
    base.duration = Frame{100};
    const auto original = fingerprint(base);
    auto skewed = base;
    skewed.transform.skew.x = 12.0f;
    CHECK(fingerprint(std::move(skewed)) != original);
    auto matte = base;
    matte.track_matte.type = TrackMatteType::Alpha;
    matte.track_matte.source_layer = "matte";
    CHECK(fingerprint(std::move(matte)) != original);
    auto video_a = base;
    video_a.kind = LayerKind::Video;
    video_a.video_source = std::make_unique<video::VideoSource>();
    video_a.video_source->path = "clip.mp4";
    auto video_b = video_a;
    video_b.video_source->source_start = Frame{5};
    CHECK(fingerprint(std::move(video_a)) != fingerprint(std::move(video_b)));

    Layer remapped;
    remapped.name = "remapped";
    remapped.duration = Frame{100};
    remapped.time_remap.speed = 0.5f;
    Scene scene;
    scene.add_layer(std::move(remapped));
    graph::SceneHasher hasher;
    CHECK(hasher.compute_dynamic_fingerprint(scene, Frame{10}) !=
          hasher.compute_dynamic_fingerprint(scene, Frame{11}));
}
