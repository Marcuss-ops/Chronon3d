#include <doctest/doctest.h>

#include <chronon3d/render_graph/effect_gpu_capability.hpp>

#include <set>

using namespace chronon3d;

TEST_CASE("strict GPU effect capability rows are explicit and fail closed") {
    using effects::EffectType;
    using graph::GpuEffectRoute;

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

    for (const auto type : {EffectType::Brightness, EffectType::Contrast,
                            EffectType::Saturation, EffectType::HueRotate,
                            EffectType::Invert, EffectType::Exposure,
                            EffectType::Fill, EffectType::Levels,
                            EffectType::Threshold}) {
        const auto* row = graph::gpu_effect_capability(type);
        REQUIRE(row != nullptr);
        CHECK(row->route == GpuEffectRoute::NativeZeroReadback);
    }
    for (const auto type : {EffectType::Curves, EffectType::Lut3D,
                            EffectType::Vignette, EffectType::Noise}) {
        const auto* row = graph::gpu_effect_capability(type);
        REQUIRE(row != nullptr);
        CHECK(row->route == GpuEffectRoute::Unsupported);
    }

    CHECK(graph::gpu_effect_capability(EffectType::Unknown) == nullptr);
}

TEST_CASE("strict GPU effect capability census covers every catalog effect once") {
    std::set<effects::EffectType> seen;
    std::size_t catalog_count = 0;
#define CHRONON_EFFECT(val, enum_name, ...)                                   \
    ++catalog_count;                                                          \
    CHECK(graph::gpu_effect_capability(effects::EffectType::enum_name) != nullptr);
#include <chronon3d/effects/effect_catalog.def>
#undef CHRONON_EFFECT
    for (const auto& row : graph::kGpuEffectCapabilityRows)
        CHECK(seen.insert(row.effect).second);
    CHECK(graph::kGpuEffectCapabilityRows.size() == catalog_count);
    std::size_t native = 0;
    for (const auto& row : graph::kGpuEffectCapabilityRows)
        if (row.route == graph::GpuEffectRoute::NativeZeroReadback) ++native;
    CHECK(native == 13);
}
