# TICKET-CHRONON-INTEGRATION-OWNERSHIP — single authority for Chronon semantics

> Status: OPEN (authority declaration + census; no code moved yet).
> Scope: cross-repository (Chronon3d / RenderingGen / PipelineGen), single ticket —
> this is the coordination authority. Do not create parallel coordination tickets.

## Why this exists

A census found Chronon *semantics* implemented in more than one place:

| Semantic | Canonical owner (declared) | Duplicates found |
|---|---|---|
| RenderPlan schema / render semantics / capability definitions | **Chronon3d** (`schemas/json/chronon.*`, engine code) | none (engine is sole executor) |
| OverlayPlan → Chronon RenderPlan projector/compiler | **PipelineGen** (`refactored/internal/capabilities/overlays/chronon.go`) | `RenderingGen/renderinggen/internal/overlay/*` (older worker-side compiler) |
| Renderer invocation / transport / job execution | **RenderingGen** (thin: receive job → resolve assets → invoke chronon3d_cli → receipt) | `RenderingGen/renderinggen/internal/chronon/*` still owns certifier/metrics-ish logic |
| Certifier / plan projection / metrics wiring | **PipelineGen** (`refactored/internal/app/wiring/chronon/*`) | `RenderingGen/renderinggen/internal/chronon/*` (chronon.go + capabilities.go), `RenderingGen/renderinggen/internal/overlay/*` |
| Native engine certification harness | **Chronon3d** (`tests/`, `tools/`) + thin Go wrapper in PipelineGen only | `RenderingGen` cmd/* render-*-certification binaries duplicate PipelineGen flows |

## Declared ownership (target state)

```text
PipelineGen ── OverlayPlan / orchestration / ONE OverlayPlan→RenderPlan projector
      │
      ▼
Chronon RenderPlan contract   (schema owned by Chronon3d)
      │
      ▼
RenderingGen ── transport + job execution ONLY (no second compiler/certifier)
      │
      ▼
chronon3d_cli ── Chronon3d (RenderPlan semantics, render validation, execution,
                           determinism contract, backend semantics)
```

- Chronon3d owns: RenderPlan schema, render semantics, capability definitions,
  backend semantics, determinism contract, render validation rules, execution.
- PipelineGen owns: business/video orchestration, OverlayPlan, video-generation
  workflow, asset selection, scene construction, the ONE projector.
- RenderingGen owns: execution transport, invocation, artifact handling.
  RenderingGen must NOT own a second plan projector, Chronon semantics,
  capability logic, certifier or overlay compiler.

## Migration census (MOVE / DELETE / THIN ADAPTER)

Target: ONE lowerer. No fourth shared library.

## EXECUTED CENSUS — 2026-09-05 (evidence + classification)

### Corrected record of the live data flow (supersedes the assumption above)

Evidence:
- PipelineGen production enqueues SEMANTIC jobs: `platform/renderinggen
  queue_client.Submit` sends `RenderPlan: job.OverlaySpec`
  (`refactored/internal/platform/renderinggen/queue_client.go:76`), wired in
  production via `refactored/internal/app/wiring/script_generation_runtime.go:181-186`
  and `clip_render_runtime.go:40`.
- The RenderingGen worker is the LIVE lowerer:
  `internal/processor/processor.go:184` calls `overlay.CompileIfSemantic
  (job.RenderPlan)` → `internal/overlay/compiler.go` (schema
  `renderinggen.overlay-plan.v1` → `chronon.render-plan.v2`) → plan.json →
  chronon3d_cli.
- PipelineGen `CompileChrononPlan`/`chronon_compile.go` (output schema
  `chronon.render-plan.v1`) has NO production caller anywhere in `refactored/`;
  every call site is a `_test.go` (platform/renderinggen e2e, capabilities/
  entities tests) → dormant duplicate, not the bridge.
- The dormant v1 format is NOT consumable by the live worker: `overlay
  CompileIfSemantic` rejects every non-semantic plan ("semantic … is the only
  accepted contract"). The v1 model block + `GoldenChrononPlanV1/V2` goldens in
  `capabilities/overlays/` therefore certify a document the execution path
  never accepts — retiring them with the compiler is safe ONCE the semantic
  corpus (OverlaySpec → worker-compiled v2) has equivalent coverage.
- Go toolchain verified offline: `refactored/.../capabilities/overlays`,
  `RenderingGen/.../overlay` and `queue/...` all build clean, so the deletion
  migration can be run and tested locally when authorized.

### Per-area classification (MOVE / DELETE / KEEP-thin)

| Area | Verdict | Evidence / action |
|---|---|---|
| `RenderingGen/…/internal/overlay/` (compiler.go, entity_contract.go, official_presets.go, visual_style_resolver.go, asset_registry.go, motion.go, stats.go, subtitle logic) | **KEEP (execution-time lowerer)** | Live path above. Lowering happens AFTER worker-side asset materialization + subtitle burn-in; moving it to PipelineGen requires moving materialization upstream — a job-contract change, not a file move. Declare RenderingGen the owner of *execution-time lowering of semantic OverlayPlan → render-plan.v2*; PipelineGen owns the *semantic OverlayPlan model/choices* (it already emits OverlaySpec). |
| `PipelineGen …/capabilities/overlays/chronon.go` + `chronon_compile.go` + `chronon_util.go` (CompileChrononPlan v1) | **DELETE (dormant duplicate)** — after exit conditions | Zero production callers. 12+ test files compile plans with it for expectations/goldens. Action: re-point those tests at the live OverlaySpec → worker-compile flow (or move the compiler under a test-only helper), then delete. Do NOT delete before e2e parity is proven. |
| `RenderingGen/…/internal/chronon/` chronon.go, ipc.go, progress.go, telemetry.go, capabilities.go, serialized.go | **KEEP-thin (execution transport)** | Worker CLI/IPC invocation, progress, requirements gating, telemetry parsing of chronon3d reports. Remove nothing; capabilities.go stays as execution-side requirement selection. |
| `RenderingGen/…/internal/chronon/golden*.go`, example.go | **KEEP (dev corpus) with cleanup** | Used by cmd benchmarks + processor integration tests (golden_overlay_integration_test.go etc.). Delete `GoldenOverlayJobV1`/v1-named artifacts (schema is render-plan.v2 today); keep v2/semantic goldens. |
| `RenderingGen/…/cmd/*-certification`, render-*-presets, showcase | **KEEP-thin (local dev runners)** or DELETE after platform e2e covers them | They drive the worker packages directly (module boundary forbids PipelineGen imports). Module-name typo reassessment stays P3. |
| `RenderingGen cmd/zz-dump-tmp` | **DELETE (executed 2026-09-05)** | Zero references in RenderingGen tree, CI, docs, scripts. |
| PipelineGen golden corpora (overlays/golden*.go, golden_content.go, preset_certification.go) | **KEEP (semantic-level cert corpus)** | Distinct level from worker job goldens; do not merge. Drift between the two corpora is tracked, not code-moved. |
| PipelineGen metrics/wiring (capabilities/cliprender/*, app/wiring/chronon/*) | **KEEP (PipelineGen owner)** | Not duplicated in RenderingGen (worker receipts → PipelineGen persistence is the single layered path). Verify single ingestion per existing telemetry tickets. |
| Schema literals/comments | **ALIGN** | `chronon.render-plan.v1` literals are stale: Chronon3d schema registry exposes only render-plan.v2 (queue model comment `queue/internal/model/model.go:53`, RenderingGen chronon example/golden v1 names). Single source of truth = Chronon3d `schemas/json/chronon.render-plan.v2.schema.json`; align every Go constant/comment. |

### Decision taken (corrected ownership)

> RenderingGen owns EXECUTION-TIME LOWERING of the PipelineGen-emitted semantic
> OverlayPlan into render-plan.v2 (after asset materialization). PipelineGen owns
> the OverlayPlan semantics/choices and orchestration, and must stop carrying a
> second, dormant v1 compiler. No fourth shared library; the existing `queue`
> module remains the only cross-repo shared package.

## Exit conditions (each must hold before further DELETE/MOVE)

- [ ] PipelineGen e2e render green with `CompileChrononPlan` removed (tests
      re-pointed at OverlaySpec → worker compile).
- [ ] No CI/script/Docker references removed symbols.
- [ ] Chronon3d GPU parity corpus green on a Vulkan host (6094dce16 CPU-side
      semantics locked; GPU-side certification outstanding).
- [ ] Schema constants/comments aligned to `chronon.render-plan.v2`.

## Ordering guard

Do NOT start further cross-repo migration while the GPU backend can still produce
pixels different from CPU for the same input (VULKAN SEMANTIC PARITY CLOSEOUT
first — CPU side done in 6094dce16; GPU host certification is the gate).

### Cleanup executed 2026-09-05 (companion, non-semantic)

- Deleted 6 junk remote branches on Chronon3d (`tmp-*`, `__invalid_should_not_create`)
  after ancestry check (all ancestors of main).
- Renamed `vulkan_legacy_fallback.cpp` → `vulkan_native_shape_dispatch.cpp`
  (file hosted the current native fast-path, not a fallback).
- VeloxEditing root artifacts moved under `artifacts/` (workspace hygiene).
