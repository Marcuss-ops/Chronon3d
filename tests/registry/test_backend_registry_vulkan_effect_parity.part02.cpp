#include "test_backend_registry_vulkan_effect_parity_test_support.hpp"

#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("Strict Vulkan rejects Echo without a native temporal surface history") {
    EffectStack stack;
    stack.push_back(effects::EffectInstance{EchoParams{
        .echoes = 2, .decay = 0.5f, .time_offset_frames = 1}});

    const auto run = vulkan_strict_stack(stack);
    CHECK_FALSE(run.ok);
    CHECK_FALSE(run.error.empty());
}

// ── Wave-2 bulk routes (TICKET-VULKAN-EFFECT-PARITY waves 2-4) ─────────────
// Software vs strict-Vulkan pixel parity for the radial/warp/generate/LUT
// bulk dispatches. Same harness contract as wave 1: the oracle is the real
// registered software EffectProcessor; the Vulkan side runs EffectStackNode
// with require_native_gpu so a missing route fails instead of silently
// falling back to software. Every parity case therefore also certifies the
// strict zero-readback route (a native surface must be produced).

namespace {

// Alpha-symmetric source: centroid sits exactly on the pixel-index centre
// ((kW-1)/2, (kH-1)/2), which keeps RadialBlur inside its certified
// centroid-stable window (no software recenter pass).
std::shared_ptr<Framebuffer> make_symmetric_source() {
    auto fb = std::make_shared<Framebuffer>(kW, kH);
    fb->set_origin(0, 0);
    fb->clear(Color::transparent());
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const float dx = std::abs(static_cast<float>(x) - (kW - 1) * 0.5f);
            const float dy = std::abs(static_cast<float>(y) - (kH - 1) * 0.5f);
            const float a = 0.2f + 0.1f * (dx + dy);
            const float r = std::fmod(0.13f * static_cast<float>(x + 1) + 0.07f * y, 1.0f);
            const float g = std::fmod(0.29f * static_cast<float>(y + 1) + 0.05f * x, 1.0f);
            fb->set_pixel(x, y, Color{r * a, g * a, 0.11f * static_cast<float>(x) * a, a});
        }
    }
    return fb;
}

// Two-input strict run for DisplacementMap (source + resolved map layer).
VulkanRun vulkan_strict_two_inputs(const EffectParams& params,
                                    const Framebuffer& source,
                                    const Framebuffer& map) {
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = kW;
    ctx.frame_input.height = kH;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;

    auto src = std::make_shared<Framebuffer>(source);
    auto map_copy = std::make_shared<Framebuffer>(map);
    EffectStack effects;
    effects::EffectInstance inst;
    inst.params = params;
    inst.effect_type = effects::detect_effect_type(params);
    effects.push_back(std::move(inst));
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 2> inputs{src.get(), map_copy.get()};
    const std::array<std::optional<raster::BBox>, 2> bboxes{
        raster::BBox{0, 0, kW, kH}, raster::BBox{0, 0, kW, kH}};
    const auto result = node.execute(ctx, inputs, bboxes);
    VulkanRun run;
    if (!result.ok()) {
        run.error = result.error().message;
        return run;
    }
    const auto handle = result.value()->surface_handle();
    if (handle == runtime::kInvalidRenderSurfaceHandle) {
        run.error = "no native surface produced";
        return run;
    }
    run.pixels.assign(static_cast<std::size_t>(kW * kH * 4), 0.0f);
    run.ok = backend.download_surface(handle, run.pixels).ok();
    (void)backend.release_surface(handle);
    surfaces.release(handle);
    return run;
}

