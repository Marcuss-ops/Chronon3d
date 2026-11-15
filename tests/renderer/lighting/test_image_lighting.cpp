#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/render_graph/nodes/lighting_node.hpp>
#include <chronon3d/rendering/image_lighting.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::rendering;

namespace {

ImageTexelView view_of(std::vector<Color>& pixels, i32 width, i32 height) {
    return ImageTexelView{
        .pixels = pixels.data(),
        .width = width,
        .height = height,
        .stride = width,
    };
}

LightContext directional(Vec3 direction, f32 intensity = 1.0f) {
    LightContext light;
    light.enabled = true;
    light.ambient_enabled = false;
    light.directional_enabled = true;
    light.direction = direction;
    light.directional_color = {1.0f, 1.0f, 1.0f, 1.0f};
    light.diffuse = intensity;
    return light;
}

} // namespace

TEST_CASE("ImageLighting: flat luminance yields a flat normal") {
    std::vector<Color> pixels(9, Color{0.5f, 0.5f, 0.5f, 1.0f});
    const auto view = view_of(pixels, 3, 3);

    const Vec3 normal = compute_image_normal(view, 1, 1, 0.5f);
    CHECK(normal.x == doctest::Approx(0.0f));
    CHECK(normal.y == doctest::Approx(0.0f));
    CHECK(normal.z == doctest::Approx(1.0f));
}

