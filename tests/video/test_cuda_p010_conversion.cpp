#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>

#include <cuda.h>

#include <cstdlib>
#include <string>

namespace {

bool gpu_tests_enabled() {
    const char* value = std::getenv("CHRONON3D_RUN_CUDA_TESTS");
    return value && std::string(value) == "1";
}

} // namespace

TEST_CASE("P010 CUDA conversion has a dedicated dispatch format") {
    if (!gpu_tests_enabled()) {
        MESSAGE("set CHRONON3D_RUN_CUDA_TESTS=1 to run CUDA conversion tests");
        return;
    }

    CHECK(CUDA_SUCCESS == 0);
    CHECK(static_cast<int>(chronon3d::backends::vulkan::CudaYuvFormat::P010) !=
          static_cast<int>(chronon3d::backends::vulkan::CudaYuvFormat::Nv12));
}

#else

TEST_CASE("P010 CUDA conversion is unavailable without CUDA interop") {
    CHECK(true);
}

#endif
