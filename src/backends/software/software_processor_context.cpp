// ============================================================================
// src/backends/software/software_processor_context.cpp
//
// 06 R2 — companion definition for `make_processor_context(SoftwareRenderer &)`
// declared in `software_processor_context.hpp`.
//
// Pulls the heavy `software_renderer.hpp` here (one .cpp), so the
// header itself stays cheap and forward-decl-only.
// ============================================================================

#include <chronon3d/backends/software/software_processor_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d {

SoftwareProcessorContext make_processor_context(SoftwareRenderer* renderer) {
    SoftwareProcessorContext ctx;
    ctx.counters        = renderer->counters();
    ctx.settings        = &renderer->render_settings();
    ctx.registry        = &renderer->software_registry();
    ctx.image_backend   = renderer->image_backend();
    ctx.image_renderer  = &renderer->image_renderer();
    ctx.debug_config    = &renderer->config().debug();
    ctx.asset_resolver  = &renderer->runtime().resolver();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    // font_engine() throws on non-text builds; callers must be in
    // a `#if CHRONON3D_HAS_BACKEND_TEXT` block before invoking.
    // We unconditionally set the pointer here and document the contract.
    // On non-text builds the unique_ptr is null which is benign — the
    // pointer remains null and downstream code-checks kick in.
    try {
        ctx.font_engine = &renderer.font_engine();
    } catch (const std::logic_error&) {
        ctx.font_engine = nullptr;
    }
#endif
    return ctx;
}

} // namespace chronon3d
