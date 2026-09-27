# ADR-033 — Architecture gates for Tier C RenderPlan features

**Status**: PROPOSED
**Date**: 2026-09-27
**Deciders**: project maintainers
**Tags**: `render-plan`, `assets`, `determinism`, `3d`, `text`

## Context

The RenderPlan backlog names five features that cross current subsystem
boundaries and cannot be represented by the present bounded V3 contracts:
geographic globe rendering, authored expressions, full SVG/Lottie playback,
word-timed SRT captions, and animated/skinned glTF. Their data and runtime
contracts affect asset resolution, deterministic fingerprints, resource
budgets, and rendering ownership. Implementing isolated decoder fields before
those decisions exist risks accepting payloads that cannot be lowered
faithfully.

## Decision

Keep each feature rejected at the RenderPlan boundary until its dedicated
decision and acceptance contract are reviewed. A proposal for any of these
features must settle the following before implementation:

1. **3D globe** — geographic data source and redistribution terms, coordinate
   reference and projection, center/zoom semantics, country highlighting,
   tessellation limits, and the Mesh/Light resource budget.
2. **Expressions** — the supported grammar, allowed inputs, frame/time model,
   deterministic numeric behavior, operation and recursion limits, and how
   expression source contributes to the plan fingerprint.
3. **Full SVG/Lottie** — supported format versions and operators, asset
   resolver requirements, parser security limits, geometric expansion limits,
   and the boundary between import-time lowering and per-frame evaluation.
4. **SRT word highlighting** — timestamp precision, word segmentation and
   Unicode policy, handling of overlapping cues, layout ownership, and the
   resulting text-run/keyframe representation.
5. **Animated/skinned glTF** — supported animation channels and skinning
   features, interpolation semantics, CPU/GPU ownership, pose sampling, and
   vertex/joint memory limits.

This proposal does not authorize those implementations or change the V3
schema. Each accepted design must be followed by a separate ADR or an explicit
amendment here, decoder contract, resource budget, fingerprint definition,
lowering path, and focused acceptance tests.

## Consequences

**Positive.** Unsupported payloads remain explicit failures instead of
fingerprinted promises that the renderer cannot honor. Each future feature has
clear cross-subsystem questions to resolve before code is added.

**Cost.** The five features remain unavailable at the V3 boundary until their
decisions are reviewed; delivery requires separate design and implementation
work.

## Alternatives considered

1. Add placeholder plan types and defer rendering semantics. Rejected because
   the fingerprint would identify behavior the compiler does not produce.
2. Add unbounded parsers or geometry generation directly to the decoder.
   Rejected because it bypasses the asset and render-budget boundaries.

## References

- `docs/tickets/TICKET-RENDER-PLAN-REMAINING-FEATURES.md`
- RenderPlan V3 schema: `schemas/json/chronon.render-plan.v3.extensions.schema.json`
