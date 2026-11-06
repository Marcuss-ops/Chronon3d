#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

TEST_CASE("Vulkan DoF depth stamp resolves nearest layer independent of submission order") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    const SurfaceDesc desc{4, 4, PixelFormat::Rgba32Float,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0};
    constexpr RenderSurfaceHandle depth_forward = 9701;
    constexpr RenderSurfaceHandle depth_reverse = 9702;
    constexpr RenderSurfaceHandle near_layer = 9703;
    constexpr RenderSurfaceHandle far_layer = 9704;

    const auto first = backend.create_surface(depth_forward, desc);
    if (!first.ok()) {
        MESSAGE("Vulkan DOF depth canary skipped: " << first.error().message);
        return;
    }
    REQUIRE(backend.create_surface(depth_reverse, desc).ok());
    REQUIRE(backend.create_surface(near_layer, desc).ok());
    REQUIRE(backend.create_surface(far_layer, desc).ok());

    std::vector<float> near_rgba(4U * 4U * 4U, 0.0f);
    std::vector<float> far_rgba(4U * 4U * 4U, 0.0f);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            far_rgba[(static_cast<std::size_t>(y) * 4 + x) * 4 + 3] = 1.0f;
        }
    }
    near_rgba[(static_cast<std::size_t>(1) * 4 + 1) * 4 + 3] = 1.0f;
    REQUIRE(backend.upload_surface(near_layer, desc, near_rgba).ok());
    REQUIRE(backend.upload_surface(far_layer, desc, far_rgba).ok());

    const auto reset = [&](RenderSurfaceHandle target) {
        return backend.dof_depth_stamp_surface(target, far_layer, 0.0f, -1.0f,
            0, 0, 0, 0, 0, 0, true, false);
    };
    const auto stamp_near = [&](RenderSurfaceHandle target) {
        return backend.dof_depth_stamp_surface(target, near_layer, 700.0f, 6.0f,
            0, 0, 4, 4, 0, 0, false, false);
    };
    const auto stamp_far = [&](RenderSurfaceHandle target) {
        return backend.dof_depth_stamp_surface(target, far_layer, 1700.0f, 12.0f,
            0, 0, 4, 4, 0, 0, false, false);
    };

    REQUIRE(reset(depth_forward).ok());
    REQUIRE(stamp_far(depth_forward).ok());
    REQUIRE(stamp_near(depth_forward).ok());
    REQUIRE(reset(depth_reverse).ok());
    REQUIRE(stamp_near(depth_reverse).ok());
    REQUIRE(stamp_far(depth_reverse).ok());

    std::vector<float> forward(4U * 4U * 4U, 0.0f);
    std::vector<float> reverse(4U * 4U * 4U, 0.0f);
    REQUIRE(backend.download_surface(depth_forward, forward).ok());
    REQUIRE(backend.download_surface(depth_reverse, reverse).ok());
    CHECK(forward == reverse);

    const auto overlap = (static_cast<std::size_t>(1) * 4 + 1) * 4;
    const auto far_only = 0U;
    const auto uncovered = (static_cast<std::size_t>(3) * 4 + 3) * 4;
    CHECK(forward[overlap] == doctest::Approx(700.0f));
    CHECK(forward[overlap + 1] == doctest::Approx(6.0f));
    CHECK(forward[far_only] == doctest::Approx(1700.0f));
    CHECK(forward[far_only + 1] == doctest::Approx(12.0f));
    CHECK(forward[uncovered] == doctest::Approx(-1.0f));
    CHECK(forward[uncovered + 1] == doctest::Approx(-1.0f));

    std::cout << "[dof-depth-map] rows=z/coc px, '-'=unset\n";
    for (int y = 0; y < 4; ++y) {
        std::cout << "[dof-depth-map]";
        for (int x = 0; x < 4; ++x) {
            const auto i = (static_cast<std::size_t>(y) * 4 + x) * 4;
            if (forward[i] < 0.0f) std::cout << " -";
            else std::cout << ' ' << forward[i] << '/' << forward[i + 1];
        }
        std::cout << '\n';
    }
}

