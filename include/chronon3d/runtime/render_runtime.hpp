#pragma once

// ---------------------------------------------------------------------------
// runtime/render_runtime.hpp
//
// TICKET-011a — declare `chronon3d::runtime::RenderRuntime`, the
// engine-lifetime owner of long-lived render infrastructure.  This
// header is INTENTIONALLY HEADER-ONLY in this commit:
//
//   - No new behaviour is introduced (no .cpp yet).
//   - No existing consumer is touched.
//   - All pointer-bearing fields default-initialised to nullptr so
//     downstream consumers of `RenderRuntime&` can be wired in
//     sub-commits 011b–011e (BackendExecutionContext integration,
//     ShapeDrawContext migration, TextRasterService, draw_node /
//     draw_text_run migration).
//
// What this replaces (to be wired in subsequent sub-commits):
//   - `chronon3d::RendererRuntimeResources` (currently owned by
//     SoftwareRenderer; will fold into RenderRuntime in 011b).
//   - `chronon3d::RendererCacheState` (currently owned by
//     SoftwareRenderer; will fold into RenderRuntime in 011b).
//   - The std::unique_ptr<SoftwareBackend> m_backend held inside
//     SoftwareRenderer (will move under RenderRuntime in 011e).
//
// ── Lifetime model (TICKET-011 design) ──────────────────────────────────
//
//   Engine lifetime           ─┐
//     RenderRuntime           │  ← this file
//       ├── PipelineCatalogs   │  ← was inside SoftwareRenderer
//       ├── AssetRegistry*     │  ← from m_image_renderer + media/frame_source_provider
//       ├── NodeCache          │  ← was inside RendererCacheState
//       ├── CompiledGraphCache │  ← was inside RendererCacheState
//       ├── FramebufferPool    │  ← was inside RendererCacheState
//       ├── ExecutionScheduler │  ← new slot (was render_pipeline glue)
//       ├── MediaFrameProvider*│  ← was in SoftwareRenderer::m_video_decoder
//       ├── ExecutionPlanCache │  ← was m_runtime_resources.plan_cache
//       └── unique_ptr<RenderBackend>
//                              ─┘
//
// ── Migration stages ────────────────────────────────────────────────────
//   - 011a (this): declare RenderRuntime + RenderServices (engine) — pure
//     surface; no behaviour.
//   - 011b:   introduce SoftwareBackendServices; SoftwareBackend ctor
//             accepts services.  Adapter populates from RenderRuntime.
//   - 011c:   introduce ShapeDrawContext; ShapeProcessor::draw() migrated
//             to consume it (closes the service-locator on
//             SoftwareRenderer&).
//   - 011d:   introduce TextRasterService; replaces BOTH static anon
//             BLFontFace caches in text_run_processor.cpp + text_rasterizer_render.cpp.
//   - 011e:   migrate SoftwareRenderer::draw_node + ::draw_text_run to
//             SoftwareBackend; SoftwareRenderer stops inheriting from
//             RenderBackend (closes dual backend identity).  The 8 callers
//             that dynamic_cast<SoftwareRenderer*> today become plain
//             polymorphic dispatch via `ctx.services.backend`.
// ---------------------------------------------------------------------------

#include <cassert>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>
#include <chronon3d/runtime/execution_plan_cache.hpp>

#include <memory>

namespace chronon3d {
    struct Config;
    struct RenderSettings;
    class DebugConfig;
}

namespace chronon3d::assets {
    class AssetRegistry;
}

namespace chronon3d::cache {
    class NodeCache;
    class FramebufferPool;
    class CompiledGraphCache;
}

namespace chronon3d::media {
    class MediaFrameProvider;
}

namespace chronon3d::graph {
    class RenderBackend;
    class ExecutionScheduler;
}

