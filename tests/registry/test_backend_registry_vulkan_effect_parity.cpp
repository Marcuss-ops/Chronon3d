// test_backend_registry_vulkan_effect_parity.cpp — software vs strict-Vulkan
// pixel parity for the native color-matrix effect routes
// (TICKET-VULKAN-EFFECT-PARITY wave 1).  The oracle is the real registered
// software EffectProcessor applied to the same framebuffer; the Vulkan side
// runs EffectStackNode with require_native_gpu so a missing native route
// fails instead of silently falling back to software.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

namespace chronon3d::renderer {
std::unique_ptr<EffectProcessor> create_brightness_effect_processor();
std::unique_ptr<EffectProcessor> create_bloom_effect_processor();
std::unique_ptr<EffectProcessor> create_glow_effect_processor();
std::unique_ptr<EffectProcessor> create_blur_effect_processor();
std::unique_ptr<EffectProcessor> create_contrast_effect_processor();
std::unique_ptr<EffectProcessor> create_saturation_effect_processor();
std::unique_ptr<EffectProcessor> create_hue_rotate_effect_processor();
std::unique_ptr<EffectProcessor> create_invert_effect_processor();
std::unique_ptr<EffectProcessor> create_exposure_effect_processor();
std::unique_ptr<EffectProcessor> create_temperature_tint_effect_processor();
std::unique_ptr<EffectProcessor> create_lift_gamma_gain_effect_processor();
std::unique_ptr<EffectProcessor> create_offset_effect_processor();
std::unique_ptr<EffectProcessor> create_noise_effect_processor();
std::unique_ptr<EffectProcessor> create_checkerboard_effect_processor();
std::unique_ptr<EffectProcessor> create_fill_effect_processor();
std::unique_ptr<EffectProcessor> create_levels_effect_processor();
std::unique_ptr<EffectProcessor> create_threshold_effect_processor();
std::unique_ptr<EffectProcessor> create_vignette_effect_processor();
std::unique_ptr<EffectProcessor> create_mosaic_effect_processor();
std::unique_ptr<EffectProcessor> create_keying_effect_processor();
std::unique_ptr<EffectProcessor> create_drop_shadow_effect_processor();
std::unique_ptr<EffectProcessor> create_directional_blur_effect_processor();
std::unique_ptr<EffectProcessor> create_radial_blur_effect_processor();
std::unique_ptr<EffectProcessor> create_fake_3d_wave_effect_processor();
std::unique_ptr<EffectProcessor> create_corner_pin_effect_processor();
std::unique_ptr<EffectProcessor> create_twirl_effect_processor();
std::unique_ptr<EffectProcessor> create_bulge_effect_processor();
std::unique_ptr<EffectProcessor> create_mesh_warp_effect_processor();
std::unique_ptr<EffectProcessor> create_roughen_edges_effect_processor();
std::unique_ptr<EffectProcessor> create_turbulent_displace_effect_processor();
std::unique_ptr<EffectProcessor> create_wave_warp_effect_processor();
std::unique_ptr<EffectProcessor> create_ripple_effect_processor();
std::unique_ptr<EffectProcessor> create_displacement_map_effect_processor();
std::unique_ptr<EffectProcessor> create_lens_distortion_effect_processor();
std::unique_ptr<EffectProcessor> create_fold_warp_effect_processor();
std::unique_ptr<EffectProcessor> create_crease_shading_effect_processor();
std::unique_ptr<EffectProcessor> create_fractal_noise_effect_processor();
std::unique_ptr<EffectProcessor> create_four_color_gradient_effect_processor();
std::unique_ptr<EffectProcessor> create_light_rays_effect_processor();
std::unique_ptr<EffectProcessor> create_halftone_effect_processor();
std::unique_ptr<EffectProcessor> create_luminance_response_effect_processor();
std::unique_ptr<EffectProcessor> create_curves_effect_processor();
std::unique_ptr<EffectProcessor> create_lut_3d_effect_processor();
std::unique_ptr<EffectProcessor> create_stroke_effect_processor();
std::unique_ptr<EffectProcessor> create_noise_sampler_effect_processor();
std::unique_ptr<EffectProcessor> create_banded_grain_effect_processor();
} // namespace chronon3d::renderer

namespace {

using namespace chronon3d;
using namespace chronon3d::graph;

constexpr int kW = 8;
constexpr int kH = 4;
// Approximate tolerance class: GPU transcendental functions (pow/exp2/cos)
// and FMA contraction may differ from the scalar CPU path by a few ULP.
constexpr float kTolerance = 2.0e-4f;

std::shared_ptr<Framebuffer> make_source(int origin_x = 0, int origin_y = 0) {
    auto fb = std::make_shared<Framebuffer>(kW, kH);
    fb->set_origin(origin_x, origin_y);
    fb->clear(Color::transparent());
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const float a = (x == 0) ? 0.0f : (y == 1 ? 0.5f : (y == 3 ? 0.25f : 1.0f));
            const float r = std::fmod(0.13f * static_cast<float>(x + 1) + 0.07f * y, 1.0f);
            const float g = std::fmod(0.29f * static_cast<float>(y + 1) + 0.05f * x, 1.0f);
            const float b = (x == 7 && y == 0) ? 1.4f : 0.11f * static_cast<float>(x);  // one HDR texel
            // Framebuffer storage is premultiplied; x == 0 keeps junk RGB at a == 0.
            fb->set_pixel(x, y, x == 0 ? Color{0.3f, 0.6f, 0.9f, 0.0f}
                                       : Color{r * a, g * a, b * a, a});
        }
    }
    return fb;
}