TEST_CASE("Vulkan DoF point highlight fills a circular bokeh footprint") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t size = 33;
    constexpr float radius = 8.0f;
    constexpr RenderSurfaceHandle source_handle = 9711;
    constexpr RenderSurfaceHandle depth_handle = 9712;
    constexpr RenderSurfaceHandle output_handle = 9713;
    const SurfaceDesc desc{size, size, PixelFormat::Rgba32Float,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0};

    const auto created = backend.create_surface(source_handle, desc);
    if (!created.ok()) {
        MESSAGE("Vulkan DoF bokeh canary skipped: " << created.error().message);
        return;
    }
    REQUIRE(backend.create_surface(depth_handle, desc).ok());
    REQUIRE(backend.create_surface(output_handle, desc).ok());

    const auto pixel_count = static_cast<std::size_t>(size) * size;
    std::vector<float> source(pixel_count * 4U, 0.0f);
    std::vector<float> depth(pixel_count * 4U, 0.0f);
    source[((size / 2U) * size + size / 2U) * 4U] = 8.0f;
    source[((size / 2U) * size + size / 2U) * 4U + 1U] = 8.0f;
    source[((size / 2U) * size + size / 2U) * 4U + 2U] = 8.0f;
    source[((size / 2U) * size + size / 2U) * 4U + 3U] = 1.0f;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        depth[i * 4U] = 100.0f;
        depth[i * 4U + 1U] = radius;
    }
    REQUIRE(backend.upload_surface(source_handle, desc, source).ok());
    REQUIRE(backend.upload_surface(depth_handle, desc, depth).ok());
    REQUIRE(backend.dof_blur_disk_surface(output_handle, source_handle,
        depth_handle, 0, 0, static_cast<int>(size), static_cast<int>(size),
        radius, false, 0.0f).ok());

    std::vector<float> output(pixel_count * 4U, 0.0f);
    REQUIRE(backend.download_surface(output_handle, output).ok());
    float peak = 0.0f;
    for (std::size_t i = 0; i < pixel_count; ++i)
        peak = std::max(peak, output[i * 4U]);
    REQUIRE(peak > 0.0f);

    // A circular optical point response should occupy most of its disk and
    // leave the square's corners dark. The threshold is relative to peak so
    // the check is independent of sample-count normalization.
    std::size_t lit = 0;
    std::size_t disk_pixels = 0;
    std::size_t lit_square_corners = 0;
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) - size / 2U;
            const float dy = static_cast<float>(y) - size / 2U;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const bool on = output[(static_cast<std::size_t>(y) * size + x) * 4U]
                > peak * 0.2f;
            if (distance <= radius + 0.5f) {
                ++disk_pixels;
                if (on) ++lit;
            } else if (std::abs(dx) <= radius && std::abs(dy) <= radius && on) {
                ++lit_square_corners;
            }
        }
    }
    const float occupancy = static_cast<float>(lit) / disk_pixels;
    INFO("point-highlight disk occupancy=" << occupancy
         << ", lit square-corner pixels=" << lit_square_corners
         << ", peak=" << peak);
    CHECK(occupancy >= 0.65f);
    CHECK(lit_square_corners == 0U);

    // Optional raw diagnostic capture for reproducing the pixel-shape review.
    // Tests stay side-effect free unless the caller names an output path.
    if (const char* path = std::getenv("CHRONON3D_DOF_BOKEH_PGM")) {
        std::ofstream pgm(path, std::ios::binary);
        REQUIRE(pgm.good());
        pgm << "P5\n" << size << ' ' << size << "\n255\n";
        for (std::size_t i = 0; i < pixel_count; ++i) {
            const float normalized = std::clamp(output[i * 4U] / peak, 0.0f, 1.0f);
            const auto encoded = static_cast<unsigned char>(
                std::lround(std::sqrt(normalized) * 255.0f));
            pgm.write(reinterpret_cast<const char*>(&encoded), 1);
        }
        REQUIRE(pgm.good());
        std::cout << "[dof-bokeh-pgm] " << path << '\n';
    }

    for (const auto handle : {source_handle, depth_handle, output_handle})
        REQUIRE(backend.release_surface(handle).ok());
}

TEST_CASE("Vulkan DoF keeps an in-focus foreground bar clean of far bokeh") {
    using namespace chronon3d::runtime;
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t size = 33;
    constexpr float far_depth = 100.0f;
    constexpr float far_radius = 8.0f;
    constexpr RenderSurfaceHandle source_handle = 9721;
    constexpr RenderSurfaceHandle depth_handle = 9722;
    constexpr RenderSurfaceHandle output_handle = 9723;
    const SurfaceDesc desc{size, size, PixelFormat::Rgba32Float,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0};
    const auto created = backend.create_surface(source_handle, desc);
    if (!created.ok()) {
        MESSAGE("Vulkan DoF occlusion canary skipped: " << created.error().message);
        return;
    }
    REQUIRE(backend.create_surface(depth_handle, desc).ok());
    REQUIRE(backend.create_surface(output_handle, desc).ok());

    const auto pixel_count = static_cast<std::size_t>(size) * size;
    std::vector<float> source(pixel_count * 4U, 0.0f);
    std::vector<float> depth(pixel_count * 4U, 0.0f);
    const auto index = [](std::uint32_t x, std::uint32_t y) {
        return (static_cast<std::size_t>(y) * size + x) * 4U;
    };
    source[index(10, 16)] = 8.0f;
    source[index(10, 16) + 1U] = 8.0f;
    source[index(10, 16) + 2U] = 8.0f;
    source[index(10, 16) + 3U] = 1.0f;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        depth[i * 4U] = far_depth;
        depth[i * 4U + 1U] = far_radius;
    }
    // A sharp opaque foreground strip overlaps the far point's blur disk.
    for (std::uint32_t y = 12; y <= 20; ++y) {
        for (std::uint32_t x = 13; x <= 17; ++x) {
            const auto i = index(x, y);
            source[i] = 0.0f;
            source[i + 1U] = 1.0f;
            source[i + 2U] = 0.0f;
            source[i + 3U] = 1.0f;
            depth[i] = 50.0f;
            depth[i + 1U] = 0.0f;
        }
    }
    REQUIRE(backend.upload_surface(source_handle, desc, source).ok());
    REQUIRE(backend.upload_surface(depth_handle, desc, depth).ok());
    REQUIRE(backend.dof_blur_disk_surface(output_handle, source_handle,
        depth_handle, 0, 0, static_cast<int>(size), static_cast<int>(size),
        far_radius, false, 0.0f).ok());
    std::vector<float> output(pixel_count * 4U, 0.0f);
    REQUIRE(backend.download_surface(output_handle, output).ok());

    const auto foreground = index(15, 16);
    const auto defocused_edge = index(18, 16);
    INFO("foreground RGB=" << output[foreground] << ',' << output[foreground + 1U]
         << ',' << output[foreground + 2U] << "; far blur edge red="
         << output[defocused_edge]);
    CHECK(output[foreground] < 1e-5f);
    CHECK(output[foreground + 1U] > 0.99f);
    CHECK(output[foreground + 2U] < 1e-5f);
    CHECK(output[defocused_edge] > 1e-4f);

    for (const auto handle : {source_handle, depth_handle, output_handle})
        REQUIRE(backend.release_surface(handle).ok());
}
#endif
