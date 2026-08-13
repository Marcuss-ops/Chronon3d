#include <doctest/doctest.h>

#include <chronon3d/simd/kernel_registry.hpp>
#include <chronon3d/simd/kernel_test_harness.hpp>

namespace {
void test_blend(float* dst, const float* src, std::size_t count) {
    chronon3d::simd::detail::scalar_blend(dst, src, count);
}
}

TEST_CASE("KernelRegistry resolves typed implementations with scalar fallback") {
    using namespace chronon3d::simd;
    KernelRegistry registry;
    registry.register_kernel(KernelId::Blend, CpuIsa::AVX2, &test_blend);

    CpuCapabilities caps{CpuIsa::AVX2, false, true, false, false};
    const auto& table = registry.resolve(caps);
    CHECK(table.blend.apply == &test_blend);
    CHECK(table.blur.apply == kScalarSet.blur.apply);
    CHECK(registry.has(CpuIsa::AVX2));
}

TEST_CASE("KernelRegistry keeps scalar reference available") {
    using namespace chronon3d::simd;
    KernelRegistry registry;
    CpuCapabilities caps{CpuIsa::NEON, false, false, false, true};
    CHECK(registry.resolve(caps).blend.apply == kScalarSet.blend.apply);
}

TEST_CASE("Kernel harness covers tails, odd sizes and padded strides") {
    const auto cases = chronon3d::simd::default_kernel_test_cases();
    CHECK(cases.size() >= 30);
    CHECK(chronon3d::simd::values_match(1.0f, 1.0f,
        {.absolute = 0.0f, .relative = 0.0f}));
    CHECK_FALSE(chronon3d::simd::values_match(1.0f, 1.1f,
        {.absolute = 0.0f, .relative = 0.0f}));
}