std::vector<float> software_oracle(renderer::EffectProcessor& processor,
                                   const EffectParams& params,
                                   const std::optional<raster::BBox>& clip,
                                   int origin_x = 0, int origin_y = 0,
                                   EffectScratchResources* scratch = nullptr,
                                   Frame frame = Frame{},
                                   const Framebuffer* source_override = nullptr,
                                   CurveCache* curve_cache = nullptr,
                                   const Framebuffer* second_input = nullptr) {
    auto fb = source_override
        ? std::make_shared<Framebuffer>(*source_override)
        : make_source(origin_x, origin_y);
    effects::EffectExecutionContext ctx{};
    ctx.clip = clip;
    ctx.effect_scratch = scratch;
    ctx.frame = frame;
    ctx.curve_cache = curve_cache;
    ctx.second_input = second_input;
    processor.apply(*fb, params, ctx);
    std::vector<float> out;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const Color c = fb->get_pixel(x, y);
            out.insert(out.end(), {c.r, c.g, c.b, c.a});
        }
    return out;
}

struct VulkanRun {
    bool ok{false};
    std::string error;
    std::vector<float> pixels;
};

VulkanRun vulkan_strict(const EffectParams& params,
                        const std::optional<raster::BBox>& clip,
                        int origin_x = 0, int origin_y = 0,
                        Frame frame = Frame{},
                        const Framebuffer* source_override = nullptr) {
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = kW;
    ctx.frame_input.height = kH;
    ctx.frame_input.frame = frame;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;
    ctx.node_exec.clip_rect = clip;

    auto source = source_override
        ? std::make_shared<Framebuffer>(*source_override)
        : make_source(origin_x, origin_y);
    EffectStack effects;
    effects::EffectInstance inst;
    inst.params = params;
    inst.effect_type = effects::detect_effect_type(params);
    effects.push_back(std::move(inst));
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{origin_x, origin_y, origin_x + kW, origin_y + kH}};
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

