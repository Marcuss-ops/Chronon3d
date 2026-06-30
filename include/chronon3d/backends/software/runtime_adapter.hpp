#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// backends/software/runtime_adapter.hpp
//
// Fase 4 — adapter module that holds software-specific functions previously
// living in `runtime/render_runtime.hpp`.  Keeps the core runtime free of
// software-backend dependencies.
//
// M4 init-order closure: `attach_software_backend(renderer)` is the canonical
// attach call for any caller that constructs a `SoftwareRenderer` outside of
// `cli::create_renderer`.  Centralising the helper here means the attach
// sequence (signature collection → factory → runtime.attach_backend) lives
// in one place.  Once all CLI construction paths route through this helper,
// the `cli::create_renderer` inline duplicate can be replaced with a single
// call here.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/backends/software/software_render_session.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::runtime { class RenderRuntime; }

namespace chronon3d::backends::software {

/// Vends a fresh per-render-job `SoftwareRenderSession` whose
/// `common.services` field is populated with the runtime's catalogue
/// back-pointers.  Lifetime invariant: `runtime` must outlive every
/// session it vends.
[[nodiscard]] chronon3d::SoftwareRenderSession make_session(
    chronon3d::runtime::RenderRuntime& runtime
);

/// Read the back-pointers currently bound to a session.  Returns an
/// empty record if the session was not produced by `make_session()`.
[[nodiscard]] const chronon3d::runtime::SessionServices& session_services(
    const chronon3d::SoftwareRenderSession& session
);

/// Register builtin shape processors into the software registry.
/// Called once during software-renderer construction.
void register_builtin_processors(chronon3d::renderer::SoftwareRegistry& reg);

/// M4 init-order closure: attach a `SoftwareBackend` to the renderer's
/// runtime.  Idempotent — a no-op when the runtime already has a backend.
/// Gathers the SAME service bundle `cli_render_utils::create_renderer`
/// builds (counters, settings, framebuffer_pool, asset_resolver, text
/// resources, image renderer, owner pointer), invokes the factory, and
/// hands the resulting unique_ptr to `runtime.attach_backend(...)`.
///
/// Call this AFTER per-instance wiring (image_backend / settings /
/// composition_registry) so the backend's service pointers are stable
/// for the rest of the session.  Constructing the backend from inside
/// the renderer ctor would be unsafe: the surrounding
/// `shared_ptr<SoftwareRenderer>` can be moved before these refs are read.
void attach_software_backend(chronon3d::SoftwareRenderer& renderer);

} // namespace chronon3d::backends::software
