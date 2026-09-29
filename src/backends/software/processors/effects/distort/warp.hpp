#pragma once

// ---------------------------------------------------------------------------
// warp.hpp — Declarations for the warp family (CornerPin, Twirl, Bulge,
// MeshWarp).  All four resample in place: every destination pixel samples the
// pre-warp source through an inverse map, using the canonical Sampler2D with
// the declared edge mode.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <optional>

namespace chronon3d::renderer {

// ── Corner Pin — inverse bilinear map of the source onto an arbitrary quad ──
void apply_corner_pin(Framebuffer& fb, const CornerPinParams& params,
                      ::chronon3d::EffectScratchResources& scratch,
                      const std::optional<raster::BBox>& clip = std::nullopt);

// ── Twirl — radial rotation with falloff around a center ────────────────────
void apply_twirl(Framebuffer& fb, const TwirlParams& params,
                 ::chronon3d::EffectScratchResources& scratch,
                 const std::optional<raster::BBox>& clip = std::nullopt);

// ── Bulge / Pinch — radial magnification around a center ────────────────────
void apply_bulge(Framebuffer& fb, const BulgeParams& params,
                 ::chronon3d::EffectScratchResources& scratch,
                 const std::optional<raster::BBox>& clip = std::nullopt);

// ── Mesh Warp — bilinear control-grid displacement ──────────────────────────
void apply_mesh_warp(Framebuffer& fb, const MeshWarpParams& params,
                     ::chronon3d::EffectScratchResources& scratch,
                     const std::optional<raster::BBox>& clip = std::nullopt);
void apply_turbulent_displace(Framebuffer& fb, const TurbulentDisplaceParams& params, float time_seconds,
                              ::chronon3d::EffectScratchResources& scratch,
                              const std::optional<raster::BBox>& clip = std::nullopt);
void apply_wave_warp(Framebuffer& fb, const WaveWarpParams& params, float time_seconds,
                     ::chronon3d::EffectScratchResources& scratch,
                     const std::optional<raster::BBox>& clip = std::nullopt);
void apply_ripple(Framebuffer& fb, const RippleParams& params, float time_seconds,
                  ::chronon3d::EffectScratchResources& scratch,
                  const std::optional<raster::BBox>& clip = std::nullopt);
void apply_displacement_map(Framebuffer& fb, const DisplacementMapParams& params,
                            const Framebuffer& map,
                            ::chronon3d::EffectScratchResources& scratch,
                            const std::optional<raster::BBox>& clip = std::nullopt);
void apply_displacement_field(Framebuffer& fb, const DisplacementMapParams& params,
                              const graphics::Field2D& field,
                              ::chronon3d::EffectScratchResources& scratch,
                              const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
