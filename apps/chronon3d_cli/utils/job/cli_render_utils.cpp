#include "cli_render_utils.hpp"
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>  // Fase A2 — attach_software_backend factory
#include <chronon3d/runtime/render_runtime.hpp>

#include "../common/render_error_formatter.hpp"

// Audit §10 — `<filesystem>` include was REMOVED (the unconditional
// `cwd = current_path()` mount line that consumed `std::filesystem`
// was deleted).  If a future caller re-introduces an EXPLICIT
// `--assets-root` CLI flag wiring, restore the include here.
#include <cassert>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id) {
    return resolve_composition(registry, comp_id, CompositionProps{});
}

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id,
                                         const CompositionProps& props) {
    ResolvedComposition result;

    if (!registry.contains(comp_id)) {
        print_render_error(
            graph::NodeExecutionError{
                graph::RenderBackendErrorCode::InvalidInput,
                "composition_registry",
                "unknown composition '" + comp_id + "'"
            },
            comp_id);
        return result;
    }

    try {
        auto comp_instance = registry.create(comp_id, props);
        result.comp = std::make_shared<Composition>(std::move(comp_instance));
    } catch (const std::exception& e) {
        print_render_error(
            graph::NodeExecutionError{
                graph::RenderBackendErrorCode::InvalidInput,
                "composition_registry",
                "could not create composition '" + comp_id + "': " + e.what()
            },
            comp_id);
    }
    return result;
}

std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings,
    std::optional<Config> config) {
    auto renderer = config.has_value()
        ? std::make_shared<SoftwareRenderer>(std::move(*config))
        : std::make_shared<SoftwareRenderer>(Config::from_environment());
    renderer->set_composition_registry(&registry);
    renderer->set_settings(settings);

    // Inject default backends
    renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

    // FIX (M4 init-order bug): SoftwareRenderer::backend() dereferences
    // m_runtime->backend(), which throws `RenderRuntime::backend() called
    // before attach_backend()` if no SoftwareBackend has been attached to
    // the renderer's runtime.  The renderer's two ctors do NOT attach a
    // backend automatically (that step is documented in render_runtime.hpp
    // as the responsibility of RenderEngine::Impl, which the CLI doesn't
    // use — CLI goes straight through SoftwareRenderer + create_renderer).
    // Attach SoftwareBackend here AFTER all per-instance wiring
    // (image_backend / settings / registry) so the backend's service
    // pointers (counters, settings, framebuffer_pool, asset_resolver,
    // text_resources, owner) are stable and lifetime-invariant for the
    // rest of the session.  The SoftwareBackend keeps non-owning
    // references into the renderer; constructing it from inside the
    // renderer ctor would be unsafe because the shared_ptr<SoftwareRenderer>
    // further up this function could be moved before these refs were
    // read.
    assert(renderer != nullptr);
    if (!renderer->runtime().backend_attached()) {
        // Fase A2 — unified backend construction via the canonical
        // `attach_software_backend()` factory (runtime_adapter.hpp).
        // Replaces the previously-inlined services bundle +
        // make_software_backend + attach_processor_context duplicate.
        chronon3d::backends::software::attach_software_backend(renderer.get());
    }

    // FIX (video-pipeline SEGV): capture cwd once and mount it on both
    // asset services of the per-runtime.  Without this mount the
    // resolver side stays empty, AssetPreflightResolver::check falls
    // through "no FontEngine available" → SIGSEGV during the first
    // frame graph execution.  cli_render_utils.cpp is the SINGLE
    // chokepoint for renderer creation (used by both the working `render`
    // command and the crashing `video` command), so mounting here covers
    // every code path.  Capturing cwd into a local avoids the racy
    // global mutable CWD read across the two mount() calls.
    // Audit §10 — the previous `cwd = current_path()` unconditional
    // mount on `renderer->runtime().resolver()` was REMOVED.  That mount
    // was a process-wide asset root in disguise (every CLI invocation —
    // `render`, `video`, `bench_convert`, preflight — silently snapshotted
    // the working directory and made it the per-runtime resolver mount).
    // Composition tests under `cd /tmp/test-runner` would resolve
    // `assets/fonts/Inter.ttf` from PROJECT ROOT regardless of the
    // test's effective cwd — a silent cross-test drift surface.
    //
    // After removal: callers that need the resolver mounted must
    //   (a) construct `SoftwareRenderer` with an explicit `Config`
    //       whose `assets_root` field is populated — this routes
    //       through `RenderRuntime::create()`'s
    //       `runtime->resolver().mount(*cfg.assets_root)` canonical
    //       wiring (see src/runtime/render_runtime.cpp:85), OR
    //   (b) bridge through `sdk::RenderEngine` and call
    //       `engine.set_assets_root(path)` before invoking
    //       `create_renderer` — this routes through `RenderEngine::Impl`
    //       which calls `m_runtime.resolver().mount(root)`
    //       (see src/runtime/render_engine.cpp:95).
    // If neither (a) nor (b) is set, the resolver is un-mounted at
    // construction time and preflight surfaces the missing mount as
    // a hard FAIL — the desired behaviour per audit §10 ("fallback
    // CWD" delisting).  No silent drift.

    // P1-14 — the vestigial `populate()` call was REMOVED.  The canonical
    // `RenderRuntime::create()` factory auto-pops the runtime via
    // `populate()` (and the 1-arg `RenderRuntime(Config)` ctor does the
    // same in its body).  `populate()` is now PRIVATE on RenderRuntime;
    // this call site was vestigial (the runtime is already populated by
    // the time we reach this point in `create_renderer`).

    return renderer;
}

} // namespace cli
} // namespace chronon3d
