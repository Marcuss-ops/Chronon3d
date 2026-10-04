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
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

namespace chronon3d::renderer {
std::unique_ptr<EffectProcessor> create_brightness_effect_processor();
std::unique_ptr<EffectProcessor> create_blur_effect_processor();
std::unique_ptr<EffectProcessor> create_contrast_effect_processor();
std::unique_ptr<EffectProcessor> create_saturation_effect_processor();
std::unique_ptr<EffectProcessor> create_hue_rotate_effect_processor();
std::unique_ptr<EffectProcessor> create_invert_effect_processor();
std::unique_ptr<EffectProcessor> create_exposure_effect_processor();
std::unique_ptr<EffectProcessor> create_fill_effect_processor();
std::unique_ptr<EffectProcessor> create_levels_effect_processor();
std::unique_ptr<EffectProcessor> create_threshold_effect_processor();
std::unique_ptr<EffectProcessor> create_keying_effect_processor();
std::unique_ptr<EffectProcessor> create_drop_shadow_effect_processor();
} // namespace chronon3d::renderer

namespace {

using namespace chronon3d;
using namespace chronon3d::graph;

constexpr int kW = 8;
constexpr int kH = 4;
// Approximate tolerance class: GPU transcendental functions (pow/exp2/cos)
// and FMA contraction may differ from the scalar CPU path by a few ULP.
constexpr float kTolerance = 2.0e-4f;

std::shared_ptr<Framebuffer> make_source() {
    auto fb = std::make_shared<Framebuffer>(kW, kH);
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
                                   const std::optional<raster::BBox>& clip) {
    auto fb = make_source();
    effects::EffectExecutionContext ctx{};
    ctx.clip = clip;
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
                        const std::optional<raster::BBox>& clip) {
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
    EffectStack effects;
    effects::EffectInstance inst;
    inst.params = params;
    inst.effect_type = effects::detect_effect_type(params);
    effects.push_back(std::move(inst));
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

VulkanRun vulkan_strict_stack(const EffectStack& stack) {
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = kW;
    ctx.frame_input.height = kH;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;

    auto source = make_source();
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

void check_parity(renderer::EffectProcessor& processor, const EffectParams& params,
                  const std::optional<raster::BBox>& clip = std::nullopt) {
    const auto expected = software_oracle(processor, params, clip);
    const auto actual = vulkan_strict(params, clip);
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
}

TEST_CASE("Vulkan native color effects honor the software clip contract") {
    const raster::BBox clip{2, 1, 6, 3};
    auto brightness = renderer::create_brightness_effect_processor();
    check_parity(*brightness, BrightnessParams{.value = 0.2f}, clip);
    auto invert = renderer::create_invert_effect_processor();
    check_parity(*invert, InvertParams{.amount = 0.8f}, clip);
    auto threshold = renderer::create_threshold_effect_processor();
    check_parity(*threshold, ThresholdParams{.threshold = 0.4f, .softness = 0.15f}, clip);
    // Software Exposure/Levels/Fill ignore the clip; native must too.
    auto exposure = renderer::create_exposure_effect_processor();
    check_parity(*exposure, ExposureParams{.stops = 1.0f}, clip);
}

TEST_CASE("Vulkan luma and chroma key match the software processor") {
    auto processor = renderer::create_keying_effect_processor();
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

TEST_CASE("Strict Vulkan applies certified effect stacks in authored order") {
    auto source = make_source();
    auto expected = *source;
    auto brightness = renderer::create_brightness_effect_processor();
    auto invert = renderer::create_invert_effect_processor();
    effects::EffectExecutionContext context{};
    brightness->apply(expected, BrightnessParams{.value = 0.12f}, context);
    invert->apply(expected, InvertParams{.amount = 0.4f}, context);

    EffectStack stack;
    stack.push_back(effects::EffectInstance{BrightnessParams{.value = 0.12f}});
    effects::EffectInstance disabled{InvertParams{.amount = 0.9f}};
    disabled.enabled = false;
    stack.push_back(std::move(disabled));
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

TEST_CASE("Strict Vulkan still fails closed for non-native effects") {
    const auto run = vulkan_strict(
        DropShadowParams{.offset = {2.0f, 2.0f}, .color = Color{0, 0, 0, 0.5f}, .radius = 3.0f},
        std::nullopt);
    CHECK_FALSE(run.ok);
    CHECK(run.error.find("drop-shadow") != std::string::npos);

    const auto nan_run = vulkan_strict(BrightnessParams{.value = std::nanf("")}, std::nullopt);
    CHECK_FALSE(nan_run.ok);
}

TEST_CASE("Strict Vulkan rejects Echo without a native temporal surface history") {
    EffectStack stack;
    stack.push_back(effects::EffectInstance{EchoParams{
        .echoes = 2, .decay = 0.5f, .time_offset_frames = 1}});

    const auto run = vulkan_strict_stack(stack);
    CHECK_FALSE(run.ok);
    CHECK_FALSE(run.error.empty());
}

#endif // CHRONON3D_ENABLE_VULKAN