void check_parity_pixels(const std::vector<float>& expected, const VulkanRun& actual) {
    INFO("strict Vulkan error: " << actual.error);
    REQUIRE(actual.ok);
    REQUIRE(actual.pixels.size() == expected.size());
    float max_err = 0.0f;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const float tol = kTolerance * std::max(1.0f, std::abs(expected[i]));
        const float err = std::abs(actual.pixels[i] - expected[i]);
        max_err = std::max(max_err, err);
        if (err > tol) {
            INFO("texel " << i / 4 << " channel " << i % 4 << " expected "
                 << expected[i] << " actual " << actual.pixels[i]);
            CHECK(err <= tol);
        }
    }
    MESSAGE("max abs error " << max_err);
}

} // namespace

TEST_CASE("Strict Vulkan RadialBlur matches software and invalid parameters fail closed") {
    auto radial = renderer::create_radial_blur_effect_processor();
    EffectScratchResources scratch;
    auto symmetric = make_symmetric_source();
    // Blur centre on the content centroid: (kW-1)/2 -> (3.0/7, 1.0/3).
    const Vec2 center{3.0f / 7.0f, 1.0f / 3.0f};
    const std::array<RadialBlurParams, 4> cases{{
        {.center = center, .amount = 2.0f, .render_samples = 8, .preview_samples = 4},
        {.center = center, .amount = 8.0f, .render_samples = 8, .preview_samples = 4},
        {.center = center, .amount = 5.0f, .render_samples = 0, .preview_samples = 4},
        {.center = center, .amount = 1.0f, .render_samples = 16, .preview_samples = 4},
    }};
    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        CAPTURE(case_index);
        CAPTURE(cases[case_index].amount);
        CAPTURE(cases[case_index].render_samples);
        const auto expected = software_oracle(
            *radial, cases[case_index], std::nullopt, 0, 0, &scratch, Frame{},
            symmetric.get());
        const auto actual = vulkan_strict(
            cases[case_index], std::nullopt, 0, 0, Frame{}, symmetric.get());
        check_parity_pixels(expected, actual);
    }

    // amount <= 0 is the CPU identity fast path: strict native forwards input.
    const auto identity_expected = software_oracle(
        *radial, RadialBlurParams{.center = center, .amount = 0.0f}, std::nullopt,
        0, 0, &scratch, Frame{}, symmetric.get());
    check_parity_pixels(identity_expected, vulkan_strict(
        RadialBlurParams{.center = center, .amount = 0.0f}, std::nullopt,
        0, 0, Frame{}, symmetric.get()));

    // Certified window is amount (0, 8] with taps in [2, 64]; outside fails closed.
    CHECK_FALSE(vulkan_strict(
        RadialBlurParams{.center = center, .amount = 9.0f}, std::nullopt,
        0, 0, Frame{}, symmetric.get()).ok);
    CHECK_FALSE(vulkan_strict(
        RadialBlurParams{.center = center, .amount = 4.0f, .render_samples = 65},
        std::nullopt, 0, 0, Frame{}, symmetric.get()).ok);
    CHECK_FALSE(vulkan_strict(
        RadialBlurParams{.center = {std::nanf(""), 0.5f}, .amount = 4.0f},
        std::nullopt, 0, 0, Frame{}, symmetric.get()).ok);
    const std::optional<raster::BBox> partial_clip{raster::BBox{2, 1, 6, 3}};
    CHECK_FALSE(vulkan_strict(
        RadialBlurParams{.center = center, .amount = 4.0f}, partial_clip,
        0, 0, Frame{}, symmetric.get()).ok);
}

