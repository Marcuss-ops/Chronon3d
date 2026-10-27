#pragma once
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

#endif // CHRONON3D_ENABLE_VULKAN
