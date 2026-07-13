#include "cli_render_utils.hpp"
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>  // Fase A2 — attach_software_backend factory
#include <chronon3d/runtime/render_runtime.hpp>

#include <cassert>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id) {
    ResolvedComposition result;

    if (!registry.contains(comp_id)) {
        spdlog::error("Unknown composition: {}", comp_id);
        return result;
    }

    auto comp_instance = registry.create(comp_id);
    result.comp = std::make_shared<Composition>(std::move(comp_instance));
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
    const std::filesystem::path cwd = std::filesystem::current_path();

    // Mount 1/2 — typed AssetRegistry.  Picked up by callers that go
    // through `runtime().assets()` (mirrors `cli_asset_registry().mount()`
    // in render_job_setup.cpp at the CLI-wide global level).
    renderer->runtime().assets().mount(cwd);

    // Mount 2/2 — per-engine Resolver.  WP-8 PR 8.0 split the runtime
    // assets into TWO siblings: `assets()` (typed registry) and
    // `resolver()` (per-engine typed path resolver).  The FontEngine
    // is constructed in `SoftwareRenderer(Config{})` via
    // `make_unique<FontEngine>(m_runtime->resolver())`
    // (src/backends/software/software_renderer.cpp), so font lookup goes
    // through the resolver — NOT the registry.  Likewise
    // AssetPreflightResolver::check calls `resolver.resolve_lexical(ref.path)`
    // (include/chronon3d/assets/asset_preflight_resolver.hpp) and
    // SoftwareRenderer::preflight_fonts passes `m_runtime->resolver()` to
    // `TextRenderResources::resolve_handle(...)`.  Mounting assets()
    // alone is therefore insufficient — the resolver side stays empty
    // and produces the same SEGV.  Mount both siblings.
    renderer->runtime().resolver().mount(cwd);

    // FIX (correctness — populate idempotency): the canonical
    // `RenderRuntime::create()` factory auto-pops the runtime via
    // `populate()`, but the deprecated `SoftwareRenderer(Config{})` ctor
    // path may leave m_populated=false until first attach_backend.  The
    // header guarantees `populate()` idempotent ("calling populate() on
    // a populated runtime is a no-op"), so calling it here after
    // attach_software_backend() is safe and guards against any internal
    // ctor-ordering surprises.  Renamed from "safety" to "correctness":
    // this is in fact a correctness fix, not just defensive.
    renderer->runtime().populate();

    return renderer;
}

} // namespace cli
} // namespace chronon3d