TEST_CASE("Strict Vulkan static warps match software") {
    EffectScratchResources scratch;
    auto twirl = renderer::create_twirl_effect_processor();
    check_parity(*twirl,
        TwirlParams{.center = {0.5f, 0.5f}, .angle_degrees = 90.0f, .radius = 0.5f, .falloff = 1.0f},
        std::nullopt, 0, 0, &scratch);
    check_parity(*twirl,
        TwirlParams{.center = {0.25f, 0.75f}, .angle_degrees = -45.0f, .radius = 1.0f, .falloff = 2.0f},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        TwirlParams{.center = {0.5f, 0.5f}, .angle_degrees = 30.0f, .radius = 3.0f, .falloff = 1.0f},
        std::nullopt).ok);

    auto bulge = renderer::create_bulge_effect_processor();
    check_parity(*bulge,
        BulgeParams{.center = {0.5f, 0.5f}, .amount = 0.5f, .radius = 0.5f},
        std::nullopt, 0, 0, &scratch);
    check_parity(*bulge,
        BulgeParams{.center = {0.3f, 0.6f}, .amount = -0.75f, .radius = 1.0f,
                    .edge_mode = sampling::EdgeMode::Clamp},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        BulgeParams{.center = {0.5f, 0.5f}, .amount = 2.0f, .radius = 0.5f},
        std::nullopt).ok);

    auto pin = renderer::create_corner_pin_effect_processor();
    check_parity(*pin,
        CornerPinParams{.top_left = {0.02f, 0.0f}, .top_right = {0.98f, 0.03f},
                        .bottom_right = {1.0f, 0.97f}, .bottom_left = {0.0f, 1.0f}},
        std::nullopt, 0, 0, &scratch);
    check_parity(*pin,
        CornerPinParams{.top_left = {0.0f, 0.0f}, .top_right = {1.0f, 0.0f},
                        .bottom_right = {1.0f, 1.0f}, .bottom_left = {0.0f, 1.0f},
                        .edge_mode = sampling::EdgeMode::Clamp},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        CornerPinParams{.top_left = {5.0f, 0.0f}, .top_right = {1.0f, 0.0f},
                        .bottom_right = {1.0f, 1.0f}, .bottom_left = {0.0f, 1.0f}},
        std::nullopt).ok);

    auto lens = renderer::create_lens_distortion_effect_processor();
    check_parity(*lens,
        LensDistortionParams{.curvature = 0.15f, .center = {0.5f, 0.5f}, .scale = 1.0f},
        std::nullopt, 0, 0, &scratch);
    check_parity(*lens,
        LensDistortionParams{.curvature = -0.5f, .center = {0.4f, 0.6f}, .scale = 1.2f},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        LensDistortionParams{.curvature = 2.0f, .center = {0.5f, 0.5f}, .scale = 1.0f},
        std::nullopt).ok);

    auto fold = renderer::create_fold_warp_effect_processor();
    check_parity(*fold,
        FoldWarpParams{.axis = FoldAxis::Vertical, .position = 0.5f, .amount = 0.25f,
                       .radius = 0.2f, .falloff = 1.0f, .perspective = 0.05f},
        std::nullopt, 0, 0, &scratch);
    check_parity(*fold,
        FoldWarpParams{.axis = FoldAxis::Horizontal, .position = 0.3f, .amount = -0.4f,
                       .radius = 0.5f, .falloff = 2.0f, .perspective = 0.1f},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        FoldWarpParams{.axis = FoldAxis::Vertical, .position = 0.5f, .amount = 0.25f,
                       .radius = 0.001f, .falloff = 1.0f, .perspective = 0.05f},
        std::nullopt).ok);

    auto mesh = renderer::create_mesh_warp_effect_processor();
    check_parity(*mesh,
        MeshWarpParams{.cols = 2, .rows = 2, .smoothness = 0.5f,
                       .offsets = {{0.05f, 0.0f}, {-0.05f, 0.02f},
                                   {0.0f, -0.04f}, {0.03f, 0.03f}}},
        std::nullopt, 0, 0, &scratch);
    check_parity(*mesh,
        MeshWarpParams{.cols = 3, .rows = 3, .smoothness = 1.0f,
                       .offsets = {{0.0f, 0.0f}, {0.02f, 0.0f}, {0.0f, 0.0f},
                                   {0.0f, 0.02f}, {-0.03f, -0.02f}, {0.0f, -0.02f},
                                   {0.0f, 0.0f}, {-0.02f, 0.0f}, {0.0f, 0.0f}}},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        MeshWarpParams{.cols = 4, .rows = 4, .smoothness = 0.0f,
                       .offsets = std::vector<Vec2>(16)},
        std::nullopt).ok);
}

