#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d::simd {

struct KernelTolerance {
    float absolute{0.0f};
    float relative{0.0f};
    std::uint32_t max_ulp{0};
};

struct KernelTestCase {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t stride{0};
    std::uint32_t seed{0};
};

[[nodiscard]] inline std::vector<KernelTestCase> default_kernel_test_cases() {
    constexpr std::array<std::array<std::uint32_t, 2>, 12> sizes = {{
        {1, 1}, {2, 2}, {3, 7}, {15, 13}, {16, 16}, {17, 17},
        {31, 9}, {127, 63}, {128, 128}, {129, 129}, {1920, 1}, {1, 1080}
    }};
    std::vector<KernelTestCase> result;
    result.reserve(std::size(sizes) * 3);
    std::uint32_t seed = 0x9e3779b9u;
    for (const auto& size : sizes) {
        for (const auto stride_extra : {0u, 1u, 10u}) {
            result.push_back({size[0], size[1], size[0] + stride_extra, seed});
            seed = seed * 1664525u + 1013904223u;
        }
    }
    return result;
}

[[nodiscard]] inline bool values_match(float actual, float reference,
                                        KernelTolerance tolerance) noexcept {
    if (std::isnan(actual) || std::isnan(reference)) return false;
    const float error = std::fabs(actual - reference);
    return error <= tolerance.absolute + tolerance.relative * std::fabs(reference);
}


struct KernelBenchmarkResult {
    double elapsed_ms{0.0};
    double ns_per_element{0.0};
};


} // namespace chronon3d::simd