VulkanRun vulkan_strict_stack(const EffectStack& stack,
                               const Framebuffer* source_override = nullptr) {
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = kW;
    ctx.frame_input.height = kH;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;

    auto source = source_override
        ? std::make_shared<Framebuffer>(*source_override)
        : make_source();
    EffectStack effects = stack;
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{raster::BBox{0, 0, kW, kH}};
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

VulkanRun vulkan_strict_adjustment_stack(
    const EffectStack& stack,
    std::optional<raster::BBox> input_bbox = std::nullopt,
    std::optional<raster::BBox> clip = std::nullopt) {
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = kW;
    ctx.frame_input.height = kH;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;
    ctx.node_exec.clip_rect = clip;

    auto source = make_source();
    AdjustmentNode node(stack);
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{input_bbox};
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

void check_parity(renderer::EffectProcessor& processor, const EffectParams& params,
                  const std::optional<raster::BBox>& clip = std::nullopt,
                  int origin_x = 0, int origin_y = 0,
                  EffectScratchResources* scratch = nullptr,
                  Frame frame = Frame{}) {
    const auto expected = software_oracle(processor, params, clip, origin_x, origin_y,
                                          scratch, frame);
    const auto actual = vulkan_strict(params, clip, origin_x, origin_y, frame);
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

std::uint64_t display_framehash(const std::vector<float>& pixels) {
    REQUIRE(pixels.size() % 4 == 0);
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const std::uint8_t rgba[] = {
            Color::linear_to_srgb8(pixels[i]),
            Color::linear_to_srgb8(pixels[i + 1]),
            Color::linear_to_srgb8(pixels[i + 2]),
            static_cast<std::uint8_t>(std::clamp(pixels[i + 3], 0.0f, 1.0f) * 255.0f + 0.5f)};
        for (const auto byte : rgba) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

} // namespace

TEST_CASE("Vulkan native color effects match the software processors") {
    SUBCASE("Brightness") {
        auto p = renderer::create_brightness_effect_processor();
        check_parity(*p, BrightnessParams{.value = 0.15f});
        check_parity(*p, BrightnessParams{.value = -0.3f});
        check_parity(*p, BrightnessParams{.value = 0.0f});
    }
    SUBCASE("Contrast") {
        auto p = renderer::create_contrast_effect_processor();
        check_parity(*p, ContrastParams{.value = 1.6f});
        check_parity(*p, ContrastParams{.value = 0.4f});
        check_parity(*p, ContrastParams{.value = 1.0f});
    }
    SUBCASE("Saturation") {
        auto p = renderer::create_saturation_effect_processor();
        check_parity(*p, SaturationParams{.value = 0.0f});
        check_parity(*p, SaturationParams{.value = 1.8f});
        check_parity(*p, SaturationParams{.value = 1.0f});
    }
    SUBCASE("HueRotate") {
        auto p = renderer::create_hue_rotate_effect_processor();
        check_parity(*p, HueRotateParams{.degrees = 75.0f});
        check_parity(*p, HueRotateParams{.degrees = -200.0f});
    }
    SUBCASE("Invert") {
        auto p = renderer::create_invert_effect_processor();
        check_parity(*p, InvertParams{.amount = 1.0f});
        check_parity(*p, InvertParams{.amount = 0.35f});
        check_parity(*p, InvertParams{.amount = -0.5f});
    }
    SUBCASE("Exposure") {
        auto p = renderer::create_exposure_effect_processor();
        check_parity(*p, ExposureParams{.stops = 1.5f});
        check_parity(*p, ExposureParams{.stops = -2.0f});
    }
    SUBCASE("TemperatureTint") {
        auto p = renderer::create_temperature_tint_effect_processor();
        check_parity(*p, TemperatureTintParams{.temperature = 0.7f, .tint = -0.6f});
        check_parity(*p, TemperatureTintParams{.temperature = -0.4f, .tint = 0.8f});
        check_parity(*p, TemperatureTintParams{.temperature = 2.0f, .tint = -2.0f});
        check_parity(*p, TemperatureTintParams{});
    }
    SUBCASE("LiftGammaGain") {
        auto p = renderer::create_lift_gamma_gain_effect_processor();
        check_parity(*p, LiftGammaGainParams{.lift = 0.08f, .gamma = 1.7f, .gain = 1.3f});
        check_parity(*p, LiftGammaGainParams{.lift = -0.2f, .gamma = 0.6f, .gain = 0.8f});
        check_parity(*p, LiftGammaGainParams{.lift = 3.0f, .gamma = 0.01f, .gain = 8.0f});
        check_parity(*p, LiftGammaGainParams{});
    }
    SUBCASE("Offset") {
        auto p = renderer::create_offset_effect_processor();
        EffectScratchResources scratch;
        for (const auto edge : {sampling::EdgeMode::Transparent, sampling::EdgeMode::Clamp,
                                sampling::EdgeMode::Wrap, sampling::EdgeMode::Mirror}) {
            for (const auto filter : {sampling::SampleFilter::Nearest,
                                      sampling::SampleFilter::Bilinear}) {
                check_parity(*p, OffsetParams{.offset = {1.25f, -0.75f},
                                              .edge_mode = edge, .filter = filter},
                             std::nullopt, 0, 0, &scratch);
            }
        }
        check_parity(*p, OffsetParams{.offset = {-2.0f, 1.0f},
                                      .edge_mode = sampling::EdgeMode::Wrap,
                                      .filter = sampling::SampleFilter::Nearest},
                     std::nullopt, 0, 0, &scratch);
        check_parity(*p, OffsetParams{.offset = {0.0f, 0.0f}},
                     std::nullopt, 0, 0, &scratch);
        check_parity(*p, OffsetParams{.offset = {-0.4f, 1.1f},
                                      .edge_mode = sampling::EdgeMode::Clamp,
                                      .filter = sampling::SampleFilter::Bilinear},
                     raster::BBox{2, 1, 6, 3}, 0, 0, &scratch);
    }
    SUBCASE("Noise") {
        auto p = renderer::create_noise_effect_processor();
        const NoiseParams monochrome{.amount = 0.18f, .seed = 0x9123abcdU,
            .animated = false, .color_mode = NoiseColorMode::Monochrome,
            .size = 2.5f, .shadow_response = 0.35f,
            .midtone_response = 1.2f, .highlight_response = 0.7f};
        check_parity(*p, monochrome);
        const NoiseParams animated_rgb{.amount = 0.11f, .seed = 0xfedcba98U,
            .animated = true, .color_mode = NoiseColorMode::RGB,
            .size = 1.0f, .shadow_response = 1.0f,
            .midtone_response = 0.8f, .highlight_response = 1.3f};
        check_parity(*p, animated_rgb, std::nullopt, 0, 0, nullptr, Frame{37});
        check_parity(*p, monochrome, raster::BBox{2, 1, 6, 3});
        check_parity(*p, NoiseParams{.amount = 0.0f, .seed = 123U});
        check_parity(*p, NoiseParams{.amount = 0.2f, .seed = 34U,
                                     .shadow_response = NAN},
                     std::nullopt, 0, 0, nullptr, Frame{12});
    }
    SUBCASE("Checkerboard") {
        auto p = renderer::create_checkerboard_effect_processor();
        check_parity(*p, CheckerboardParams{.size = 2.5f,
            .color_a = Color{0.9f, 0.1f, 0.2f, 1.0f},
            .color_b = Color{0.05f, 0.8f, 0.4f, 1.0f}, .feather = 0.0f});
        check_parity(*p, CheckerboardParams{.size = 3.25f,
            .color_a = Color{0.8f, 0.25f, 0.1f, 0.35f},
            .color_b = Color{0.05f, 0.6f, 0.95f, 0.85f}, .feather = 0.8f});
        check_parity(*p, CheckerboardParams{.size = 2.0f,
            .color_a = Color{1.0f, 0.0f, 0.0f, 1.0f},
            .color_b = Color{0.0f, 0.0f, 1.0f, 1.0f}, .feather = 8.0f},
            raster::BBox{2, 1, 6, 3});
        check_parity(*p, CheckerboardParams{.size = 0.0f});
    }
    SUBCASE("Fill") {
        auto p = renderer::create_fill_effect_processor();
        check_parity(*p, FillParams{.color = Color{0.9f, 0.2f, 0.4f, 1.0f},
                                    .amount = 1.0f, .mode = FillMode::Replace});
        check_parity(*p, FillParams{.color = Color{0.1f, 0.7f, 0.3f, 1.0f},
                                    .amount = 0.4f, .mode = FillMode::Mix});
        check_parity(*p, FillParams{.color = Color{0.1f, 0.7f, 0.3f, 1.0f},
                                    .amount = 0.0f, .mode = FillMode::Mix});
    }
    SUBCASE("Levels") {
        auto p = renderer::create_levels_effect_processor();
        LevelsParams levels;
        levels.master = {0.05f, 0.95f, 1.3f, 0.0f, 1.0f};
        levels.red = {0.1f, 0.9f, 0.8f, 0.05f, 0.95f};
        levels.green = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        levels.blue = {0.2f, 0.7f, 2.2f, 0.1f, 1.0f};
        check_parity(*p, levels);
        check_parity(*p, LevelsParams{});
    }
    SUBCASE("Threshold") {
        auto p = renderer::create_threshold_effect_processor();
        check_parity(*p, ThresholdParams{.threshold = 0.5f, .softness = 0.0f});
        check_parity(*p, ThresholdParams{.threshold = 0.3f, .softness = 0.2f});
        check_parity(*p, ThresholdParams{.threshold = 0.8f, .softness = 0.5f});
        check_parity(*p, ThresholdParams{.threshold = 0.0f, .softness = 0.0f});
        check_parity(*p, ThresholdParams{.threshold = 1.0f, .softness = 1.0f});
    }
    SUBCASE("Vignette") {
        auto p = renderer::create_vignette_effect_processor();
        check_parity(*p, VignetteParams{.radius = 0.27f, .softness = 0.38f,
                                        .amount = 0.82f,
                                        .color = Color{0.12f, 0.04f, 0.31f, 0.2f}});
        check_parity(*p, VignetteParams{.radius = 0.75f, .softness = 0.0f,
                                        .amount = 1.0f,
                                        .color = Color{0.0f, 0.0f, 0.0f, 1.0f}});
        check_parity(*p, VignetteParams{.radius = 0.5f, .softness = 0.5f,
                                        .amount = 0.0f});
    }
    SUBCASE("Mosaic") {
        auto p = renderer::create_mosaic_effect_processor();
        check_parity(*p, MosaicParams{.block_size = 1});
        check_parity(*p, MosaicParams{.block_size = 3});
        check_parity(*p, MosaicParams{.block_size = 512});
    }
}

TEST_CASE("Vulkan native color effects honor the software clip contract") {
    const raster::BBox clip{2, 1, 6, 3};
    auto brightness = renderer::create_brightness_effect_processor();
    check_parity(*brightness, BrightnessParams{.value = 0.2f}, clip);
    auto invert = renderer::create_invert_effect_processor();
    check_parity(*invert, InvertParams{.amount = 0.8f}, clip);
    auto threshold = renderer::create_threshold_effect_processor();
    check_parity(*threshold, ThresholdParams{.threshold = 0.4f, .softness = 0.15f}, clip);
    auto vignette = renderer::create_vignette_effect_processor();
    check_parity(*vignette, VignetteParams{.radius = 0.25f, .softness = 0.45f,
                                           .amount = 0.9f}, clip);
    auto mosaic = renderer::create_mosaic_effect_processor();
    check_parity(*mosaic, MosaicParams{.block_size = 3}, clip);
    // Straight-alpha processors honor the clip too: untouched HDR/half-alpha
    // pixels outside the region must survive the native output copy.
    auto exposure = renderer::create_exposure_effect_processor();
    check_parity(*exposure, ExposureParams{.stops = 1.0f}, clip);
    auto temperature_tint = renderer::create_temperature_tint_effect_processor();
    check_parity(*temperature_tint,
        TemperatureTintParams{.temperature = 0.45f, .tint = -0.3f}, clip);
    auto levels = renderer::create_levels_effect_processor();
    LevelsParams levels_params;
    levels_params.master.gamma = 1.5f;
    check_parity(*levels, levels_params, clip);
    auto fill = renderer::create_fill_effect_processor();
    check_parity(*fill, FillParams{.color = Color{0.9f, 0.2f, 0.4f, 1.0f},
        .amount = 0.4f, .mode = FillMode::Mix}, clip);
}

TEST_CASE("Vulkan luma and chroma key match the software processor") {
    auto processor = renderer::create_keying_effect_processor();
    // Exercise non-opaque premultiplied texels with partial keep alpha and
    // softness. This catches output RGB being multiplied by the updated alpha
    // twice after unpremultiplication.
    KeyingParams semitransparent_soft_key;
    semitransparent_soft_key.mode = KeyingMode::Luma;
    semitransparent_soft_key.threshold = 0.18f;
    semitransparent_soft_key.softness = 0.72f;
    semitransparent_soft_key.spill = 0.0f;
    check_parity(*processor, semitransparent_soft_key);

    KeyingParams chroma;
    chroma.mode = KeyingMode::Chroma;
    chroma.key_color = Color{0.0f, 1.0f, 0.0f, 1.0f};
    chroma.threshold = 0.12f;
    chroma.softness = 0.52f;
    chroma.spill = 0.73f;
    chroma.clip_black = 0.08f;
    chroma.clip_white = 0.91f;
    chroma.choke = 0.17f;
    chroma.garbage_x = 0.12f;
    chroma.garbage_y = 0.08f;
    chroma.garbage_w = 0.76f;
    chroma.garbage_h = 0.82f;
    chroma.garbage_feather = 0.09f;
    check_parity(*processor, chroma);
    check_parity(*processor, chroma, raster::BBox{1, 0, 7, 4});

    KeyingParams luma;
    luma.mode = KeyingMode::Luma;
    luma.threshold = 0.32f;
    luma.softness = 0.28f;
    luma.spill = 0.4f;
    luma.clip_black = 0.14f;
    luma.clip_white = 0.88f;
    luma.choke = 0.05f;
    check_parity(*processor, luma);
}

TEST_CASE("Vulkan luma and chroma key have stable display framehashes") {
    auto processor = renderer::create_keying_effect_processor();
    const KeyingParams chroma{
        .mode = KeyingMode::Chroma,
        .key_color = Color{0.0f, 1.0f, 0.0f, 1.0f},
        .threshold = 0.12f,
        .softness = 0.52f,
        .spill = 0.73f,
        .clip_black = 0.08f,
        .clip_white = 0.91f,
        .choke = 0.17f,
        .garbage_x = 0.12f,
        .garbage_y = 0.08f,
        .garbage_w = 0.76f,
        .garbage_h = 0.82f,
        .garbage_feather = 0.09f};
    const KeyingParams luma{
        .mode = KeyingMode::Luma,
        .threshold = 0.32f,
        .softness = 0.28f,
        .spill = 0.4f,
        .clip_black = 0.14f,
        .clip_white = 0.88f,
        .choke = 0.05f};

    const auto verify = [&](const KeyingParams& params, std::uint64_t expected_hash) {
        const auto software = software_oracle(*processor, params, std::nullopt);
        const auto actual = vulkan_strict(params, std::nullopt);
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        REQUIRE(actual.pixels.size() == software.size());
        for (std::size_t i = 0; i < software.size(); ++i)
            CHECK(actual.pixels[i] == doctest::Approx(software[i]).epsilon(kTolerance));
        const auto gpu_hash = display_framehash(actual.pixels);
        const auto software_hash = display_framehash(software);
        MESSAGE("GPU framehash " << gpu_hash << ", software framehash " << software_hash);
        CHECK(gpu_hash == software_hash);
        CHECK(gpu_hash == expected_hash);
    };

    SUBCASE("chroma with spill, matte refinement and garbage matte") {
        verify(chroma, 0x99f24d5e1a478dd3ULL);
    }
    SUBCASE("luma with matte refinement") {
        verify(luma, 0xbad1bfc197f9213dULL);
    }
}

TEST_CASE("Strict Vulkan applies certified effect stacks in authored order") {
    auto source = make_source();
    auto expected = *source;
    auto brightness = renderer::create_brightness_effect_processor();
    auto invert = renderer::create_invert_effect_processor();
    auto vignette = renderer::create_vignette_effect_processor();
    auto mosaic = renderer::create_mosaic_effect_processor();
    effects::EffectExecutionContext context{};
    brightness->apply(expected, BrightnessParams{.value = 0.12f}, context);
    const VignetteParams vignette_params{.radius = 0.32f, .softness = 0.27f,
                                          .amount = 0.74f,
                                          .color = Color{0.08f, 0.02f, 0.11f, 1.0f}};
    vignette->apply(expected, vignette_params, context);
    const MosaicParams mosaic_params{.block_size = 3};
    mosaic->apply(expected, mosaic_params, context);
    invert->apply(expected, InvertParams{.amount = 0.4f}, context);

    EffectStack stack;
    stack.push_back(effects::EffectInstance{BrightnessParams{.value = 0.12f}});
    effects::EffectInstance disabled{InvertParams{.amount = 0.9f}};
    disabled.enabled = false;
    stack.push_back(std::move(disabled));
    stack.push_back(effects::EffectInstance{vignette_params});
    stack.push_back(effects::EffectInstance{mosaic_params});
    stack.push_back(effects::EffectInstance{InvertParams{.amount = 0.4f}});
    const auto actual = vulkan_strict_stack(stack);
    INFO("strict Vulkan error: " << actual.error);
    REQUIRE(actual.ok);
    REQUIRE(actual.pixels.size() == static_cast<std::size_t>(kW * kH * 4));
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const auto i = static_cast<std::size_t>((y * kW + x) * 4);
            const Color c = expected.get_pixel(x, y);
            for (int channel = 0; channel < 4; ++channel) {
                const float wanted = channel == 0 ? c.r : channel == 1 ? c.g :
                                     channel == 2 ? c.b : c.a;
                CHECK(actual.pixels[i + static_cast<std::size_t>(channel)] ==
                      doctest::Approx(wanted).epsilon(kTolerance));
            }
        }
    }
}

TEST_CASE("Strict Vulkan AdjustmentNode composes ordered native color stages") {
    auto source = make_source();
    auto expected = *source;
    auto checkerboard = renderer::create_checkerboard_effect_processor();
    auto brightness = renderer::create_brightness_effect_processor();
    auto invert = renderer::create_invert_effect_processor();
    auto temperature_tint = renderer::create_temperature_tint_effect_processor();
    auto lift_gamma_gain = renderer::create_lift_gamma_gain_effect_processor();
    auto offset = renderer::create_offset_effect_processor();
    auto noise = renderer::create_noise_effect_processor();
    EffectScratchResources scratch;
    effects::EffectExecutionContext context{};
    context.effect_scratch = &scratch;
    const CheckerboardParams checkerboard_params{.size = 2.75f,
        .color_a = Color{0.8f, 0.15f, 0.35f, 0.9f},
        .color_b = Color{0.1f, 0.7f, 0.45f, 0.65f}, .feather = 0.6f};
    checkerboard->apply(expected, checkerboard_params, context);
    brightness->apply(expected, BrightnessParams{.value = 0.14f}, context);
    invert->apply(expected, InvertParams{.amount = 0.35f}, context);
    const TemperatureTintParams white_balance{.temperature = 0.55f, .tint = -0.4f};
    temperature_tint->apply(expected, white_balance, context);
    const LiftGammaGainParams grade{.lift = 0.06f, .gamma = 1.4f, .gain = 1.15f};
    lift_gamma_gain->apply(expected, grade, context);
    const OffsetParams offset_params{.offset = {0.75f, -0.5f},
                                     .edge_mode = sampling::EdgeMode::Mirror,
                                     .filter = sampling::SampleFilter::Bilinear};
    offset->apply(expected, offset_params, context);
    const NoiseParams noise_params{.amount = 0.09f, .seed = 0xabc12345U,
        .animated = false, .color_mode = NoiseColorMode::RGB, .size = 2.0f};
    noise->apply(expected, noise_params, context);

    EffectStack stack;
    stack.emplace_back(checkerboard_params);
    stack.emplace_back(BrightnessParams{.value = 0.14f});
    stack.emplace_back(InvertParams{.amount = 0.35f});
    stack.emplace_back(white_balance);
    stack.emplace_back(grade);
    stack.emplace_back(offset_params);
    stack.emplace_back(noise_params);
    const auto actual = vulkan_strict_adjustment_stack(stack);
    INFO("strict Vulkan adjustment error: " << actual.error);
    REQUIRE(actual.ok);
    REQUIRE(actual.pixels.size() == static_cast<std::size_t>(kW * kH * 4));
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const Color color = expected.get_pixel(x, y);
            const float channels[]{color.r, color.g, color.b, color.a};
            const auto index = static_cast<std::size_t>((y * kW + x) * 4);
            for (int channel = 0; channel < 4; ++channel)
                CHECK(actual.pixels[index + static_cast<std::size_t>(channel)] ==
                      doctest::Approx(channels[channel]).epsilon(kTolerance));
        }
}