TEST_CASE("Strict Vulkan animated warps match software at a fixed frame") {
    EffectScratchResources scratch;
    auto turb = renderer::create_turbulent_displace_effect_processor();
    check_parity(*turb,
        TurbulentDisplaceParams{.amount = 0.3f, .size = 0.1f, .evolution = 0.5f,
                                .evolution_speed = 0.2f, .complexity = 2},
        std::nullopt, 0, 0, &scratch);
    check_parity(*turb,
        TurbulentDisplaceParams{.amount = 1.0f, .size = 0.05f, .evolution = 0.0f,
                                .evolution_speed = 0.0f, .complexity = 1},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        TurbulentDisplaceParams{.amount = 0.3f, .size = 0.1f, .evolution = 0.0f,
                                .evolution_speed = 0.0f, .complexity = 17},
        std::nullopt).ok);

    auto wave = renderer::create_wave_warp_effect_processor();
    check_parity(*wave,
        WaveWarpParams{.type = WaveWarpParams::WaveType::Sine,
                       .direction = {1.0f, 0.0f}, .amplitude = 0.2f,
                       .wavelength = 0.2f, .speed = 1.0f, .phase = 0.0f},
        std::nullopt, 0, 0, &scratch);
    check_parity(*wave,
        WaveWarpParams{.type = WaveWarpParams::WaveType::Triangle,
                       .direction = {0.6f, 0.8f}, .amplitude = 0.1f,
                       .wavelength = 0.5f, .speed = 0.5f, .phase = 1.0f},
        std::nullopt, 0, 0, &scratch);
    check_parity(*wave,
        WaveWarpParams{.type = WaveWarpParams::WaveType::Square,
                       .direction = {0.0f, 1.0f}, .amplitude = 0.15f,
                       .wavelength = 0.3f, .speed = 0.0f, .phase = 0.5f},
        std::nullopt, 0, 0, &scratch);
    check_parity(*wave,
        WaveWarpParams{.type = WaveWarpParams::WaveType::Saw,
                       .direction = {1.0f, 1.0f}, .amplitude = 0.1f,
                       .wavelength = 0.4f, .speed = 2.0f, .phase = 0.0f},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        WaveWarpParams{.direction = {1.0f, 0.0f}, .amplitude = 0.1f,
                       .wavelength = 0.0f, .speed = 0.0f, .phase = 0.0f},
        std::nullopt).ok);

    auto ripple = renderer::create_ripple_effect_processor();
    check_parity(*ripple,
        RippleParams{.center = {0.5f, 0.5f}, .radius = 1.0f, .wave_speed = 1.5f,
                     .width = 0.1f, .wave_height = 0.05f, .damping = 0.5f},
        std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        RippleParams{.center = {0.5f, 0.5f}, .radius = 0.0f, .wave_speed = 1.5f,
                     .width = 0.1f, .wave_height = 0.05f, .damping = 0.5f},
        std::nullopt).ok);

    auto roughen = renderer::create_roughen_edges_effect_processor();
    check_parity(*roughen,
        RoughenEdgesParams{.amount = 2.0f, .scale = 4.0f, .complexity = 2,
                            .evolution = 0.25f, .evolution_speed = 0.0f, .seed = 7},
        std::nullopt, 0, 0, &scratch);
    // amount == 0 is the CPU no-op: strict native forwards input.
    check_parity(*roughen, RoughenEdgesParams{}, std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        RoughenEdgesParams{.amount = 100.0f, .scale = 4.0f, .complexity = 1},
        std::nullopt).ok);

    auto fake3d = renderer::create_fake_3d_wave_effect_processor();
    const Fake3DWaveParams fake_h{.amplitude_px = 2.0f, .frequency = 2.2f,
                                  .speed = 3.5f, .depth_px = 5.0f, .phase = 0.3f,
                                  .slices = 3, .axis = WaveAxis::Horizontal,
                                  .perspective = 0.06f, .highlight = 0.2f,
                                  .side_darkening = 0.18f, .shadow_enabled = false};
    check_parity(*fake3d, fake_h, std::nullopt, 0, 0, &scratch);
    Fake3DWaveParams fake_v = fake_h;
    fake_v.axis = WaveAxis::Vertical;
    check_parity(*fake3d, fake_v, std::nullopt, 0, 0, &scratch);
    // Shadow composites fail closed: only the body pass is certified.
    Fake3DWaveParams fake_shadow = fake_h;
    fake_shadow.shadow_enabled = true;
    CHECK_FALSE(vulkan_strict(fake_shadow, std::nullopt).ok);

    auto stroke = renderer::create_stroke_effect_processor();
    check_parity(*stroke,
        StrokeParams{.color = {1.0f, 0.2f, 0.1f, 1.0f}, .width = 2.0f,
                     .softness = 1.0f, .mode = StrokeMode::Outside},
        std::nullopt, 0, 0, &scratch);
    check_parity(*stroke,
        StrokeParams{.color = {0.2f, 0.8f, 0.3f, 1.0f}, .width = 1.5f,
                     .softness = 0.5f, .mode = StrokeMode::Inside},
        std::nullopt, 0, 0, &scratch);
    check_parity(*stroke,
        StrokeParams{.color = {0.1f, 0.2f, 1.0f, 1.0f}, .width = 3.0f,
                     .softness = 0.0f, .mode = StrokeMode::Center},
        std::nullopt, 0, 0, &scratch);
    // width <= 0 forwards input; morphology above radius 16 fails closed.
    check_parity(*stroke, StrokeParams{}, std::nullopt, 0, 0, &scratch);
    CHECK_FALSE(vulkan_strict(
        StrokeParams{.color = {1.0f, 1.0f, 1.0f, 1.0f}, .width = 40.0f,
                     .softness = 0.0f, .mode = StrokeMode::Outside},
        std::nullopt).ok);

    // Temporal effects are deterministic for the same inputs and frame.
    const TurbulentDisplaceParams det{.amount = 0.4f, .size = 0.2f, .evolution = 1.0f,
                                      .evolution_speed = 0.5f, .complexity = 3};
    const auto first = vulkan_strict(det, std::nullopt);
    const auto second = vulkan_strict(det, std::nullopt);
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    REQUIRE(first.pixels == second.pixels);
}

