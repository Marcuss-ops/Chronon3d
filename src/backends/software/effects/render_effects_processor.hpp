#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/layer/layer_effect.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/internal/render_graph/processor_registry_snapshot.hpp>
#include <optional>
#include <span>

namespace chronon3d {
namespace renderer {

class SoftwareRegistry;
class EffectProcessor;

void apply_effect_stack(
    Framebuffer& fb,
    const EffectStack& stack,
    const effects::EffectExecutionContext& context,
    std::span<const EffectProcessorHandle> processors,
    std::shared_ptr<const ProcessorRegistrySnapshot> snapshot);

// Legacy direct-dispatch entry point retained for standalone effect tests.
// Compiled render-graph execution uses the overload above and never reaches
// the registry lookup path.
void apply_effect_stack(
    Framebuffer& fb,
    const EffectStack& stack,
    const effects::EffectExecutionContext& context,
    SoftwareRegistry& registry);

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip = std::nullopt, int passes = 2);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_fake_3d_wave(Framebuffer& fb, const Fake3DWaveParams& params,
                        float time_seconds,
                        const std::optional<raster::BBox>& clip = std::nullopt);

// ── Individual effect implementations (extracted from effect_stack.cpp) ──
//
// TICKET-007 — glow debug artifacts receive a per-instance `DebugConfig*`
// from `EffectExecutionContext::debug_cfg`; nullptr keeps them disabled.
// This replaces the removed process-wide `detail::g_debug_config`.
void apply_shadow_effect(Framebuffer& fb, const DropShadowParams& p, const std::optional<raster::BBox>& clip, bool diagnostics_enabled = false);

// ── Adjustment-layer color correction (AE-5) ──
void apply_saturation(Framebuffer& fb, f32 sat, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_hue_rotate(Framebuffer& fb, f32 degrees, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_invert(Framebuffer& fb, f32 amount, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_vignette(Framebuffer& fb, f32 radius, f32 softness, f32 amount, Color color, const std::optional<raster::BBox>& clip = std::nullopt);


} // namespace renderer
} // namespace chronon3d