TEST_CASE("Strict Vulkan AdjustmentNode preserves keying order") {
    auto brightness = renderer::create_brightness_effect_processor();
    auto keying = renderer::create_keying_effect_processor();
    const BrightnessParams brightness_params{.value = 0.18f};
    const KeyingParams keying_params{
        .mode = KeyingMode::Chroma,
        .key_color = Color{0.12f, 0.48f, 0.76f, 1.0f},
        .threshold = 0.38f,
        .softness = 0.24f,
        .spill = 0.17f};
    effects::EffectExecutionContext context{};

    auto expected_brightness_key = *make_source();
    brightness->apply(expected_brightness_key, brightness_params, context);
    keying->apply(expected_brightness_key, keying_params, context);
    EffectStack brightness_key_stack;
    brightness_key_stack.emplace_back(brightness_params);
    brightness_key_stack.emplace_back(keying_params);
    const auto brightness_key = vulkan_strict_adjustment_stack(brightness_key_stack);
    INFO("strict Vulkan adjustment error: " << brightness_key.error);
    REQUIRE(brightness_key.ok);

    auto expected_key_brightness = *make_source();
    keying->apply(expected_key_brightness, keying_params, context);
    brightness->apply(expected_key_brightness, brightness_params, context);
    EffectStack key_brightness_stack;
    key_brightness_stack.emplace_back(keying_params);
    key_brightness_stack.emplace_back(brightness_params);
    const auto key_brightness = vulkan_strict_adjustment_stack(key_brightness_stack);
    INFO("strict Vulkan adjustment error: " << key_brightness.error);
    REQUIRE(key_brightness.ok);

    bool order_changes_output = false;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const auto i = static_cast<std::size_t>((y * kW + x) * 4);
            const Color a = expected_brightness_key.get_pixel(x, y);
            const Color b = expected_key_brightness.get_pixel(x, y);
            const float channels_a[]{a.r, a.g, a.b, a.a};
            const float channels_b[]{b.r, b.g, b.b, b.a};
            for (std::size_t channel = 0; channel < 4; ++channel) {
                CHECK(brightness_key.pixels[i + channel] ==
                      doctest::Approx(channels_a[channel]).epsilon(kTolerance));
                CHECK(key_brightness.pixels[i + channel] ==
                      doctest::Approx(channels_b[channel]).epsilon(kTolerance));
                order_changes_output |= std::abs(channels_a[channel] - channels_b[channel]) > 1e-4f;
            }
        }
    CHECK(order_changes_output);
}