TEST_CASE("Strict Vulkan DisplacementMap matches software with a map input") {
    auto displace = renderer::create_displacement_map_effect_processor();
    EffectScratchResources scratch;
    auto map = std::make_shared<Framebuffer>(kW, kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const float v = static_cast<float>(x + y * kW) /
                            static_cast<float>(kW * kH - 1);
            map->set_pixel(x, y, Color{v, v, v, 1.0f});
        }
    const DisplacementMapParams params{.map_layer = "map",
                                        .intensity_x = 0.5f, .intensity_y = 0.25f,
                                        .channel = DisplacementMapParams::Channel::Luma};
    auto source = make_source();
    {
        effects::EffectExecutionContext ctx{};
        ctx.effect_scratch = &scratch;
        ctx.second_input = map.get();
        auto fb = std::make_shared<Framebuffer>(*source);
        displace->apply(*fb, params, ctx);
        std::vector<float> expected;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const Color c = fb->get_pixel(x, y);
                expected.insert(expected.end(), {c.r, c.g, c.b, c.a});
            }
        check_parity_pixels(expected, vulkan_strict_two_inputs(params, *source, *map));
    }
    const DisplacementMapParams red{.map_layer = "map",
                                    .intensity_x = -0.75f, .intensity_y = 0.5f,
                                    .channel = DisplacementMapParams::Channel::Red};
    {
        effects::EffectExecutionContext ctx{};
        ctx.effect_scratch = &scratch;
        ctx.second_input = map.get();
        auto fb = std::make_shared<Framebuffer>(*source);
        displace->apply(*fb, red, ctx);
        std::vector<float> expected;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const Color c = fb->get_pixel(x, y);
                expected.insert(expected.end(), {c.r, c.g, c.b, c.a});
            }
        check_parity_pixels(expected, vulkan_strict_two_inputs(red, *source, *map));
    }
    // Procedural fields, empty map layers and oversized intensities fail closed.
    DisplacementMapParams field = params;
    field.field = graphics::Field2D{};
    CHECK_FALSE(vulkan_strict_two_inputs(field, *source, *map).ok);
    DisplacementMapParams no_layer = params;
    no_layer.map_layer.clear();
    CHECK_FALSE(vulkan_strict_two_inputs(no_layer, *source, *map).ok);
    DisplacementMapParams hot = params;
    hot.intensity_x = 8.0f;
    CHECK_FALSE(vulkan_strict_two_inputs(hot, *source, *map).ok);
}

