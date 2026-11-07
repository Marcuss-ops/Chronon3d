#include <doctest/doctest.h>

#include <chronon3d/rendering/shadow_map.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#endif

namespace {

using chronon3d::Mat4;
using chronon3d::Vec3;
using chronon3d::Vec4;
using namespace chronon3d::rendering;

Mat4 identity_light_matrix() {
    // Zero-to-one left-handed orthographic projection over [-1, 1]: world
    // (x, y, z) maps directly to clip (x, y, z) with z in [0, 1].
    return glm::orthoLH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);
}

Mat4 pixel_light_matrix(std::uint32_t resolution) {
    // The apply kernel treats layer pixels as world units: pixel (x, y) is at
    // world (origin + x + 0.5, origin + y + 0.5). A 0..resolution orthography
    // maps that space onto the full shadow map.
    const float extent = static_cast<float>(resolution);
    return glm::orthoLH_ZO(0.0f, extent, 0.0f, extent, 0.0f, 1.0f);
}

std::vector<float> project_quad(const Mat4& light, float center,
                                float half_extent, float depth,
                                std::uint32_t resolution) {
    const Vec3 corners[4] = {
        {center - half_extent, center - half_extent, depth},
        {center + half_extent, center - half_extent, depth},
        {center + half_extent, center + half_extent, depth},
        {center - half_extent, center + half_extent, depth}};
    std::vector<float> vertices;
    vertices.reserve(16);
    for (const Vec3& corner : corners) {
        const auto projected = project_shadow_point(light, corner, resolution);
        REQUIRE(projected.valid);
        vertices.insert(vertices.end(),
                        {projected.x, projected.y, projected.depth, 1.0f});
    }
    return vertices;
}

} // namespace

TEST_CASE("Shadow diffusion widens the PCF radius and stays bounded") {
    ShadowMapSettings settings;
    settings.pcf_radius_px = 1.0f;
    settings.diffusion_px = 0.0f;
    CHECK(shadow_map_pcf_radius(settings) == doctest::Approx(1.0f));
    settings.diffusion_px = 50.0f;
    CHECK(shadow_map_pcf_radius(settings) == doctest::Approx(20.0f));
}

TEST_CASE("Shadow light matrices project the scene centre and depth range") {
    const auto directional = build_directional_shadow_map(
        Vec3{0.0f, 0.0f, -1.0f}, Vec3{0.0f, 0.0f, 0.0f}, 10.0f, 2.0f);
    REQUIRE(directional.valid);
    const auto centre = project_shadow_point(
        directional.view_projection, Vec3{0.0f, 0.0f, 0.0f}, 256);
    REQUIRE(centre.valid);
    CHECK(centre.x == doctest::Approx(128.0f).epsilon(0.01));
    CHECK(centre.y == doctest::Approx(128.0f).epsilon(0.01));

    const auto spot = build_spot_shadow_map(
        Vec3{0.0f, 0.0f, -4.0f}, Vec3{0.0f, 0.0f, 1.0f}, 0.5f, 0.5f, 20.0f);
    REQUIRE(spot.valid);
    const auto on_axis = project_shadow_point(
        spot.view_projection, Vec3{0.0f, 0.0f, 0.0f}, 128);
    REQUIRE(on_axis.valid);
    CHECK(on_axis.x == doctest::Approx(64.0f).epsilon(0.01));
    CHECK(on_axis.y == doctest::Approx(64.0f).epsilon(0.01));
}

TEST_CASE("Shadow adaptive bias grows at grazing angles") {
    ShadowMapSettings settings;
    settings.depth_bias = 0.001f;
    settings.normal_bias = 0.01f;
    const float facing = shadow_map_adaptive_bias(
        settings, Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 1.0f});
    const float grazing = shadow_map_adaptive_bias(
        settings, Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.95f, 0.0f, 0.312f});
    CHECK(facing == doctest::Approx(0.001f));
    CHECK(grazing > facing);
}