TEST_CASE("Strict Vulkan AdjustmentNode preserves extent for ordered blur stacks") {
    auto expected = *make_source();
    auto blur = renderer::create_blur_effect_processor();
    auto invert = renderer::create_invert_effect_processor();
    effects::EffectExecutionContext context{};
    blur->apply(expected, BlurParams{.radius = 1.25f}, context);
    invert->apply(expected, InvertParams{.amount = 0.3f}, context);

    EffectStack stack;
    stack.emplace_back(BlurParams{.radius = 1.25f});
    stack.emplace_back(InvertParams{.amount = 0.3f});
    const auto actual = vulkan_strict_adjustment_stack(
        stack, raster::BBox{0, 0, kW, kH});
    INFO("strict Vulkan adjustment error: " << actual.error);
    REQUIRE(actual.ok);
    REQUIRE(actual.pixels.size() == static_cast<std::size_t>(kW * kH * 4));
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const Color color = expected.get_pixel(x, y);
            const float channels[]{color.r, color.g, color.b, color.a};
            const auto index = static_cast<std::size_t>((y * kW + x) * 4);
            for (int channel = 0; channel < 4; ++channel)
                CHECK(actual.pixels[index + static_cast<std::size_t>(channel)] ==
                      doctest::Approx(channels[channel]).epsilon(kTolerance));
        }
}

