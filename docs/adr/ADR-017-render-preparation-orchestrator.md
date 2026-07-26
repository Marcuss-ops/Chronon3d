# ADR-017 — Render preparation orchestrator

| Field | Value |
|---|---|
| Status | Implemented with explicit Phase 2 follow-up |
| Date | 2026-07-26 |
| Scope | Preflight → resource preparation → optional renderer warmup |

## Decision

`chronon3d::runtime::prepare_render()` is the single orchestration point for
rendering entry points. It evaluates the composition, runs the canonical
`AssetPreflightResolver`, invokes `ResourcePreparation`, populates runtime
font/image services, and optionally delegates to the existing
`warmup_renderer()`.

The function is synchronous and stateless. It owns no resolver, registry or
cache. A failed required asset returns a structured error before encoding;
there is no frame-loop busy wait or fake media metadata.

## Integrated paths

Still/job render, video export, chunked export, pipe export, benchmark and
video dry-run use `prepare_render()`. Commands named explicitly `preflight`
continue to perform manifest-only validation because they do not create a
renderer or encoder.

## Current boundary

Native video/audio probing is compiled only with
`CHRONON3D_ENABLE_NATIVE_FFMPEG`. Without that option, required media assets
fail with a structured preparation error rather than fabricated dimensions or
duration. Phase 2 remains tracked in
`docs/tickets/TICKET-ASSET-PREP-BARRIER.md` for backend handle population and
the future `RenderEngine::prepare()` API.

## Verification

`tests/runtime/test_resource_preparation.cpp` covers fail-loud defaults,
WarnAndSkip, per-phase opt-out, duplicate owners, malformed paths and all
phase loaders. The architecture boundary gate and doc-sync gate must remain
green when this contract changes.