TEST_CASE("Shadow CPU PCF reference resolves coverage, depth and diffusion") {
    constexpr std::uint32_t size = 16;
    std::vector<float> depth_map(static_cast<std::size_t>(size) * size * 4U, 0.0f);
    // Left half is a caster at depth 0.4; the right half is empty.
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size / 2; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * size + x) * 4U;
            depth_map[index] = 0.4f;
            depth_map[index + 1U] = 1.0f;
        }
    }
    ShadowMapSettings settings;
    settings.enabled = true;
    settings.pcf_taps = 1;
    settings.pcf_radius_px = 0.0f;
    settings.depth_bias = 0.0025f;
    settings.normal_bias = 0.0f;
    settings.strength = 1.0f;

    // World x = 0 is the caster boundary; the left sample is occluded at
    // receiver depth 0.6 and the right sample is empty.
    const float inside = shadow_factor_cpu(
        depth_map, size, size, identity_light_matrix(),
        Vec3{-0.2f, 0.0f, 0.6f}, Vec3{0.0f, 0.0f, 1.0f},
        Vec3{0.0f, 0.0f, 1.0f}, settings);
    const float outside = shadow_factor_cpu(
        depth_map, size, size, identity_light_matrix(),
        Vec3{0.2f, 0.0f, 0.6f}, Vec3{0.0f, 0.0f, 1.0f},
        Vec3{0.0f, 0.0f, 1.0f}, settings);
    CHECK(inside == doctest::Approx(0.0f));
    CHECK(outside == doctest::Approx(1.0f));

    // A receiver in front of the caster stays lit regardless of coverage.
    const float in_front = shadow_factor_cpu(
        depth_map, size, size, identity_light_matrix(),
        Vec3{-0.2f, 0.0f, 0.2f}, Vec3{0.0f, 0.0f, 1.0f},
        Vec3{0.0f, 0.0f, 1.0f}, settings);
    CHECK(in_front == doctest::Approx(1.0f));

    // Diffusion near the boundary produces a fractional visibility.
    settings.pcf_taps = 5;
    settings.diffusion_px = 2.0f;
    const float soft = shadow_factor_cpu(
        depth_map, size, size, identity_light_matrix(),
        Vec3{0.0f, 0.0f, 0.6f}, Vec3{0.0f, 0.0f, 1.0f},
        Vec3{0.0f, 0.0f, 1.0f}, settings);
    CHECK(soft > 0.0f);
    CHECK(soft < 1.0f);
}

#ifdef CHRONON3D_ENABLE_VULKAN