TEST_CASE("Strict Vulkan AdjustmentNode preserves clip for ordered blur stacks") {
    auto expected = *make_source();
    auto blur = renderer::create_blur_effect_processor();
    auto invert = renderer::create_invert_effect_processor();
    const raster::BBox clip{2, 1, 6, 3};
    effects::EffectExecutionContext context{};
    context.clip = clip;
    blur->apply(expected, BlurParams{.radius = 1.25f}, context);
    invert->apply(expected, InvertParams{.amount = 0.3f}, context);

    EffectStack stack;
    stack.emplace_back(BlurParams{.radius = 1.25f});
    stack.emplace_back(InvertParams{.amount = 0.3f});
    const auto actual = vulkan_strict_adjustment_stack(
        stack, raster::BBox{0, 0, kW, kH}, clip);
    INFO("strict Vulkan adjustment error: " << actual.error);
    REQUIRE(actual.ok);
    REQUIRE(actual.pixels.size() == static_cast<std::size_t>(kW * kH * 4));
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const Color color = expected.get_pixel(x, y);
            const float channels[]{color.r, color.g, color.b, color.a};
            const auto index = static_cast<std::size_t>((y * kW + x) * 4);
            for (int channel = 0; channel < 4; ++channel) {
                INFO("x=" << x << " y=" << y << " channel=" << channel);
                CHECK(actual.pixels[index + static_cast<std::size_t>(channel)] ==
                      doctest::Approx(channels[channel]).epsilon(kTolerance));
            }
        }
}

TEST_CASE("Strict Vulkan AdjustmentNode composes Glow and Bloom stacks") {
    auto bloom_processor = renderer::create_bloom_effect_processor();
    auto glow_processor = renderer::create_glow_effect_processor();
    auto invert_processor = renderer::create_invert_effect_processor();
    EffectScratchResources glow_scratch;
    for (const float radius : {0.5f, 1.5f, 3.0f, 10.0f}) {
        INFO("glow radius=" << radius);
        auto expected = *make_source();
        const GlowParams params{
            .radius = radius, .intensity = 0.45f,
            .color = Color{0.7f, 0.35f, 0.9f, 0.8f}};
        effects::EffectExecutionContext context{};
        context.effect_scratch = &glow_scratch;
        glow_processor->apply(expected, params, context);
        invert_processor->apply(expected, InvertParams{.amount = 0.3f}, context);

        EffectStack glow_stack;
        glow_stack.emplace_back(params);
        glow_stack.emplace_back(InvertParams{.amount = 0.3f});
        const auto actual = vulkan_strict_adjustment_stack(
            glow_stack, raster::BBox{0, 0, kW, kH});
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        REQUIRE(actual.pixels.size() == static_cast<std::size_t>(kW * kH * 4));
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const Color color = expected.get_pixel(x, y);
                const float channels[]{color.r, color.g, color.b, color.a};
                const auto index = static_cast<std::size_t>((y * kW + x) * 4);
                for (int channel = 0; channel < 4; ++channel)
                    CHECK(actual.pixels[index + static_cast<std::size_t>(channel)] ==
                          doctest::Approx(channels[channel]).epsilon(kTolerance));
            }
    }

    for (const float radius : {0.5f, 1.5f, 3.0f, 10.0f}) {
        INFO("radius=" << radius);
        auto expected = *make_source();
        effects::EffectExecutionContext context{};
        const BloomParams params{
            .threshold = 0.35f, .radius = radius, .intensity = 0.5f};
        bloom_processor->apply(expected, params, context);
        invert_processor->apply(expected, InvertParams{.amount = 0.3f}, context);

        EffectStack bloom_stack;
        bloom_stack.emplace_back(params);
        bloom_stack.emplace_back(InvertParams{.amount = 0.3f});
        const auto actual = vulkan_strict_adjustment_stack(
            bloom_stack, raster::BBox{0, 0, kW, kH});
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        REQUIRE(actual.pixels.size() == static_cast<std::size_t>(kW * kH * 4));
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const Color color = expected.get_pixel(x, y);
                const float channels[]{color.r, color.g, color.b, color.a};
                const auto index = static_cast<std::size_t>((y * kW + x) * 4);
                for (int channel = 0; channel < 4; ++channel)
                    CHECK(actual.pixels[index + static_cast<std::size_t>(channel)] ==
                          doctest::Approx(channels[channel]).epsilon(kTolerance));
            }
    }
}

