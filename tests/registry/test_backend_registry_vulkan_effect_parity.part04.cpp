#include "test_backend_registry_vulkan_effect_parity_test_support.hpp"

#ifdef CHRONON3D_ENABLE_VULKAN

TEST_CASE("Strict Vulkan CameraRing matches software within jitter tolerance and invalid parameters fail closed") {
    auto camera_ring = renderer::create_camera_ring_effect_processor();
    // Zero-blur / safe-zone bypass is exact.
    check_parity(*camera_ring,
                 CameraRingParams{.center = {0.5f, 0.5f}, .axis_scale = {0.5f, 0.5f},
                                  .inner_radius = 0.0f, .outer_radius = 0.5f,
                                  .max_blur_radius = 0.0f, .chroma_dispersion = 0.0f,
                                  .anamorphic_stretch = 0.5f, .samples = 8});

    // Active defocus: CPU uses integer-hash per-pixel jitter while GPU uses
    // interleaved gradient noise, so individual texels differ slightly by
    // stochastic phase while remaining close.
    const CameraRingParams active{
        .center = {0.5f, 0.5f}, .axis_scale = {0.68f, 0.32f},
        .inner_radius = 0.22f, .outer_radius = 0.78f,
        .max_blur_radius = 0.022f, .chroma_dispersion = 0.007f,
        .anamorphic_stretch = 1.0f, .samples = 16};
    const auto expected = software_oracle(*camera_ring, active, std::nullopt);
    const auto actual = vulkan_strict(active, std::nullopt);
    INFO("strict Vulkan error: " << actual.error);
    REQUIRE(actual.ok);
    REQUIRE(actual.pixels.size() == expected.size());
    float max_err = 0.0f;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        max_err = std::max(max_err, std::abs(actual.pixels[i] - expected[i]));
    }
    CHECK_MESSAGE(max_err <= 0.01f, "CameraRing max abs error = " << max_err);

    // Outside certified parameter window or NaN must fail closed.
    CHECK_FALSE(vulkan_strict(
        CameraRingParams{.max_blur_radius = 0.30f, .samples = 16}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(
        CameraRingParams{.samples = 65}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(
        CameraRingParams{.center = {std::nanf(""), 0.5f}, .samples = 16}, std::nullopt).ok);
}

TEST_CASE("Strict Vulkan InnerGlow matches software and uncertified parameters fail closed") {
    auto inner_glow = renderer::create_inner_glow_effect_processor();
    EffectScratchResources scratch;
    const std::array<InnerGlowParams, 2> cases{{
        {.size = 2.0f, .choke = 0.0f, .intensity = 1.0f,
         .color_r = 1.0f, .color_g = 0.8f, .color_b = 0.2f, .color_a = 1.0f},
        {.size = 6.0f, .choke = 0.0f, .intensity = 1.5f,
         .color_r = 0.2f, .color_g = 0.6f, .color_b = 1.0f, .color_a = 0.8f},
    }};
    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        CAPTURE(case_index);
        const auto expected = software_oracle(
            *inner_glow, cases[case_index], std::nullopt, 0, 0, &scratch);
        const auto actual = vulkan_strict(cases[case_index], std::nullopt);
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        REQUIRE(actual.pixels.size() == expected.size());
        float max_rgb_err = 0.0f;
        for (std::size_t i = 0; i < expected.size(); i += 4) {
            CHECK(actual.pixels[i + 3] == doctest::Approx(expected[i + 3]).epsilon(kTolerance));
            for (int c = 0; c < 3; ++c) {
                max_rgb_err = std::max(
                    max_rgb_err, std::abs(actual.pixels[i + c] - expected[i + c]));
            }
        }
        // CPU uses 1-pass integer box blur while GPU uses continuous Gaussian blur.
        CHECK_MESSAGE(max_rgb_err <= 0.15f, "InnerGlow max_rgb_err = " << max_rgb_err);
    }
    // Certified GPU window is choke=0, size in (0, 16]; outside fails closed.
    CHECK_FALSE(vulkan_strict(InnerGlowParams{.size = 0.0f, .choke = 0.0f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(InnerGlowParams{.size = 3.5f, .choke = 0.35f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(InnerGlowParams{.size = 17.0f, .choke = 0.0f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(InnerGlowParams{.size = -1.0f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(InnerGlowParams{.intensity = std::nanf("")}, std::nullopt).ok);
}

TEST_CASE("Strict Vulkan GlowAdvanced matches software and uncertified parameters fail closed") {
    auto glow_adv = renderer::create_glow_advanced_effect_processor();
    EffectScratchResources scratch;
    const std::array<GlowAdvancedParams, 3> cases{{
        {.threshold = 0.2f, .radius = 2.0f, .intensity = 0.8f,
         .color_r = 1.0f, .color_g = 0.5f, .color_b = 0.1f, .color_a = 1.0f,
         .color_b_r = 0.1f, .color_b_g = 0.7f, .color_b_b = 1.0f, .color_b_a = 1.0f,
         .falloff = 1.0f, .color_cycle = 0.0f},
        {.threshold = 0.0f, .radius = 4.0f, .intensity = 1.4f,
         .color_r = 0.9f, .color_g = 0.2f, .color_b = 0.8f, .color_a = 1.0f,
         .color_b_r = 1.0f, .color_b_g = 1.0f, .color_b_b = 0.4f, .color_b_a = 1.0f,
         .falloff = 1.0f, .color_cycle = 0.0f},
        {.threshold = 0.5f, .radius = 0.0f, .intensity = 0.0f},
    }};
    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        CAPTURE(case_index);
        check_parity(*glow_adv, cases[case_index], std::nullopt, 0, 0, &scratch);
    }
    // Certified GPU window is falloff=1, color_cycle=0, radius<=96; outside fails closed.
    CHECK_FALSE(vulkan_strict(GlowAdvancedParams{.radius = 2.0f, .falloff = 1.5f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(GlowAdvancedParams{.radius = 2.0f, .falloff = 1.0f, .color_cycle = 0.25f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(GlowAdvancedParams{.radius = 97.0f, .falloff = 1.0f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(GlowAdvancedParams{.falloff = 0.5f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(GlowAdvancedParams{.threshold = std::nanf("")}, std::nullopt).ok);
}

TEST_CASE("Strict Vulkan OpticsCompensation matches software and invalid parameters fail closed") {
    auto optics = renderer::create_optics_compensation_effect_processor();
    EffectScratchResources scratch;
    const std::array<OpticsCompensationParams, 4> cases{{
        {.fov_degrees = 0.0f, .reverse = 0.0f, .center_x = 0.5f, .center_y = 0.5f, .scale = 1.0f},
        {.fov_degrees = 45.0f, .reverse = 0.0f, .center_x = 0.5f, .center_y = 0.5f, .scale = 1.0f},
        {.fov_degrees = 65.0f, .reverse = 1.0f, .center_x = 0.35f, .center_y = 0.65f, .scale = 1.25f},
        {.fov_degrees = 90.0f, .reverse = 0.0f, .center_x = 0.7f, .center_y = 0.3f, .scale = 0.8f},
    }};
    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        CAPTURE(case_index);
        check_parity(*optics, cases[case_index], std::nullopt, 0, 0, &scratch);
    }
    CHECK_FALSE(vulkan_strict(OpticsCompensationParams{.fov_degrees = 121.0f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(OpticsCompensationParams{.scale = 0.05f}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(OpticsCompensationParams{.fov_degrees = std::nanf("")}, std::nullopt).ok);
}

#endif // CHRONON3D_ENABLE_VULKAN
