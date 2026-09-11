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

#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <memory>

namespace chronon3d::runtime { class RuntimeContext; }

namespace chronon3d::backends::software {

/// Register builtin shape processors into the software registry.
/// Called once during software-renderer construction.
void register_builtin_processors(chronon3d::renderer::SoftwareRegistry& reg);

/// Build the initial explicit dependency view used to construct a renderer.
/// The returned view borrows engine-lifetime services; it owns nothing.
[[nodiscard]] SoftwareExecutionContext
make_software_renderer_context(chronon3d::runtime::RuntimeContext& runtime);

/// Resolve the five explicit software execution domains once for a renderer
/// job. The returned view is installed after backend attachment and is the
/// dependency boundary consumed by RenderPipeline.
[[nodiscard]] SoftwareExecutionContext
make_software_execution_context(
    chronon3d::SoftwareRenderer* renderer,
    chronon3d::runtime::RuntimeContext& runtime);

/// Construct a renderer without exposing RuntimeContext in the renderer API.
[[nodiscard]] std::unique_ptr<chronon3d::SoftwareRenderer>
make_software_renderer(chronon3d::runtime::RuntimeContext& runtime,
                        chronon3d::Config config);

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
void attach_software_backend(chronon3d::SoftwareRenderer* renderer,
                             chronon3d::runtime::RuntimeContext& runtime);

/// Resolve and attach the requested backend. `GPU` is strict: it fails while
/// Vulkan is not registered rather than silently selecting Software.
void attach_software_backend(
    chronon3d::SoftwareRenderer* renderer,
    chronon3d::runtime::RuntimeContext& runtime,
    chronon3d::graph::BackendPreference preference);

} // namespace chronon3d::backends::software