TEST_CASE("Vulkan shadow map caster, PCF and adaptive bias are measurable") {
    using namespace chronon3d::runtime;
    namespace vk = chronon3d::backends::vulkan;

    constexpr std::uint32_t size = 32;
    constexpr RenderSurfaceHandle depth_handle = 9851;
    constexpr RenderSurfaceHandle receiver_handle = 9852;
    constexpr RenderSurfaceHandle output_handle = 9853;
    constexpr RenderSurfaceHandle pcf_output_handle = 9854;
    constexpr RenderSurfaceHandle bias_output_handle = 9855;
    const SurfaceDesc desc{size, size, PixelFormat::Rgba32Float,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0};

    vk::VulkanBackend backend;
    const auto created = backend.create_surface(depth_handle, desc);
    if (!created.ok()) {
        MESSAGE("Vulkan shadow-map canary skipped: " << created.error().message);
        return;
    }
    REQUIRE(backend.supports_shadow_map());
    REQUIRE(backend.create_surface(receiver_handle, desc).ok());
    REQUIRE(backend.create_surface(output_handle, desc).ok());
    REQUIRE(backend.create_surface(pcf_output_handle, desc).ok());
    REQUIRE(backend.create_surface(bias_output_handle, desc).ok());

    std::vector<float> receiver(static_cast<std::size_t>(size) * size * 4U, 1.0f);
    REQUIRE(backend.upload_surface(receiver_handle, desc, receiver).ok());

    ShadowMapLight light;
    light.view_projection = pixel_light_matrix(size);
    light.direction = Vec3{0.0f, 0.0f, 1.0f};
    light.valid = true;

    ShadowMapSettings settings;
    settings.enabled = true;
    settings.resolution = size;
    settings.pcf_taps = 1;
    settings.pcf_radius_px = 0.0f;
    settings.diffusion_px = 0.0f;
    settings.depth_bias = 0.0025f;
    settings.normal_bias = 0.0f;
    settings.strength = 1.0f;

    const std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
    const auto caster = project_quad(light.view_projection, 16.0f, 8.0f,
                                     0.4f, size);
    REQUIRE(backend.shadow_map_caster_surface(
        depth_handle, caster, indices, 0, 0, size, size).ok());

    const auto apply = [&](RenderSurfaceHandle output, float depth_bias,
                           std::uint32_t taps, float radius) {
        auto local = settings;
        local.depth_bias = depth_bias;
        local.pcf_taps = taps;
        local.pcf_radius_px = radius;
        local.diffusion_px = 0.0f;
        const auto params = make_shadow_apply_params(
            light, local, Vec3{0.0f, 0.0f, 1.0f}, 0.6f, 0, 0);
        return backend.shadow_map_apply_surface(
            output, receiver_handle, depth_handle, params);
    };
    const auto pixel = [](std::uint32_t x, std::uint32_t y) {
        return (static_cast<std::size_t>(y) * size + x) * 4U;
    };

    REQUIRE(apply(output_handle, 0.0025f, 1, 0.0f).ok());
    std::vector<float> hard(static_cast<std::size_t>(size) * size * 4U, 0.0f);
    REQUIRE(backend.download_surface(output_handle, hard).ok());
    INFO("hard centre=" << hard[pixel(16, 16)]
         << " edge-in=" << hard[pixel(23, 16)]
         << " edge-out=" << hard[pixel(24, 16)]
         << " corner=" << hard[pixel(1, 1)]);
    CHECK(hard[pixel(16, 16)] <= 0.02f);
    CHECK(hard[pixel(23, 16)] <= 0.02f);
    CHECK(hard[pixel(24, 16)] >= 0.98f);
    CHECK(hard[pixel(1, 1)] >= 0.98f);

    // PCF must soften the shadow edge without moving it.
    REQUIRE(apply(pcf_output_handle, 0.0025f, 5, 2.0f).ok());
    std::vector<float> soft(static_cast<std::size_t>(size) * size * 4U, 0.0f);
    REQUIRE(backend.download_surface(pcf_output_handle, soft).ok());
    INFO("pcf edge-in=" << soft[pixel(23, 16)]
         << " edge-out=" << soft[pixel(24, 16)]);
    CHECK(soft[pixel(23, 16)] > 0.2f);
    CHECK(soft[pixel(23, 16)] < 0.95f);
    CHECK(soft[pixel(23, 16)] > hard[pixel(23, 16)]);
    CHECK(soft[pixel(24, 16)] < hard[pixel(24, 16)]);
    CHECK(soft[pixel(16, 16)] <= 0.02f);

    // A caster just in front of the receiver must stop self-shadowing once
    // the adaptive bias exceeds the depth gap.
    const auto near_caster = project_quad(light.view_projection, 16.0f, 8.0f,
                                          0.595f, size);
    REQUIRE(backend.shadow_map_caster_surface(
        depth_handle, near_caster, indices, 0, 0, size, size).ok());
    REQUIRE(apply(bias_output_handle, 0.0f, 1, 0.0f).ok());
    std::vector<float> acne(static_cast<std::size_t>(size) * size * 4U, 0.0f);
    REQUIRE(backend.download_surface(bias_output_handle, acne).ok());
    CHECK(acne[pixel(16, 16)] <= 0.02f);
    REQUIRE(apply(bias_output_handle, 0.01f, 1, 0.0f).ok());
    std::vector<float> biased(static_cast<std::size_t>(size) * size * 4U, 0.0f);
    REQUIRE(backend.download_surface(bias_output_handle, biased).ok());
    INFO("bias centre without=" << acne[pixel(16, 16)]
         << " with=" << biased[pixel(16, 16)]);
    CHECK(biased[pixel(16, 16)] >= 0.98f);

    // Restore the hard-shadow caster, then compare the CPU reference against
    // the exact image the caster kernel produced.
    REQUIRE(backend.shadow_map_caster_surface(
        depth_handle, caster, indices, 0, 0, size, size).ok());
    std::vector<float> depth_pixels(static_cast<std::size_t>(size) * size * 4U, 0.0f);
    REQUIRE(backend.download_surface(depth_handle, depth_pixels).ok());
    const auto pixel_world = [&](std::uint32_t x, std::uint32_t y) {
        return Vec3{static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f, 0.6f};
    };
    const float cpu_inside = shadow_factor_cpu(
        depth_pixels, size, size, light.view_projection,
        pixel_world(16, 16), Vec3{0.0f, 0.0f, 1.0f},
        Vec3{0.0f, 0.0f, 1.0f}, settings);
    const float cpu_outside = shadow_factor_cpu(
        depth_pixels, size, size, light.view_projection,
        pixel_world(24, 16), Vec3{0.0f, 0.0f, 1.0f},
        Vec3{0.0f, 0.0f, 1.0f}, settings);
    INFO("cpu inside=" << cpu_inside << " outside=" << cpu_outside);
    CHECK(cpu_inside <= 0.02f);
    CHECK(cpu_outside >= 0.98f);

    // Machine-readable evidence for the AE audit. The audit only certifies a
    // PASS when this report exists and satisfies the same tolerances.
    float edge_px = 0.0f;
    for (std::uint32_t x = 0; x < size; ++x) {
        if (hard[pixel(x, 16)] >= 0.5f) {
            edge_px = static_cast<float>(x);
            break;
        }
    }
    const float edge_error_px = std::abs(edge_px - 24.0f);
    const bool pcf_softened =
        soft[pixel(23, 16)] > hard[pixel(23, 16)] + 0.05f &&
        soft[pixel(24, 16)] < hard[pixel(24, 16)] - 0.05f;
    const bool bias_removed_acne =
        acne[pixel(16, 16)] <= 0.02f && biased[pixel(16, 16)] >= 0.98f;
    const float cpu_gpu_max_delta = std::max(
        std::abs(cpu_inside - hard[pixel(16, 16)]),
        std::abs(cpu_outside - hard[pixel(24, 16)]));
    const bool report_pass =
        edge_error_px <= 1.0f && pcf_softened && bias_removed_acne &&
        cpu_gpu_max_delta <= 0.02f &&
        soft[pixel(23, 16)] > 0.05f && soft[pixel(23, 16)] < 0.95f;
    CHECK(pcf_softened);
    CHECK(bias_removed_acne);
    CHECK(edge_error_px <= 1.0f);
    CHECK(cpu_gpu_max_delta <= 0.02f);
    CHECK(report_pass);

    if (const char* report_path = std::getenv("CHRONON3D_SHADOW_MAP_REPORT")) {
        const std::filesystem::path path(report_path);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream report(path);
        REQUIRE(report.good());
        report << "{\n"
               << "  \"schema\": \"chronon3d.shadow-map.evidence.v1\",\n"
               << "  \"status\": \"" << (report_pass ? "PASS" : "FAIL") << "\",\n"
               << "  \"production_vulkan_shadow_map\": true,\n"
               << "  \"pcf_kernel\": true,\n"
               << "  \"adaptive_normal_bias\": true,\n"
               << "  \"measured\": true,\n"
               << "  \"resolution\": " << size << ",\n"
               << "  \"edge_error_px\": " << edge_error_px << ",\n"
               << "  \"pcf_edge_value\": " << soft[pixel(23, 16)] << ",\n"
               << "  \"pcf_softened\": " << (pcf_softened ? "true" : "false") << ",\n"
               << "  \"bias_removed_acne\": " << (bias_removed_acne ? "true" : "false") << ",\n"
               << "  \"cpu_gpu_max_delta\": " << cpu_gpu_max_delta << "\n"
               << "}\n";
        report.close();
        REQUIRE(report.good());
        std::cout << "[shadow-map-report] " << path.string() << '\n';
    }

    for (const auto handle : {depth_handle, receiver_handle, output_handle,
                              pcf_output_handle, bias_output_handle})
        REQUIRE(backend.release_surface(handle).ok());
}

#endif // CHRONON3D_ENABLE_VULKAN
