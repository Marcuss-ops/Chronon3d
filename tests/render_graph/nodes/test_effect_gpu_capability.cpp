#include <doctest/doctest.h>

#include <chronon3d/render_graph/effect_gpu_capability.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>

#include <set>

using namespace chronon3d;

TEST_CASE("strict GPU effect capability rows are explicit and fail closed") {
    using effects::EffectType;
    using graph::GpuEffectRoute;

    const auto* blur = graph::gpu_effect_capability(EffectType::Blur);
    REQUIRE(blur != nullptr);
    CHECK(blur->route == GpuEffectRoute::NativeZeroReadback);

    const auto* sharpen = graph::gpu_effect_capability(EffectType::Sharpen);
    REQUIRE(sharpen != nullptr);
    CHECK(sharpen->route == GpuEffectRoute::NativeZeroReadback);
    CHECK(sharpen->diagnostic.find("unsharp-mask") != std::string_view::npos);

    const auto* shadow = graph::gpu_effect_capability(EffectType::DropShadow);
    REQUIRE(shadow != nullptr);
    CHECK(shadow->route == GpuEffectRoute::NativeZeroReadback);
    CHECK(shadow->diagnostic.find("alpha-mask") != std::string_view::npos);

    const auto* bloom = graph::gpu_effect_capability(EffectType::Bloom);
    REQUIRE(bloom != nullptr);
    CHECK(bloom->route == GpuEffectRoute::NativeZeroReadback);
    CHECK(bloom->diagnostic.find("bright-pass") != std::string_view::npos);

    const auto* keying = graph::gpu_effect_capability(EffectType::Keying);
    REQUIRE(keying != nullptr);
    CHECK(keying->route == GpuEffectRoute::NativeZeroReadback);

    const auto* vignette = graph::gpu_effect_capability(EffectType::Vignette);
    REQUIRE(vignette != nullptr);
    CHECK(vignette->route == GpuEffectRoute::NativeZeroReadback);

    const auto* mosaic = graph::gpu_effect_capability(EffectType::Mosaic);
    REQUIRE(mosaic != nullptr);
    CHECK(mosaic->route == GpuEffectRoute::NativeZeroReadback);

    const auto* echo = graph::gpu_effect_capability(EffectType::Echo);
    REQUIRE(echo != nullptr);
    CHECK(echo->route == GpuEffectRoute::NativeZeroReadback);
    CHECK(echo->diagnostic.find("frame-addressed JobSession history") !=
          std::string_view::npos);

    const auto* rgb = graph::gpu_effect_capability(EffectType::ChromaticAberration);
    REQUIRE(rgb != nullptr);
    CHECK(rgb->route == GpuEffectRoute::NativeZeroReadback);
    CHECK(rgb->diagnostic.find("RGB") != std::string_view::npos);

    const auto* temperature_tint = graph::gpu_effect_capability(EffectType::TemperatureTint);
    REQUIRE(temperature_tint != nullptr);
    CHECK(temperature_tint->route == GpuEffectRoute::NativeZeroReadback);
    const auto* lift_gamma_gain = graph::gpu_effect_capability(EffectType::LiftGammaGain);
    REQUIRE(lift_gamma_gain != nullptr);
    CHECK(lift_gamma_gain->route == GpuEffectRoute::NativeZeroReadback);
    const auto* offset = graph::gpu_effect_capability(EffectType::Offset);
    REQUIRE(offset != nullptr);
    CHECK(offset->route == GpuEffectRoute::NativeZeroReadback);
    const auto* noise = graph::gpu_effect_capability(EffectType::Noise);
    REQUIRE(noise != nullptr);
    CHECK(noise->route == GpuEffectRoute::NativeZeroReadback);
    const auto* checkerboard = graph::gpu_effect_capability(EffectType::Checkerboard);
    REQUIRE(checkerboard != nullptr);
    CHECK(checkerboard->route == GpuEffectRoute::NativeZeroReadback);

    const auto* directional_blur = graph::gpu_effect_capability(EffectType::DirectionalBlur);
    REQUIRE(directional_blur != nullptr);
    CHECK(directional_blur->route == GpuEffectRoute::NativeZeroReadback);

    for (const auto type : {EffectType::Brightness, EffectType::Contrast,
                            EffectType::Saturation, EffectType::HueRotate,
                            EffectType::Invert, EffectType::Exposure,
                            EffectType::TemperatureTint,
                            EffectType::LiftGammaGain,
                            EffectType::Offset,
                            EffectType::Noise,
                            EffectType::Checkerboard,
                            EffectType::Fill, EffectType::Levels,
                            EffectType::Threshold, EffectType::Vignette,
                            EffectType::Mosaic}) {
        const auto* row = graph::gpu_effect_capability(type);
        REQUIRE(row != nullptr);
        CHECK(row->route == GpuEffectRoute::NativeZeroReadback);
    }
    for (const auto type : {EffectType::Fake3DWave, EffectType::RadialBlur,
                            EffectType::Stroke, EffectType::FractalNoise,
                            EffectType::FourColorGradient, EffectType::LightRays,
                            EffectType::Lut3D, EffectType::Curves,
                            EffectType::CornerPin, EffectType::Twirl,
                            EffectType::Bulge, EffectType::MeshWarp,
                            EffectType::TurbulentDisplace, EffectType::WaveWarp,
                            EffectType::Ripple, EffectType::DisplacementMap,
                            EffectType::Halftone, EffectType::LensDistortion,
                            EffectType::LuminanceResponse, EffectType::FoldWarp,
                            EffectType::CreaseShading, EffectType::RoughenEdges}) {
        const auto* row = graph::gpu_effect_capability(type);
        REQUIRE(row != nullptr);
        CHECK(row->route == GpuEffectRoute::NativeZeroReadback);
    }

    CHECK(graph::gpu_effect_capability(EffectType::Unknown) == nullptr);

    for (const auto type : {EffectType::LinearWipe, EffectType::MinMax,
                            EffectType::StretchWarp, EffectType::LensFlare}) {
        const auto* row = graph::gpu_effect_capability(type);
        REQUIRE(row != nullptr);
        CHECK(row->route == GpuEffectRoute::NativeZeroReadback);
        CHECK_FALSE(row->diagnostic.empty());
    }
}

TEST_CASE("AE effect graph hashes include every authored scalar parameter") {
    using namespace effects;
    for (const auto id : {ids::TransitionLinearWipe, ids::StylizeMinMax,
                          ids::DistortStretchWarp, ids::LightLensFlare}) {
        const auto defaults=make_default_animatable_effect_params(id);
        REQUIRE(defaults);
        const auto hash=[](const EffectParams& params) {
            EffectStack stack;
            std::visit([&](const auto& p) {
                using T=std::decay_t<decltype(p)>;
                if constexpr (!std::is_same_v<T,std::monostate>) stack.emplace_back(p);
            },params);
            return graph::hash_effect_stack(stack);
        };
        for (const auto& row : kAnimatableEffectParams) {
            if (row.effect_id != id) continue;
            auto low=*defaults, high=*defaults;
            REQUIRE(set_animatable_effect_param(low,id,row.param,row.minimum));
            REQUIRE(set_animatable_effect_param(high,id,row.param,row.maximum));
            CHECK(hash(low) != hash(high));
            CHECK(hash(low) == hash(low));
        }
    }
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
    std::set<effects::EffectType> unsupported;
    for (const auto& row : graph::kGpuEffectCapabilityRows)
        if (row.route == graph::GpuEffectRoute::NativeZeroReadback) ++native;
    CHECK(native == graph::gpu_capability_detail::kNativeRoutes.size());
}
