#pragma once

// ---------------------------------------------------------------------------
// fill_noise_offset.hpp — Declarations for scalar Fill, Noise, Offset
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/backends/software/sampling/edge_mode.hpp>
#include <optional>

namespace chronon3d::renderer {

// ── Fill ────────────────────────────────────────────────────────────────────

void apply_fill(Framebuffer& fb, const Color& color, float amount,
                bool replace_mode,
                const std::optional<raster::BBox>& clip = std::nullopt);

// ── Noise (deterministic, thread-safe) ──────────────────────────────────────

void apply_noise(Framebuffer& fb, float amount, uint32_t seed,
                 bool animated, bool rgb_mode, uint32_t frame = 0,
                 const std::optional<raster::BBox>& clip = std::nullopt);

// ── Offset ──────────────────────────────────────────────────────────────────

void apply_offset(Framebuffer& fb, float dx, float dy,
                  chronon3d::sampling::EdgeMode edge_mode,
                  chronon3d::sampling::SampleFilter filter,
                  const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
