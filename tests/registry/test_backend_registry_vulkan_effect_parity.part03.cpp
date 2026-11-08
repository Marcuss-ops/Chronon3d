#include "test_backend_registry_vulkan_effect_parity_test_support.hpp"

#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("Strict Vulkan DropShadow matches software and invalid parameters fail closed") {
    auto drop_shadow = renderer::create_drop_shadow_effect_processor();
    const std::array<DropShadowParams, 4> cases{{
        {.offset = {0.0f, 0.0f}, .color = Color{0.0f, 0.0f, 0.0f, 0.5f}, .radius = 0.0f},
        {.offset = {2.0f, 2.0f}, .color = Color{0.2f, 0.1f, 0.0f, 0.5f}, .radius = 0.5f},
        {.offset = {-2.4f, 1.6f}, .color = Color{0.0f, 0.0f, 0.0f, 0.5f}, .radius = 3.0f},
        {.offset = {1.5f, -2.5f}, .color = Color{0.1f, 0.3f, 0.2f, 0.65f}, .radius = 5.0f},
    }};
    auto shadow_source = std::make_shared<Framebuffer>(kW, kH);
    shadow_source->clear(Color::transparent());
    for (int y = 0; y < kH; ++y)
        for (int x = 1; x < kW; ++x) {
            const float alpha = y == 1 ? 0.5f : (y == 3 ? 0.25f : 1.0f);
            shadow_source->set_pixel(x, y, Color{
                0.13f * static_cast<float>(x) * alpha,
                0.17f * static_cast<float>(y + 1) * alpha,
                0.09f * static_cast<float>(x + y) * alpha, alpha});
        }
    const std::optional<raster::BBox> partial_clip{raster::BBox{2, 1, 6, 3}};
    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        const auto& params = cases[case_index];
        const auto clip = case_index == 3 ? partial_clip : std::nullopt;
        CAPTURE(case_index);
        CAPTURE(params.offset.x);
        CAPTURE(params.offset.y);
        CAPTURE(params.radius);
        const auto expected = software_oracle(
            *drop_shadow, params, clip, 0, 0, nullptr, Frame{}, shadow_source.get());
        const auto actual = vulkan_strict(
            params, clip, 0, 0, Frame{}, shadow_source.get());
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        REQUIRE(actual.pixels.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(actual.pixels[i] == doctest::Approx(expected[i]).epsilon(kTolerance));
    }

    const auto oversized_shadow = vulkan_strict(
        DropShadowParams{.offset = {2.0f, 2.0f}, .color = Color{0, 0, 0, 0.5f}, .radius = 32.1f},
        std::nullopt);
    CHECK_FALSE(oversized_shadow.ok);

    const auto nan_run = vulkan_strict(BrightnessParams{.value = std::nanf("")}, std::nullopt);
    CHECK_FALSE(nan_run.ok);
}

TEST_CASE("Strict Vulkan DirectionalBlur matches software and invalid parameters fail closed") {
    auto directional = renderer::create_directional_blur_effect_processor();
    // The motion processors require a scratch area, as the engine always provides.
    EffectScratchResources scratch;
    const std::array<DirectionalBlurParams, 6> cases{{
        {.angle = 0.0f, .length = 3.5f, .samples = 0},
        {.angle = 90.0f, .length = 5.0f, .samples = 0},
        {.angle = 37.0f, .length = 7.25f, .samples = 0},
        {.angle = 180.0f, .length = 8.0f, .samples = 5},
        {.angle = -45.0f, .length = 16.0f, .samples = 0},
        {.angle = 123.0f, .length = 64.0f, .samples = 0},
    }};
    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        CAPTURE(case_index);
        CAPTURE(cases[case_index].angle);
        CAPTURE(cases[case_index].length);
        CAPTURE(cases[case_index].samples);
        check_parity(*directional, cases[case_index], std::nullopt, 0, 0, &scratch);
    }

    // Length zero is the CPU identity fast path: strict native forwards input.
    check_parity(*directional, DirectionalBlurParams{.angle = 30.0f, .length = 0.0f, .samples = 0},
                 std::nullopt, 0, 0, &scratch);

    // Certified window is length (0, 64] with taps in [2, 64]; outside fails closed.
    CHECK_FALSE(vulkan_strict(DirectionalBlurParams{.angle = 0.0f, .length = 65.0f, .samples = 0}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(DirectionalBlurParams{.angle = 0.0f, .length = 8.0f, .samples = 65}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(DirectionalBlurParams{.angle = std::nanf(""), .length = 8.0f, .samples = 0}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(DirectionalBlurParams{.angle = 0.0f, .length = std::nanf(""), .samples = 0}, std::nullopt).ok);

    // Partial clips are not certified: fail closed to the software processor.
    const std::optional<raster::BBox> partial_clip{raster::BBox{2, 1, 6, 3}};
    CHECK_FALSE(vulkan_strict(DirectionalBlurParams{.angle = 20.0f, .length = 6.0f, .samples = 0}, partial_clip).ok);
}

TEST_CASE("Strict Vulkan SdfBevel matches software shading and fails closed outside the window") {
    auto sdf_bevel = renderer::create_sdf_bevel_effect_processor();
    const SdfBevelParams base{
        .bevel_width = 6.0f,
        .bevel_height = 4.0f,
        .rim_width = 1.5f,
        .rim_color = Color{0.78f, 0.58f, 0.30f, 1.0f},
        .rim_intensity = 0.8f,
        .highlight = 0.45f,
        .shade = 0.3f,
        .light_angle = -135.0f,
        .light_elevation = 38.0f,
        .light_orbit_speed = 0.0f,
        .shadow_distance = 0.0f,
        .shadow_softness = 0.0f,
        .shadow_opacity = 0.0f};
    check_parity(*sdf_bevel, base);

    // The certified native window is explicit; malformed or oversized plans
    // fall back to the software processor instead of being silently approved.
    auto oversized = base;
    oversized.bevel_width = 257.0f;
    CHECK_FALSE(vulkan_strict(oversized, std::nullopt).ok);
    auto nan_light = base;
    nan_light.light_angle = std::nanf("");
    CHECK_FALSE(vulkan_strict(nan_light, std::nullopt).ok);
}

#endif // CHRONON3D_ENABLE_VULKAN