TEST_CASE("Strict Vulkan EffectStackNode composes Glow and Bloom stacks") {
    auto bloom_processor = renderer::create_bloom_effect_processor();
    auto glow_processor = renderer::create_glow_effect_processor();
    auto invert_processor = renderer::create_invert_effect_processor();
    EffectScratchResources glow_scratch;

    for (const float radius : {0.5f, 1.5f, 3.0f, 10.0f}) {
        INFO("glow radius=" << radius);
        auto expected = *make_source();
        const GlowParams params{
            .radius = radius, .intensity = 0.45f,
            .color = Color{0.7f, 0.35f, 0.9f, 0.8f}};
        effects::EffectExecutionContext context{};
        context.effect_scratch = &glow_scratch;
        glow_processor->apply(expected, params, context);
        invert_processor->apply(expected, InvertParams{.amount = 0.3f}, context);

        EffectStack stack;
        stack.emplace_back(params);
        stack.emplace_back(InvertParams{.amount = 0.3f});
        const auto actual = vulkan_strict_stack(stack);
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const Color color = expected.get_pixel(x, y);
                const float channels[]{color.r, color.g, color.b, color.a};
                const auto index = static_cast<std::size_t>((y * kW + x) * 4);
                for (int channel = 0; channel < 4; ++channel)
                    CHECK(actual.pixels[index + static_cast<std::size_t>(channel)] ==
                          doctest::Approx(channels[channel]).epsilon(kTolerance));
            }
    }

    for (const float radius : {0.5f, 1.5f, 3.0f, 10.0f}) {
        INFO("bloom radius=" << radius);
        auto expected = *make_source();
        const BloomParams params{
            .threshold = 0.35f, .radius = radius, .intensity = 0.5f};
        effects::EffectExecutionContext context{};
        bloom_processor->apply(expected, params, context);
        invert_processor->apply(expected, InvertParams{.amount = 0.3f}, context);

        EffectStack stack;
        stack.emplace_back(params);
        stack.emplace_back(InvertParams{.amount = 0.3f});
        const auto actual = vulkan_strict_stack(stack);
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const Color color = expected.get_pixel(x, y);
                const float channels[]{color.r, color.g, color.b, color.a};
                const auto index = static_cast<std::size_t>((y * kW + x) * 4);
                for (int channel = 0; channel < 4; ++channel)
                    CHECK(actual.pixels[index + static_cast<std::size_t>(channel)] ==
                          doctest::Approx(channels[channel]).epsilon(kTolerance));
            }
    }
}

TEST_CASE("Strict Vulkan Bloom matches software across full-resolution radii") {
    auto bloom = renderer::create_bloom_effect_processor();
    for (const float radius : {0.0f, 0.5f, 1.5f, 3.0f, 10.0f}) {
        INFO("radius=" << radius);
        const BloomParams params{
            .threshold = 0.32f, .radius = radius, .intensity = 0.7f};
        const auto expected = software_oracle(*bloom, params, std::nullopt);
        const auto actual = vulkan_strict(params, std::nullopt);
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        REQUIRE(actual.pixels.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(actual.pixels[i] == doctest::Approx(expected[i]).epsilon(kTolerance));
    }
    const auto downsampled = vulkan_strict(
        BloomParams{.threshold = 0.32f, .radius = 10.1f, .intensity = 0.7f},
        std::nullopt);
    CHECK_FALSE(downsampled.ok);
}

TEST_CASE("Strict Vulkan Glow matches the software alpha-halo oracle") {
    auto glow = renderer::create_glow_effect_processor();
    EffectScratchResources scratch;
    for (const float radius : {0.0f, 0.49f, 0.5f, 1.5f, 3.0f, 10.0f}) {
        INFO("radius=" << radius);
        const GlowParams params{
            .radius = radius, .intensity = 0.45f,
            .color = Color{0.7f, 0.35f, 0.9f, 0.8f}};
        const auto expected = software_oracle(
            *glow, params, std::nullopt, 0, 0, &scratch);
        const auto actual = vulkan_strict(params, std::nullopt);
        INFO("strict Vulkan error: " << actual.error);
        REQUIRE(actual.ok);
        REQUIRE(actual.pixels.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(actual.pixels[i] == doctest::Approx(expected[i]).epsilon(kTolerance));
    }
}

TEST_CASE("Strict Vulkan preserves keying order inside native effect stacks") {
    auto brightness = renderer::create_brightness_effect_processor();
    auto keying = renderer::create_keying_effect_processor();
    const BrightnessParams brightness_params{.value = 0.18f};
    const KeyingParams keying_params{
        .mode = KeyingMode::Luma,
        .threshold = 0.34f,
        .softness = 0.27f,
        .spill = 0.2f};
    effects::EffectExecutionContext context{};

    auto expected_brightness_key = *make_source();
    brightness->apply(expected_brightness_key, brightness_params, context);
    keying->apply(expected_brightness_key, keying_params, context);
    EffectStack brightness_key_stack;
    brightness_key_stack.emplace_back(brightness_params);
    brightness_key_stack.emplace_back(keying_params);
    const auto brightness_key = vulkan_strict_stack(brightness_key_stack);
    INFO("strict Vulkan error: " << brightness_key.error);
    REQUIRE(brightness_key.ok);

    auto expected_key_brightness = *make_source();
    keying->apply(expected_key_brightness, keying_params, context);
    brightness->apply(expected_key_brightness, brightness_params, context);
    EffectStack key_brightness_stack;
    key_brightness_stack.emplace_back(keying_params);
    key_brightness_stack.emplace_back(brightness_params);
    const auto key_brightness = vulkan_strict_stack(key_brightness_stack);
    INFO("strict Vulkan error: " << key_brightness.error);
    REQUIRE(key_brightness.ok);

    bool order_changes_output = false;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const auto i = static_cast<std::size_t>((y * kW + x) * 4);
            const Color wanted_a = expected_brightness_key.get_pixel(x, y);
            const Color wanted_b = expected_key_brightness.get_pixel(x, y);
            const float channels_a[] = {wanted_a.r, wanted_a.g, wanted_a.b, wanted_a.a};
            const float channels_b[] = {wanted_b.r, wanted_b.g, wanted_b.b, wanted_b.a};
            for (std::size_t channel = 0; channel < 4; ++channel) {
                CHECK(brightness_key.pixels[i + channel] ==
                      doctest::Approx(channels_a[channel]).epsilon(kTolerance));
                CHECK(key_brightness.pixels[i + channel] ==
                      doctest::Approx(channels_b[channel]).epsilon(kTolerance));
                order_changes_output |= std::abs(channels_a[channel] - channels_b[channel]) > 1e-4f;
            }
        }
    }
    CHECK(order_changes_output);
}

