#pragma once

// ---------------------------------------------------------------------------
// postprocess_fx.hpp — Software kernels for the post-process FX family
// (Scanlines, RetroTv, Halation, AnamorphicStreak).
//
// Shader-math contracts live in docs/tickets/TICKET-POSTPROCESS-FX.md. Every
// kernel is deterministic: time-dependent behaviour (RetroTv tape jitter) is a
// pure function of (time_seconds, seed) so identical inputs reproduce
// identical frames.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>

namespace chronon3d::renderer {

// ── Scanlines — per-pixel luminance modulation along one axis ───────────────
void apply_scanlines(Framebuffer& fb, const ScanlinesParams& params,
                     const std::optional<raster::BBox>& clip = std::nullopt);

// ── Retro TV — tape jitter + chroma smear + luma interference ───────────────
void apply_retro_tv(Framebuffer& fb, const RetroTvParams& params, float time_seconds,
                    ::chronon3d::EffectScratchResources& scratch,
                    const std::optional<raster::BBox>& clip = std::nullopt);

// ── Film halation — warm highlight bloom, screen composite ──────────────────
void apply_halation(Framebuffer& fb, const HalationParams& params,
                    ::chronon3d::EffectScratchResources& scratch,
                    const std::optional<raster::BBox>& clip = std::nullopt);

// ── Anamorphic streak — horizontal highlight streak, additive composite ─────
void apply_anamorphic_streak(Framebuffer& fb, const AnamorphicStreakParams& params,
                             ::chronon3d::EffectScratchResources& scratch,
                             const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