namespace chronon3d::runtime {

/// RenderServices — flat pointer bundle owned by RenderRuntime for
/// the engine lifetime.  Sessions and per-call contexts reach into it
/// via `RenderRuntime::services()` (typed) rather than accessing the
/// SoftwareRenderer directly.  Pointers are non-owning; lifetime is
/// the runtime's responsibility.
///
/// NOTE — distinct from `chronon3d::graph::RenderServices` introduced
/// by TICKET-010 (which is the per-`RenderGraphContext` service
/// locator; per-frame rather than per-engine).  Renamed suggestion
/// raised by the TICKET-011a review will land in 011b alongside the
/// SoftwareBackendServices wiring.
struct RenderServices {
    chronon3d::cache::NodeCache*           node_cache{nullptr};
    chronon3d::cache::FramebufferPool*     framebuffer_pool{nullptr};
    chronon3d::graph::CompiledGraphCache*  graph_cache{nullptr};
    chronon3d::assets::AssetRegistry*      asset_registry{nullptr};
    chronon3d::media::MediaFrameProvider*  video_decoder{nullptr};
    chronon3d::graph::ExecutionScheduler*  scheduler{nullptr};
};

/// RenderRuntime — engine-lifetime container.  Owns pipeline catalogs +
/// asset/media/scheduler services + caches + framebuffer pool +
/// execution plan cache + the unique RenderBackend implementation.
///
/// Constructed once at engine init.  Every RenderSession holds a
/// `RenderRuntime&` for the duration of every frame; the lifetime
/// invariant is "RenderRuntime outlives any session that references it".
///
/// IMPORTANT — TICKET-011a NOTE: the inline accessors on lazy-initialised
/// members (`backend()`, `plan_cache()`) assert on null dereference.
/// This is intentional and SAFE in 011a: nothing instantiates
/// `RenderRuntime` yet, so the asserts are dormant.  Sub-commit 011b
/// will populate the unique_ptr members on construction so the asserts
/// succeed at the first access.
class RenderRuntime {
public:
    // ── Construction ─────────────────────────────────────────────────
    RenderRuntime() = default;

    explicit RenderRuntime(chronon3d::Config config)
        : m_config(std::move(config)) {}

    ~RenderRuntime() = default;

    // Non-copyable, movable.  All members are movable types so default
    // special members suffice; explicit declarations here document the
    // intent (no copy; move OK) without changing semantics.
    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;
    RenderRuntime(RenderRuntime&&) noexcept = default;
    RenderRuntime& operator=(RenderRuntime&&) noexcept = default;

    // ── Configuration ────────────────────────────────────────────────
    [[nodiscard]] const chronon3d::Config& config() const noexcept { return m_config; }

    // ── Backend access (will be populated in TICKET-011b by the
    //    backend factory; in 011a the pointer stays nullptr because
    //    no factory is wired yet, hence the `assert`.) ─────────────
    [[nodiscard]] chronon3d::graph::RenderBackend& backend() noexcept {
        assert(m_backend != nullptr &&
               "RenderRuntime::backend() requires m_backend to be populated "
               "(populated by sub-commit 011b's backend factory).");
        return *m_backend;
    }
    [[nodiscard]] const chronon3d::graph::RenderBackend& backend() const noexcept {
        assert(m_backend != nullptr &&
               "RenderRuntime::backend() requires m_backend to be populated "
               "(populated by sub-commit 011b's backend factory).");
        return *m_backend;
    }

    // ── Pipeline catalogs (read-only after `populate_builtin_pipeline_catalogs` /
    //    `init_graph_pipeline_catalogs` freeze) ───────────────────────
    [[nodiscard]] chronon3d::graph::PipelineCatalogs& catalogs() noexcept { return m_catalogs; }
    [[nodiscard]] const chronon3d::graph::PipelineCatalogs& catalogs() const noexcept { return m_catalogs; }

    // ── Service-locator bundle (engine-lifetime view; populated
    //    lazily during engine-init; mutable to allow lazy populate) ──
    [[nodiscard]] RenderServices& services() noexcept { return m_services; }
    [[nodiscard]] const RenderServices& services() const noexcept { return m_services; }

    // ── Topological-plan cache (allocated lazily in 011b; assertion
    //    protects 011a's intentionally unpopulated state) ────────────
    [[nodiscard]] chronon3d::runtime::ExecutionPlanCache& plan_cache() noexcept {
        assert(m_plan_cache != nullptr &&
               "RenderRuntime::plan_cache() requires m_plan_cache to be "
               "populated (allocated by sub-commit 011b).");
        return *m_plan_cache;
    }
    [[nodiscard]] const chronon3d::runtime::ExecutionPlanCache& plan_cache() const noexcept {
        assert(m_plan_cache != nullptr &&
               "RenderRuntime::plan_cache() requires m_plan_cache to be "
               "populated (allocated by sub-commit 011b).");
        return *m_plan_cache;
    }

private:
    chronon3d::Config                       m_config;
    chronon3d::graph::PipelineCatalogs      m_catalogs;
    RenderServices                          m_services;
    std::unique_ptr<chronon3d::runtime::ExecutionPlanCache> m_plan_cache;
    std::unique_ptr<chronon3d::graph::RenderBackend>       m_backend;
};

} // namespace chronon3d::runtime