TEST_CASE("Strict Vulkan composes spatial blur before a color stage") {
    auto expected = *make_source();
    auto blur = renderer::create_blur_effect_processor();
    auto invert = renderer::create_invert_effect_processor();
    effects::EffectExecutionContext context{};
    blur->apply(expected, BlurParams{.radius = 1.25f}, context);
    invert->apply(expected, InvertParams{.amount = 0.3f}, context);

    EffectStack stack;
    stack.push_back(effects::EffectInstance{BlurParams{.radius = 1.25f}});
    stack.push_back(effects::EffectInstance{InvertParams{.amount = 0.3f}});
    const auto actual = vulkan_strict_stack(stack);
    INFO("strict Vulkan error: " << actual.error);
    REQUIRE(actual.ok);
    REQUIRE(actual.pixels.size() == static_cast<std::size_t>(kW * kH * 4));
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const auto i = static_cast<std::size_t>((y * kW + x) * 4);
            const Color c = expected.get_pixel(x, y);
            const float wanted[] = {c.r, c.g, c.b, c.a};
            for (int channel = 0; channel < 4; ++channel) {
                CHECK(actual.pixels[i + static_cast<std::size_t>(channel)] ==
                      doctest::Approx(wanted[channel]).epsilon(kTolerance));
            }
        }
    }
}

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

    auto sampler = renderer::create_noise_sampler_effect_processor();
    check_parity(*sampler,
        NoiseSamplerParams{.seed = 5, .type = NoiseSamplerType::Simplex,
                           .octaves = 3, .frequency = 0.4f,
                           .lacunarity = 2.0f, .gain = 0.5f,
                           .evolution = 0.0f, .contrast = 1.2f,
                           .brightness = 0.1f, .domain_warp = 0.5f,
                           .remap_min = 0.1f, .remap_max = 0.9f,
                           .quantize_steps = 8,
                           .color_mode = NoiseSamplerColorMode::Monochrome},
        std::nullopt);
    check_parity(*sampler,
        NoiseSamplerParams{.seed = 7, .type = NoiseSamplerType::Worley,
                           .octaves = 2, .frequency = 0.3f,
                           .color_mode = NoiseSamplerColorMode::RGB},
        std::nullopt);
    check_parity(*sampler,
        NoiseSamplerParams{.seed = 9, .type = NoiseSamplerType::Ridged,
                           .octaves = 2, .frequency = 0.2f},
        std::nullopt);
    check_parity(*sampler,
        NoiseSamplerParams{.seed = 13, .type = NoiseSamplerType::Value,
                           .octaves = 4, .frequency = 0.15f},
        std::nullopt);
    check_parity(*sampler,
        NoiseSamplerParams{.seed = 17, .type = NoiseSamplerType::Blue,
                           .octaves = 1, .frequency = 0.5f},
        std::nullopt);

    auto grain = renderer::create_banded_grain_effect_processor();
    check_parity(*grain,
        BandedGrainParams{.seed = 5, .intensity = 1.5f, .speed = 0.0f,
                          .color_mode = NoiseColorMode::Monochrome,
                          .shadow_response = 1.0f, .midtone_response = 0.5f,
                          .highlight_response = 0.25f},
        std::nullopt);
    check_parity(*grain,
        BandedGrainParams{.seed = 11, .intensity = 2.0f, .speed = 1.0f,
                          .color_mode = NoiseColorMode::RGB,
                          .bands = {{1.0f, 0.05f, 0}, {4.0f, 0.03f, 2}}},
        std::nullopt);
    BandedGrainParams too_many{.seed = 1};
    too_many.bands = {{1.0f, 0.05f, 0}, {2.0f, 0.05f, 1}, {4.0f, 0.05f, 2},
                      {8.0f, 0.05f, 3}, {16.0f, 0.05f, 4}, {32.0f, 0.05f, 5},
                      {64.0f, 0.05f, 6}};
    CHECK_FALSE(vulkan_strict(too_many, std::nullopt).ok);
    BandedGrainParams non_finite{.seed = 1};
    non_finite.intensity = std::numeric_limits<float>::infinity();
    CHECK_FALSE(vulkan_strict(non_finite, std::nullopt).ok);

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
