#include <doctest/doctest.h>

#include <chronon3d/render_graph/effect_gpu_capability.hpp>

using namespace chronon3d;

TEST_CASE("strict GPU effect capability rows are explicit and fail closed") {
    using effects::EffectType;
    using graph::GpuEffectRoute;
    CHECK(graph::kGpuEffectCapabilityRows.size() == 5);

    const auto* blur = graph::gpu_effect_capability(EffectType::Blur);
    REQUIRE(blur != nullptr);
    CHECK(blur->route == GpuEffectRoute::NativeZeroReadback);

    const auto* shadow = graph::gpu_effect_capability(EffectType::DropShadow);
    REQUIRE(shadow != nullptr);
    CHECK(shadow->route == GpuEffectRoute::Unsupported);
    CHECK(shadow->diagnostic.find("not implemented") != std::string_view::npos);

    const auto* bloom = graph::gpu_effect_capability(EffectType::Bloom);
    REQUIRE(bloom != nullptr);
    CHECK(bloom->route == GpuEffectRoute::NativeZeroReadback);
    CHECK(bloom->diagnostic.find("bright-pass") != std::string_view::npos);

    CHECK(graph::gpu_effect_capability(EffectType::Unknown) == nullptr);
}