TEST_CASE("Strict Vulkan generators match software") {
    auto fractal = renderer::create_fractal_noise_effect_processor();
    check_parity(*fractal,
        FractalNoiseParams{.seed = 3, .octaves = 3, .frequency = 0.5f,
                           .amplitude = 1.0f, .evolution = 2.0f,
                           .contrast = 1.2f, .brightness = 0.1f,
                           .color_mode = NoiseColorMode::Monochrome},
        std::nullopt);
    check_parity(*fractal,
        FractalNoiseParams{.seed = 11, .octaves = 2, .frequency = 0.25f,
                           .amplitude = 0.8f, .evolution = 0.0f,
                           .contrast = 1.0f, .brightness = 0.0f,
                           .color_mode = NoiseColorMode::RGB},
        std::nullopt);
    FractalNoiseParams with_field{.seed = 1};
    with_field.field = graphics::Field2D{};
    CHECK_FALSE(vulkan_strict(with_field, std::nullopt).ok);

    auto gradient = renderer::create_four_color_gradient_effect_processor();
    check_parity(*gradient, FourColorGradientParams{}, std::nullopt);
    check_parity(*gradient,
        FourColorGradientParams{
            .colors = {Color{0.1f, 0.2f, 0.3f, 0.5f}, Color{0.9f, 0.1f, 0.1f, 1.0f},
                       Color{0.2f, 0.7f, 0.2f, 0.8f}, Color{0.0f, 0.0f, 0.0f, 0.0f}},
            .points = {Vec2{0.1f, 0.2f}, Vec2{0.9f, 0.1f},
                       Vec2{0.2f, 0.8f}, Vec2{0.8f, 0.9f}},
            .blend_smoothness = 0.3f},
        std::nullopt);

    auto rays = renderer::create_light_rays_effect_processor();
    EffectScratchResources rays_scratch;
    check_parity(*rays,
        LightRaysParams{.origin = {0.5f, 0.5f}, .length = 6.0f, .density = 0.75f,
                        .color = {1.0f, 0.86f, 0.55f, 1.0f}, .decay = 0.92f},
        std::nullopt, 0, 0, &rays_scratch);
    // Zero length is the CPU no-op: strict native forwards input.
    check_parity(*rays,
        LightRaysParams{.origin = {0.2f, 0.8f}, .length = 0.0f, .density = 0.75f,
                        .color = {1.0f, 1.0f, 1.0f, 1.0f}, .decay = 0.5f},
        std::nullopt, 0, 0, &rays_scratch);

    auto halftone = renderer::create_halftone_effect_processor();
    check_parity(*halftone,
        HalftoneParams{.size = 3, .angle_degrees = 15.0f, .amount = 1.0f},
        std::nullopt);
    check_parity(*halftone,
        HalftoneParams{.size = 4, .angle_degrees = 0.0f, .amount = 0.5f},
        std::nullopt);
    CHECK_FALSE(vulkan_strict(HalftoneParams{.size = 0}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(HalftoneParams{.size = 129}, std::nullopt).ok);

    auto luma = renderer::create_luminance_response_effect_processor();
    check_parity(*luma,
        LuminanceResponseParams{.threshold = 0.4f, .softness = 0.2f, .radius = 0.0f,
                                .tint = {1.0f, 0.35f, 0.15f, 1.0f},
                                .intensity = 1.0f,
                                .blend = LuminanceBlendMode::Add},
        std::nullopt, 0, 0, &rays_scratch);
    check_parity(*luma,
        LuminanceResponseParams{.threshold = 0.3f, .softness = 0.3f, .radius = 0.5f,
                                .tint = {0.18f, 0.65f, 1.0f, 1.0f},
                                .intensity = 1.5f,
                                .blend = LuminanceBlendMode::Screen},
        std::nullopt, 0, 0, &rays_scratch);
    check_parity(*luma,
        LuminanceResponseParams{.threshold = 0.5f, .softness = 0.1f, .radius = 0.2f,
                                .tint = {1.0f, 1.0f, 1.0f, 1.0f},
                                .intensity = 0.8f,
                                .blend = LuminanceBlendMode::Over},
        std::nullopt, 0, 0, &rays_scratch);
    CHECK_FALSE(vulkan_strict(
        LuminanceResponseParams{.threshold = 0.5f, .softness = 0.2f, .radius = 1.0f},
        std::nullopt).ok);

    auto crease = renderer::create_crease_shading_effect_processor();
    check_parity(*crease,
        CreaseShadingParams{.axis = FoldAxis::Vertical, .position = 0.5f,
                            .width = 6.0f, .dark_side = 0.35f,
                            .light_side = 0.2f, .softness = 0.5f},
        std::nullopt);
    check_parity(*crease,
        CreaseShadingParams{.axis = FoldAxis::Horizontal, .position = 0.25f,
                            .width = 3.0f, .dark_side = 0.6f,
                            .light_side = 0.4f, .softness = 0.2f},
        std::nullopt);
}

TEST_CASE("Strict Vulkan Curves and Lut3D match software") {
    auto curves = renderer::create_curves_effect_processor();
    CurveCache cache;
    const CurvesParams film{.master = {{0.0f, 0.0f}, {0.25f, 0.3f}, {0.75f, 0.7f}, {1.0f, 1.0f}},
                            .red = {{0.0f, 0.0f}, {1.0f, 0.9f}}};
    {
        const auto expected = software_oracle(
            *curves, film, std::nullopt, 0, 0, nullptr, Frame{}, nullptr, &cache);
        check_parity_pixels(expected, vulkan_strict(film, std::nullopt));
    }
    const CurvesParams lift{.master = {{0.0f, 0.1f}, {1.0f, 0.95f}},
                            .green = {{0.0f, 0.0f}, {0.5f, 0.6f}, {1.0f, 1.0f}},
                            .blue = {{0.0f, 0.05f}, {1.0f, 1.0f}}};
    {
        const auto expected = software_oracle(
            *curves, lift, std::nullopt, 0, 0, nullptr, Frame{}, nullptr, &cache);
        check_parity_pixels(expected, vulkan_strict(lift, std::nullopt));
    }
    // Empty curves forward the input untouched on both sides.
    {
        const auto expected = software_oracle(
            *curves, CurvesParams{}, std::nullopt, 0, 0, nullptr, Frame{},
            nullptr, &cache);
        check_parity_pixels(expected, vulkan_strict(CurvesParams{}, std::nullopt));
    }

    auto lut3d = renderer::create_lut_3d_effect_processor();
    auto table = std::make_shared<const std::vector<std::array<float, 3>>>();
    {
        auto mutable_table = std::make_shared<std::vector<std::array<float, 3>>>();
        mutable_table->reserve(8);
        for (int iz = 0; iz < 2; ++iz)
            for (int iy = 0; iy < 2; ++iy)
                for (int ix = 0; ix < 2; ++ix)
                    mutable_table->push_back(
                        {ix == 0 ? 0.05f : 0.95f, iy == 0 ? 0.1f : 0.9f,
                         iz == 0 ? 0.0f : 1.0f});
        table = mutable_table;
    }
    const Lut3DParams dim2{.dimension = 2, .values = table, .amount = 1.0f};
    check_parity(*lut3d, dim2, std::nullopt);
    const Lut3DParams half{.dimension = 2, .values = table, .amount = 0.5f};
    check_parity(*lut3d, half, std::nullopt);
    // amount <= 0 forwards input; dims outside [2, 16] fail closed.
    const Lut3DParams bypass{.dimension = 2, .values = table, .amount = 0.0f};
    check_parity(*lut3d, bypass, std::nullopt);
    CHECK_FALSE(vulkan_strict(
        Lut3DParams{.dimension = 1, .values = table}, std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(
        Lut3DParams{.dimension = 17, .values = table}, std::nullopt).ok);
    // Negative/NaN amounts throw on the CPU oracle, so they fail closed.
    CHECK_FALSE(vulkan_strict(
        Lut3DParams{.dimension = 2, .values = table, .amount = -0.5f},
        std::nullopt).ok);
    CHECK_FALSE(vulkan_strict(
        Lut3DParams{.dimension = 2, .values = table, .amount = std::nanf("")},
        std::nullopt).ok);
}

TEST_CASE("Strict Vulkan preserves authored order for wave-2 stacks") {
    auto twirl = renderer::create_twirl_effect_processor();
    auto curves = renderer::create_curves_effect_processor();
    EffectScratchResources scratch;
    CurveCache cache;
    auto symmetric = make_symmetric_source();
    const TwirlParams twirl_params{.center = {0.4375f, 0.375f},
                                   .angle_degrees = 45.0f, .radius = 0.75f,
                                   .falloff = 1.0f};
    const CurvesParams curves_params{
        .master = {{0.0f, 0.0f}, {0.5f, 0.65f}, {1.0f, 1.0f}}};

    // CPU oracle: twirl, then curves, in authored order.
    auto cpu_fb = std::make_shared<Framebuffer>(*symmetric);
    {
        effects::EffectExecutionContext ctx{};
        ctx.effect_scratch = &scratch;
        twirl->apply(*cpu_fb, twirl_params, ctx);
    }
    {
        effects::EffectExecutionContext ctx{};
        ctx.curve_cache = &cache;
        curves->apply(*cpu_fb, curves_params, ctx);
    }
    std::vector<float> expected;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const Color c = cpu_fb->get_pixel(x, y);
            expected.insert(expected.end(), {c.r, c.g, c.b, c.a});
        }

    EffectStack stack;
    {
        effects::EffectInstance twirl_inst;
        twirl_inst.params = twirl_params;
        twirl_inst.effect_type = effects::EffectType::Twirl;
        stack.push_back(std::move(twirl_inst));
        effects::EffectInstance curves_inst;
        curves_inst.params = curves_params;
        curves_inst.effect_type = effects::EffectType::Curves;
        stack.push_back(std::move(curves_inst));
    }
    check_parity_pixels(expected, vulkan_strict_stack(stack, symmetric.get()));

    // Reversing the authored order must change the result.
    EffectStack reversed;
    {
        effects::EffectInstance curves_inst;
        curves_inst.params = curves_params;
        curves_inst.effect_type = effects::EffectType::Curves;
        reversed.push_back(std::move(curves_inst));
        effects::EffectInstance twirl_inst;
        twirl_inst.params = twirl_params;
        twirl_inst.effect_type = effects::EffectType::Twirl;
        reversed.push_back(std::move(twirl_inst));
    }
    const auto reversed_run = vulkan_strict_stack(reversed, symmetric.get());
    REQUIRE(reversed_run.ok);
    CHECK(reversed_run.pixels != vulkan_strict_stack(stack, symmetric.get()).pixels);
}

#endif // CHRONON3D_ENABLE_VULKAN
