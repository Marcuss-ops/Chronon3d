#pragma once

// ============================================================================
// include/chronon3d/render/render_diagnostic.hpp
//
// §10.1 / PR 6.8 — Structured render-graph diagnostic surface.
//
// Replaces the silent-degrade behaviour of the legacy "return empty
// framebuffer on every error" path with a typed, stage-tagged diagnostic
// aggregate.  One `DiagnosticsSink` (§10.2) collects the diagnostics from
// all rendering sites; the `RenderFailurePolicy` boundary (§10.5) decides
// whether each `RenderError` surfaces as a hard failure or is folded into
// `RenderOutput::diagnostics` alongside a transparent fallback framebuffer.
//
// This header is intentionally header-only.  All types are POD-ish
// aggregates — no .cpp pairing needed.
//
// Architectural placement
// -----------------------
//   INTERNAL  (this header)  : `chronon3d::render::RenderError` /
//                              `chronon3d::render::RenderOutput` / etc.
//                              Namespace `chronon3d::render` keeps the
//                              internal-telemetry surface out of the SDK
//                              namespace.  The §10.5 boundary policy
//                              translates these to the OPP-side SDK
//                              surface (`chronon3d::sdk::RenderError`
//                              partner-WIP + `chronon3d::sdk::RenderOutput`).
//   EXTERNAL  (partner WIP)  : `chronon3d::sdk::RenderError` /
//                              `chronon3d::sdk::RenderOutput` in
//                              `include/chronon3d/sdk/render_*.hpp`
//                              (still untracked on main @ §9.2 = 1aea6591;
//                              bridge to internal types lands in §10.5).
//
// Field-type mapping (user §10.1 spec ↔ canonical surface)
// --------------------------------------------------------
//   `Frame`                    ↔ `chronon3d::Frame` (i64 strong type)
//   `GraphInstanceId`          ↔ `chronon3d::graph::GraphInstanceId`
//   `NodeInstanceId` (spec)    ↔ `chronon3d::graph::StableNodeId`
//                                (canonical per-node id; spec name is a
//                                 semantic alias.  Per AGENTS.md freeze
//                                 rules we do NOT introduce a new type
//                                 for an existing concept.)
//   `Framebuffer`              ↔ `chronon3d::Framebuffer` (canonical)
//   `RenderStats`              ↔ defined IN-PLACE below (no canonical
//                                exists on main yet; minimal POD
//                                surface, extendable in §10.6 without
//                                breaking other consumers).
//
// PR 6.8 split-out — only the typed diagnostic types land here.  The
// `DiagnosticsSink` collector (§10.2), the `RenderGraphContext` wiring
// (§10.3), the `PrecompNode` reporting rewrite (§10.4), the boundary
// policy (§10.5), and the test suite (§10.6) are separate atomic
// commits on `main` that depend on this file.
// ============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::render {

// ── §10.1 — Severity / code / stage enums ──────────────────────────────

/// Diagnostic severity.  Discriminant order is the stable ABI
/// contract; do NOT reorder existing entries (add new ones at the tail).
enum class RenderDiagnosticSeverity : std::uint8_t {
    Info    = 0,   ///< Informational; render still proceeds normally.
    Warning = 1,   ///< Soft problem; engine continues with a fallback.
    Error   = 2,   ///< Hard problem at the per-node stage; root cause
                   ///<   may or may not be fatal depending on policy.
    Fatal   = 3,   ///< Pipeline-level unrecoverable; render cannot
                   ///<   produce a coherent output.
};

/// Categorical failure code reported by `RenderError` and each
/// `RenderDiagnostic` it aggregates.  Discriminant order is the stable
/// ABI contract; do NOT reorder existing entries (add new ones at the
/// tail).
///
/// Coverage (matches user §10.1 spec verbatim):
///   - `MissingRegistry`        — composition registry not wired into ctx
///   - `UnknownComposition`      — composition name not found in registry
///   - `MissingExecutor`         — GraphExecutor pointer is null
///   - `MissingScheduler`        — ExecutionScheduler pointer is null
///   - `MissingFontEngine`       — font engine not wired (TextRun path)
///   - `ScopeChainLimit`         — `make_child` rejected on chain length
///   - `RecursivePrecomp`        — precomp loop detected (direct/indirect)
///   - `ProgramCompileFailure`    — SceneProgramStore build miss + fail
///   - `InvalidCameraProgram`    — CameraProgram compile failure
///   - `AssetResolutionFailure`   — assets_root / resolver miss
enum class RenderErrorCode : std::uint8_t {
    MissingRegistry         = 0,
    UnknownComposition       = 1,
    MissingExecutor          = 2,
    MissingScheduler         = 3,
    MissingFontEngine        = 4,
    ScopeChainLimit          = 5,
    RecursivePrecomp         = 6,
    ProgramCompileFailure    = 7,
    InvalidCameraProgram     = 8,
    AssetResolutionFailure   = 9,
};

/// Pipeline stage at which the diagnostic originated.  Discriminant
/// order is the stable ABI contract; do NOT reorder existing entries
/// (add new ones at the tail).
enum class RenderStage : std::uint8_t {
    CompositionCompile = 0,
    CompositionEvaluate = 1,
    GraphCompile       = 2,
    NodeExecute        = 3,
    PrecompExecute     = 4,
    BackendRender      = 5,
    OutputEncode       = 6,
};

