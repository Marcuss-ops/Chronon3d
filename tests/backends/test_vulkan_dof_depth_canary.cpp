#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <cstddef>
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
#endif
