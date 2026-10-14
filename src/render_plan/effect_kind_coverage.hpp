#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <magic_enum/magic_enum.hpp>

#include <array>

namespace chronon3d::render_plan::detail {

using EffectKind = LayerPlan::EffectKindPlan;

inline constexpr std::array kEffectKindCoverage{
    EffectKind::Lut3D,
    EffectKind::Echo,
    EffectKind::Keying,
    EffectKind::Threshold,
    EffectKind::Mosaic,
    EffectKind::GaussianBlur,
    EffectKind::DropShadow,
    EffectKind::Bloom,
    EffectKind::Glow,
    EffectKind::Noise,
    EffectKind::FractalNoise,
    EffectKind::CornerPin,
    EffectKind::Twirl,
    EffectKind::Bulge,
    EffectKind::MeshWarp,
    EffectKind::Sharpen,
    EffectKind::RoughenEdges,
    EffectKind::TurbulentDisplace,
    EffectKind::WaveWarp,
    EffectKind::Ripple,
    EffectKind::DisplacementMap,
    EffectKind::LightRays,
    EffectKind::ChromaticAberration,
    EffectKind::Halftone,
    EffectKind::DirectionalBlur,
    EffectKind::RadialBlur,
    EffectKind::Vignette,
    EffectKind::Catalog,
    EffectKind::LensDistortion,
    EffectKind::LuminanceResponse,
    EffectKind::FoldWarp,
    EffectKind::CreaseShading,
    EffectKind::NoiseSampler,
    EffectKind::BandedGrain,
};

static_assert(kEffectKindCoverage.size() == magic_enum::enum_count<EffectKind>(),
              "Update effect kind coverage when EffectKindPlan changes");

}  // namespace chronon3d::render_plan::detail