// ── §10.1 — Stats aggregate (defined in-place) ──────────────────────────
//
// No canonical `RenderStats` exists on main yet.  Minimal POD surface —
// extendable additively in §10.6 / future PRs without breaking consumers
// (every field has a default member initialiser; new fields can be added
// at the tail without disturbing existing layout).
struct RenderStats {
    std::uint64_t nodes_visited{0};        ///< Total nodes scheduled in this frame.
    std::uint64_t nodes_executed{0};       ///< Nodes that actually ran (not skipped).
    std::uint64_t precomp_hits{0};         ///< SceneProgramStore cache hits.
    std::uint64_t precomp_misses{0};       ///< SceneProgramStore cache misses.
    std::uint64_t dirty_pixels{0};         ///< Pixels re-rendered (dirty-rect path).
    double        elapsed_milliseconds{0.0}; ///< Wall-clock render duration.
};

// ── §10.1 — Diagnostic / Error / Output aggregates ──────────────────────

/// One typed diagnostic entry.  The `code` field identifies which
/// failure path triggered; the surrounding fields provide the context
/// needed to localise it in a (graph, node, frame) coordinate space.
///
/// Stays an aggregate (default member initialisers present) so call
/// sites that use designated-init can populate exactly the fields they
/// have; the rest stay at their zero defaults.
struct RenderDiagnostic {
    RenderErrorCode          code{RenderErrorCode::MissingRegistry};
    RenderDiagnosticSeverity severity{RenderDiagnosticSeverity::Info};
    RenderStage              stage{RenderStage::CompositionCompile};

    chronon3d::Frame                       frame{0};
    chronon3d::graph::GraphInstanceId      graph{chronon3d::graph::kInvalidGraphInstanceId};
    /// Maps to user-spec `NodeInstanceId`; the canonical per-node id
    /// in the codebase is `StableNodeId` (see header doc above).
    chronon3d::graph::StableNodeId         node{chronon3d::graph::kInvalidStableNodeId};

    std::string composition_id;   ///< Empty when not PrecompExecute stage.
    std::string message;          ///< Free-form, never empty in practice.
};

/// Aggregate error report.  Carries the categorical `code` + `stage`
/// + `frame` of the primary failure, plus a free-form `message` and a
/// vector of per-context `RenderDiagnostic`s (one per node / stage
/// that observed the failure).
struct RenderError {
    RenderErrorCode code{RenderErrorCode::MissingRegistry};
    RenderStage     stage{RenderStage::CompositionCompile};
    chronon3d::Frame frame{0};
    std::string     message;
    std::vector<RenderDiagnostic> diagnostics;
};

/// Render output payload.  Always non-null `framebuffer` even in
/// fallback paths (the `RenderFailurePolicy::ContinueWithTransparentFallback`
/// boundary produces a transparent framebuffer when the engine cannot
/// render coherently — never a null).  `diagnostics` carries the
/// warning/error trail; `framebuffer` carries the produced pixels.
struct RenderOutput {
    std::shared_ptr<const chronon3d::Framebuffer> framebuffer;
    RenderStats                                  stats;
    std::vector<RenderDiagnostic>                diagnostics;
};

// ── §10.1 — Stable-name helpers ────────────────────────────────────────
//
// Mirrors the established project pattern (cf. `execution_scope_kind_name`
// in `<chronon3d/core/scope/execution_scope.hpp>` and
// `scope_error_code_name` in `<chronon3d/core/execution/scope_error.hpp>`):
// for logging / diagnostics only, never for equality.  Enum storage
// type may evolve, order is the contract.

inline const char* render_diagnostic_severity_name(RenderDiagnosticSeverity s) noexcept {
    switch (s) {
        case RenderDiagnosticSeverity::Info:    return "Info";
        case RenderDiagnosticSeverity::Warning: return "Warning";
        case RenderDiagnosticSeverity::Error:   return "Error";
        case RenderDiagnosticSeverity::Fatal:   return "Fatal";
    }
    return "Unknown";
}

inline const char* render_error_code_name(RenderErrorCode c) noexcept {
    switch (c) {
        case RenderErrorCode::MissingRegistry:       return "MissingRegistry";
        case RenderErrorCode::UnknownComposition:    return "UnknownComposition";
        case RenderErrorCode::MissingExecutor:       return "MissingExecutor";
        case RenderErrorCode::MissingScheduler:      return "MissingScheduler";
        case RenderErrorCode::MissingFontEngine:     return "MissingFontEngine";
        case RenderErrorCode::ScopeChainLimit:       return "ScopeChainLimit";
        case RenderErrorCode::RecursivePrecomp:      return "RecursivePrecomp";
        case RenderErrorCode::ProgramCompileFailure: return "ProgramCompileFailure";
        case RenderErrorCode::InvalidCameraProgram:  return "InvalidCameraProgram";
        case RenderErrorCode::AssetResolutionFailure:return "AssetResolutionFailure";
    }
    return "Unknown";
}

inline const char* render_stage_name(RenderStage s) noexcept {
    switch (s) {
        case RenderStage::CompositionCompile:  return "CompositionCompile";
        case RenderStage::CompositionEvaluate: return "CompositionEvaluate";
        case RenderStage::GraphCompile:        return "GraphCompile";
        case RenderStage::NodeExecute:         return "NodeExecute";
        case RenderStage::PrecompExecute:      return "PrecompExecute";
        case RenderStage::BackendRender:       return "BackendRender";
        case RenderStage::OutputEncode:        return "OutputEncode";
    }
    return "Unknown";
}

} // namespace chronon3d::render
