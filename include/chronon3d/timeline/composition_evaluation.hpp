#pragma once

// ============================================================================
// include/chronon3d/timeline/composition_evaluation.hpp
//
// §8.1 / PR 6.8 — CompositionEvaluationServices / Context PODs.
//
// Introduces the typed surface that `evaluate_composition(...)` (§8.2) will
// accept.  This header is PURELY ADDITIVE in §8.1: it declares two POD
// types and three forward-declared factory helpers, but introduces NO new
// code path, NO new caller, and NO change to the existing
// `Composition::evaluate(...)` overloads.  Subsequent §8.2-8.5 commits
// wire the new types into the root / precomp / tile call sites.
//
// Design goals (PR 6.8):
//   * `Services` is a PURE POD with default-initialised pointers — no
//     user-provided ctor, no invariants, no validation.  This keeps the
//     type header-only + trivially-copyable, fitting the established
//     POD-aggregate pattern used by `RenderGraphContext`, `FrameInput`,
//     `ExecutionScope`, and the other graph-stack types.
//   * `Context` embeds `Services` by value (composition over inheritance)
//     so `ctx.services.registry` reads naturally and matches the
//     `RenderGraphContext::services` precedent in TICKET-010.
//   * The two structs together form the STABLE DATA LAYOUT for
//     `ValidatedExecutionServices` (§8.4) and the `Result<Scene,
//     RenderError>` return of `evaluate_composition()` (§8.2).  Field
//     order, types, and pointer-vs-value choices are FROZEN at §8.1 —
//     any reordering here forces parallel reordering in §8.4 / §8.5.
//
// Stays in namespace `chronon3d` (not `chronon3d::graph`) because the
// `Composition` evaluation surface is the engine-generic timeline layer
// (TICKET-034).  `graph::ExecutionScope*` is forward-declared below so
// the Context can reference the per-call scope without dragging the
// `chronon3d::graph` namespace contents into every TU that just wants
// to evaluate a composition.
// ============================================================================

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>     // FrameRate
#include <chronon3d/core/types/types.hpp>          // i32, f32

#include <memory_resource>

namespace chronon3d {

// Forward declarations — keep this header lightweight.  `FrameArena` /
// `FontEngine` / `CompositionRegistry` are referenced only as POINTERS in
// `CompositionEvaluationServices`, so the forward decls suffice.  Full
// type definitions are pulled in by call sites that need to dereference
// them.  `graph::ExecutionScope` is the owning scope for the precomp /
// tile path; we forward-declare the class here so we can hold a pointer
// without pulling `execution_scope.hpp` into every TU that includes
// this header.
class FrameArena;
class FontEngine;
class CompositionRegistry;

namespace graph { class ExecutionScope; }

/// §8.1 — `CompositionEvaluationServices` (POD).
///
/// The set of services needed to evaluate a `Composition` at a given
/// point in time.  Default-initialised to nullptr pointers; call sites
/// are expected to wire each field the surrounding code can guarantee
/// (the `for_root` / `for_precomp` / `for_tile` factories wire the
/// canonical subset, callers with extra services can aggregate-init the
/// remaining fields).
///
/// Fields:
///   `registry`     — non-owning pointer to the `CompositionRegistry`
///                    that owns the composition.  Required to evaluate
///                    named compositions; the per-frame context
///                    factories take it by reference.
///   `arena`        — non-owning pointer to the per-evaluation
///                    `FrameArena`.  PMR allocations during the
///                    `evaluate(...)` call are served from this arena,
///                    NOT from the default heap.  Required for the
///                    precomp / tile path; may be the parent session's
///                    arena for the root render-job path.
///   `font_engine`  — non-owning pointer to the per-frame `FontEngine`.
///                    Optional — nullptr is equivalent to the legacy
///                    `Composition::evaluate(frame, t)` 3-arg overload
///                    (the engine-aware overload of `Composition::evaluate`
///                    defaults to nullptr when not set here).
///   `memres`       — non-owning pointer to the PMR memory resource.
///                    Optional — nullptr is equivalent to passing
///                    `std::pmr::get_default_resource()` at the
///                    `Composition::evaluate` boundary (existing
///                    3-arg/4-arg `evaluate` overloads default the
///                    memory resource to the default resource).
///
/// Stability contract: the field order, names, and pointer types are
/// FROZEN between §8.1 and §8.4.  `ValidatedExecutionServices` (§8.4)
/// wraps the same data layout, so any reordering here forces a parallel
/// reordering there.  New fields MAY be appended at the tail (with a
/// default-initialised value) in a follow-up PR without breaking ABI.
struct CompositionEvaluationServices {
    const CompositionRegistry*     registry{nullptr};
    FrameArena*                    arena{nullptr};
    FontEngine*                    font_engine{nullptr};
    std::pmr::memory_resource*     memres{nullptr};
};

/// §8.1 — `CompositionEvaluationContext` (POD).
///
/// The per-call evaluation context.  Embeds
/// `CompositionEvaluationServices` by value so `ctx.services.registry`
/// reads naturally (matches the `RenderGraphContext::services`
/// precedent in TICKET-010).
///
/// Per-call fields:
///   `frame`         — the current `Frame` (strong type; see
///                     `chronon3d/core/types/frame.hpp`).
///   `frame_time`    — sub-frame time in [0, 1) for motion-blur
///                     sub-frame evaluation; 0.0f for the
///                     single-frame render path.
///   `frame_rate`    — composition frame rate.  Held in the context
///                     (not derived from the composition itself) so
///                     the context can be USED WITHOUT holding a
///                     `Composition&` reference at the call site —
///                     the root render path keeps the comp ref alive
///                     but threads the rate into the context once.
///   `width`,
///   `height`        — composition dimensions at evaluation time.
///                     Mirrors `Composition::width()/height()`;
///                     included in the context so the per-call record
///                     is self-contained (the precomp path captures
///                     these from the nested comp once and threads
///                     them into the nested frame without a second
///                     lookup).
///   `name`          — non-owning view of the composition name
///                     (typically `comp.name()`).  Diagnostic only —
///                     `evaluate_composition` does NOT compare it
///                     against `comp.name()`.  May be empty.  Lifetime
///                     contract: the underlying string must outlive
///                     the context (callers that pass a temporary
///                     `std::string` should ensure the context's
///                     lifetime is shorter than the temp).
///   `scope`         — non-owning pointer to the owning
///                     `ExecutionScope` (for precomp / tile path);
///                     null for the root render-job path.
///                     Non-const deliberately: §8.3's
///                     `for_precomp(...)` factory will call
///                     `set_owner_key(...)` on this pointer to bind
///                     the precomp's owner key to the scope before
///                     the inner `execute_with_scope(...)` runs.
///                     Root render-job path leaves it null.
///
/// Stability contract: same as `CompositionEvaluationServices` — field
/// order, names, and types are FROZEN between §8.1 and §8.4.
struct CompositionEvaluationContext {
    CompositionEvaluationServices services{};
    Frame                        frame{0};
    f32                          frame_time{0.0f};
    FrameRate                    frame_rate{30, 1};
    i32                          width{0};
    i32                          height{0};
    std::string_view             name{};
    graph::ExecutionScope*       scope{nullptr};
};

} // namespace chronon3d
