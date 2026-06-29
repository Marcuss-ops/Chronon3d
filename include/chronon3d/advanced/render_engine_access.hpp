#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// include/chronon3d/advanced/render_engine_access.hpp
//
// P1-C — Legacy-accessor escape hatch for the SDK V0.1 migration.
// ═════════════════════════════════════════════════════════════════════════════
//
// CONTRACT (per plan §5):
//   - NOT STABLE                — method signatures may change between minor versions.
//   - NOT ABI-GUARANTEED        — the underlying types are NOT part of the SDK.
//   - NOT IN THE STANDARD V0.1 SDK — this header is internal and MUST NOT be
//     installed as part of the standard SDK package.  It exists solely to
//     keep legacy OPP-internal call sites operational during the
//     Pass B→D migration window.
//
// Why this header exists:
//   The public `chronon3d::RenderEngine::renderer()`,
//   `runtime()`, and `create_session()` accessors were removed in Pass C.
//   Their practical functionality — reaching into the engine's underlying
//   `runtime::RenderRuntime`, `SoftwareRenderer`, and per-job
//   `SoftwareRenderSession` — is exposed here under a clearly-named,
//   intentionally-internal namespace.
//
// Consumers:
//   ┌────────────────────────────────────────────────────────────────────┐
//   │ OPP-side code (CLI / benchmark / showcase) that legitimately      │
//   │ needs access to the underlying renderer/runtime for OPP testing    │
//   │ or introspection during the migration window.                      │
//   │                                                                    │
//   │ Application code MUST NOT include this header.  Use                │
//   │ `chronon3d::sdk::RenderEngine::render()` instead.                 │
//   └────────────────────────────────────────────────────────────────────┘
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::advanced {

/// OPP-side escape hatch.  Provides direct access to the legacy renderer,
/// runtime, and per-job session machinery during the Pass B→D migration.
/// Lives behind friend declarations on both `RenderEngine` (added in P1-B)
/// and `RenderEngine::Impl` (added in P1-C).
class RenderEngineAccess {
public:
    /// Returns the engine-owned `RenderRuntime` (cache owner, registries,
    /// scheduler, plan cache).  Lifetime: bound to `engine`.
    [[nodiscard]]
    static runtime::RenderRuntime& runtime(RenderEngine& engine) noexcept;

    /// Returns the engine-owned `SoftwareRenderer` by REFERENCE (vs the
    /// legacy `RenderEngine::renderer()` which returned a pointer).
    /// Pass C drops the nullability concern: the renderer is always
    /// present after construction.
    [[nodiscard]]
    static SoftwareRenderer& software_renderer(RenderEngine& engine) noexcept;

    /// Vends a fresh per-render-job `SoftwareRenderSession` referencing
    /// the engine-owned runtime.  Lifetime invariant: the engine outlives
    /// any session it vends; the session borrows from the runtime.
    [[nodiscard]]
    static chronon3d::SoftwareRenderSession create_session(RenderEngine& engine);
};

} // namespace chronon3d::advanced
