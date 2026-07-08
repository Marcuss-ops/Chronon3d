// ===========================================================================
// src/backends/software/software_renderer_effects.cpp
//
// M1.5#12 4/4 — SoftwareRenderer effect-stack dispatcher.
//
// Single source of truth for the renderer's effect-stack forwarder.
// `apply_blur`, `apply_per_pixel_dof` and `composite_layer` are
// export-Q-equivalent 1-liners that live in `software_renderer_dispatch.cpp`
// (they deal with image-framebuffer-pixel-level operations rather than
// effect-stack composition).  `apply_effect_stack` is the canonical
// effect-stack fan-out and deserves its own TU for semantic clarity.
//
// All state lives on `m_runtime->backend()` (06 R3b single-identity
// orchestrator invariant).  No new public API surface.
// ===========================================================================

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <optional>

namespace chronon3d {

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb,
                                          const EffectStack& stack,
                                          const effects::EffectExecutionContext& context) {
    m_runtime->backend().apply_effect_stack(fb, stack, context);
}

} // namespace chronon3d