TEST_CASE("ImageLighting: brightness gradient tilts the normal toward the dark side") {
    std::vector<Color> pixels(9);
    for (i32 y = 0; y < 3; ++y) {
        pixels[y * 3 + 0] = Color{0.1f, 0.1f, 0.1f, 1.0f};
        pixels[y * 3 + 1] = Color{0.5f, 0.5f, 0.5f, 1.0f};
        pixels[y * 3 + 2] = Color{0.9f, 0.9f, 0.9f, 1.0f};
    }
    const auto view = view_of(pixels, 3, 3);

    const Vec3 normal = compute_image_normal(view, 1, 1, 1.0f);
    CHECK(normal.x < 0.0f);
    CHECK(normal.y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(normal.z > 0.0f);
    CHECK(glm::length(normal) == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("ImageLighting: vertical brightness grows along screen Y") {
    std::vector<Color> pixels(9, Color{0.2f, 0.2f, 0.2f, 1.0f});
    pixels[3] = pixels[4] = pixels[5] = Color{0.5f, 0.5f, 0.5f, 1.0f};
    pixels[6] = pixels[7] = pixels[8] = Color{0.9f, 0.9f, 0.9f, 1.0f};
    const auto view = view_of(pixels, 3, 3);

    // Screen rows grow downward, so a surface brighter below receives a
    // positive world-space Y normal component.
    const Vec3 normal = compute_image_normal(view, 1, 1, 1.0f);
    CHECK(normal.y > 0.0f);
}

TEST_CASE("ImageLighting: alpha blur points the edge outward and masks the contour") {
    std::vector<Color> pixels(49, Color{0.0f, 0.0f, 0.0f, 0.0f});
    for (i32 y = 0; y < 7; ++y)
        for (i32 x = 2; x <= 4; ++x)
            pixels[y * 7 + x] = Color{1.0f, 1.0f, 1.0f, 1.0f};
    const auto view = view_of(pixels, 7, 7);

    const auto blurred = blur_alpha_box(view, 2);
    REQUIRE(blurred.size() == 49);

    // At the left contour the alpha rises to the right, so the outward
    // direction points left.
    const auto edge = sample_alpha_edge(blurred, 7, 7, 2, 3, 1.0f);
    CHECK(edge.outward.x < -0.9f);
    CHECK(std::abs(edge.outward.y) < 1e-5f);
    CHECK(edge.mask > 0.0f);
    CHECK(edge.mask <= 1.0f);
}

TEST_CASE("ImageLighting: rim follows the light side of the silhouette") {
    ImageLightingParams params;
    params.bump_strength = 0.0f;
    params.diffuse = 0.0f;
    params.specular_intensity = 0.0f;
    params.ambient_multiplier = 0.0f;
    params.rim_strength = 1.0f;
    params.rim_power = 2.0f;

    ImageLightingSample sample;
    sample.normal_world = {0.0f, 0.0f, 1.0f};
    sample.edge_mask = 1.0f;
    sample.edge_outward = {1.0f, 0.0f};

    const auto light = directional(glm::normalize(Vec3{1.0f, 0.0f, 0.5f}));
    const Color lit_side = evaluate_image_lighting(sample, light, params);

    sample.edge_outward = {-1.0f, 0.0f};
    const Color shadow_side = evaluate_image_lighting(sample, light, params);

    CHECK(lit_side.r > shadow_side.r);
    CHECK(shadow_side.r == doctest::Approx(0.0f));
}

TEST_CASE("ImageLighting: linear HDR accumulation is not clamped") {
    ImageLightingParams params;
    params.bump_strength = 0.0f;
    params.diffuse = 1.0f;
    params.specular_intensity = 0.0f;
    params.ambient_multiplier = 0.0f;

    ImageLightingSample sample;
    sample.albedo = {0.5f, 0.5f, 0.5f, 1.0f};
    sample.normal_world = {0.0f, 0.0f, 1.0f};

    const auto light = directional({0.0f, 0.0f, 1.0f}, 3.0f);
    const Color result = evaluate_image_lighting(sample, light, params);

    CHECK(result.r == doctest::Approx(1.5f));
    CHECK(result.r > 1.0f);
}

TEST_CASE("ImageLighting: light wrap bleeds the blurred background into the edge") {
    ImageLightingParams params;
    params.bump_strength = 0.0f;
    params.diffuse = 0.0f;
    params.specular_intensity = 0.0f;
    params.ambient_multiplier = 1.0f;
    params.light_wrap_strength = 0.5f;

    auto light = directional({0.0f, 0.0f, 1.0f}, 0.0f);
    light.ambient_enabled = true;
    light.ambient = 0.05f;

    ImageLightingSample sample;
    sample.albedo = {0.1f, 0.1f, 0.1f, 1.0f};
    sample.edge_mask = 1.0f;
    sample.background_wrap = {1.0f, 0.8f, 0.4f, 1.0f};

    const Color wrapped = evaluate_image_lighting(sample, light, params);
    params.light_wrap_strength = 0.0f;
    const Color plain = evaluate_image_lighting(sample, light, params);

    CHECK(wrapped.r > plain.r);
    CHECK(wrapped.g > plain.g);
    CHECK(wrapped.b > plain.b);
}

TEST_CASE("ImageLighting: gobo is sampled in world space") {
    std::vector<Color> gobo_pixels{
        Color{1.0f, 1.0f, 1.0f, 1.0f},
        Color{0.0f, 0.0f, 0.0f, 1.0f}};
    auto gobo = view_of(gobo_pixels, 2, 1);

    CHECK(sample_gobo(gobo, gobo_uv({-0.25f, 0.0f}, 1.0f, {0.0f, 0.0f})) ==
          doctest::Approx(1.0f));
    CHECK(sample_gobo(gobo, gobo_uv({0.25f, 0.0f}, 1.0f, {0.0f, 0.0f})) ==
          doctest::Approx(0.0f));

    ImageLightingParams params;
    params.gobo = &gobo;
    params.gobo_scale = 1.0f;
    params.bump_strength = 0.0f;
    params.diffuse = 1.0f;
    params.ambient_multiplier = 0.0f;
    params.specular_intensity = 0.0f;

    const auto light = directional({0.0f, 0.0f, 1.0f});
    ImageLightingSample sample;
    sample.normal_world = {0.0f, 0.0f, 1.0f};
    sample.pixel_world = {-0.25f, 0.0f, 0.0f};
    const Color open = evaluate_image_lighting(sample, light, params);
    sample.pixel_world = {0.25f, 0.0f, 0.0f};
    const Color masked = evaluate_image_lighting(sample, light, params);

    CHECK(open.r > masked.r);
}

TEST_CASE("ImageLighting: 8-bit dither is deterministic and sub-LSB") {
    const f32 a = dither_offset_8bit(10, 20);
    const f32 b = dither_offset_8bit(10, 20);
    CHECK(a == doctest::Approx(b));
    CHECK(std::abs(a) <= 0.5f / 255.0f + 1e-7f);

    bool differs = false;
    for (i32 y = 0; y < 8 && !differs; ++y)
        for (i32 x = 0; x < 8; ++x)
            if (dither_offset_8bit(x, y) != a) differs = true;
    CHECK(differs);
}

TEST_CASE("ImageLighting: LightingNode rejects GPU-only input for image relighting") {
    // An external-null framebuffer has a native handle but no CPU pixels.
    Framebuffer source(4, 4, static_cast<Color*>(nullptr));
    source.set_surface_handle(0x1234U);
    const std::array<FramebufferRef, 1> inputs{FramebufferRef{source}};

    chronon3d::graph::RenderGraphContext ctx;
    ctx.frame_input.width = 4;
    ctx.frame_input.height = 4;
    ctx.frame_input.light_context = directional({0.0f, 0.0f, 1.0f});

    Material2_5D material;
    material.image_bump_strength = 0.5f;
    auto node = chronon3d::graph::LightingNode::create(
        "gpu-only", Mat4{1.0f}, material);
    auto result = node->execute(ctx, inputs, {});

    CHECK_FALSE(result.ok());
    CHECK(source.surface_handle() == 0x1234U);
}

TEST_CASE("ImageLighting: LightingNode applies the alpha-edge rim on CPU") {
    Framebuffer source(9, 9, false);
    source.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    for (i32 y = 1; y <= 7; ++y)
        for (i32 x = 1; x <= 7; ++x)
            source.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
    const std::array<FramebufferRef, 1> inputs{FramebufferRef{source}};

    chronon3d::graph::RenderGraphContext ctx;
    ctx.frame_input.width = 9;
    ctx.frame_input.height = 9;
    ctx.frame_input.light_context =
        directional(glm::normalize(Vec3{1.0f, 0.0f, 0.2f}));

    Material2_5D material;
    material.image_bump_strength = 0.0f;
    material.image_rim_strength = 1.0f;
    material.image_rim_width_px = 2.0f;
    material.diffuse = 0.0f;
    material.specular = 0.0f;
    material.ambient_multiplier = 0.0f;

    auto node = chronon3d::graph::LightingNode::create(
        "cutout", Mat4{1.0f}, material);
    auto result = node->execute(ctx, inputs, {});
    REQUIRE(result.ok());
    auto output = result.take_value();
    REQUIRE(output);
    CHECK(output->is_cpu_authoritative());

    const Color right_edge = output->get_pixel(7, 4);
    const Color left_edge = output->get_pixel(1, 4);
    CHECK(right_edge.r > left_edge.r);
    CHECK(right_edge.r > 0.1f);
    CHECK(left_edge.r == doctest::Approx(0.0f));
}

TEST_CASE("ImageLighting: LightingNode relights a luminance gradient") {
    Framebuffer source(9, 9, false);
    source.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    for (i32 y = 1; y <= 7; ++y)
        for (i32 x = 1; x <= 7; ++x) {
            const f32 value = 0.2f + 0.08f * static_cast<f32>(x - 1);
            source.set_pixel(x, y, Color{value, value, value, 1.0f});
        }
    const std::array<FramebufferRef, 1> inputs{FramebufferRef{source}};

    chronon3d::graph::RenderGraphContext ctx;
    ctx.frame_input.width = 9;
    ctx.frame_input.height = 9;
    ctx.frame_input.light_context =
        directional(glm::normalize(Vec3{1.0f, 0.0f, 0.2f}));

    Material2_5D material;
    material.image_bump_strength = 0.0f;
    material.image_rim_strength = 0.001f; // force the image branch with a flat normal
    material.image_rim_width_px = 1.0f;
    auto flat_node = chronon3d::graph::LightingNode::create(
        "flat", Mat4{1.0f}, material);
    auto flat_result = flat_node->execute(ctx, inputs, {});
    REQUIRE(flat_result.ok());
    auto flat_output = flat_result.take_value();
    REQUIRE(flat_output);

    material.image_bump_strength = 1.0f;
    auto bump_node = chronon3d::graph::LightingNode::create(
        "bump", Mat4{1.0f}, material);
    auto bump_result = bump_node->execute(ctx, inputs, {});
    REQUIRE(bump_result.ok());
    auto bump_output = bump_result.take_value();
    REQUIRE(bump_output);

    CHECK(bump_output->get_pixel(4, 4).r !=
          doctest::Approx(flat_output->get_pixel(4, 4).r));
}

TEST_CASE("ImageLighting: material hash reacts to the new controls") {
    Material2_5D base;
    const u64 base_hash = hash_material_2_5d(base);

    Material2_5D bump = base;
    bump.image_bump_strength = 0.2f;
    CHECK(hash_material_2_5d(bump) != base_hash);

    Material2_5D rim = base;
    rim.image_rim_strength = 0.6f;
    CHECK(hash_material_2_5d(rim) != base_hash);

    Material2_5D width = base;
    width.image_rim_width_px = 8.0f;
    CHECK(hash_material_2_5d(width) != base_hash);

    Material2_5D gobo = base;
    gobo.image_gobo_asset = "gobo.png";
    CHECK(hash_material_2_5d(gobo) != base_hash);

    Material2_5D wrap = base;
    wrap.image_light_wrap_asset = "background.png";
    wrap.image_light_wrap_strength = 0.5f;
    CHECK(hash_material_2_5d(wrap) != base_hash);
}

TEST_CASE("ImageLighting: LightingNode fails closed when a gobo asset is unavailable") {
    Framebuffer source(4, 4, false);
    source.clear(Color{1.0f, 1.0f, 1.0f, 1.0f});
    const std::array<FramebufferRef, 1> inputs{FramebufferRef{source}};

    chronon3d::graph::RenderGraphContext ctx;
    ctx.frame_input.width = 4;
    ctx.frame_input.height = 4;
    ctx.frame_input.light_context = directional({0.0f, 0.0f, 1.0f});

    Material2_5D material;
    material.image_bump_strength = 0.1f;
    material.image_gobo_asset = "missing-gobo.png";
    auto node = chronon3d::graph::LightingNode::create(
        "missing-gobo", Mat4{1.0f}, material);
    auto result = node->execute(ctx, inputs, {});

    CHECK_FALSE(result.ok());
}
