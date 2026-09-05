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

Target: `PipelineGen internal/.../chronon = ONE OWNER`. No fourth shared library.

1. `RenderingGen/renderinggen/internal/chronon/` — census every symbol: is it
   (a) thin invocation of chronon3d_cli (keep/adapter),
   (b) plan/capability/certifier semantics duplicated in PipelineGen (DELETE or MOVE),
   (c) metrics duplicated by PipelineGen wiring (DELETE or MOVE).
2. `RenderingGen/renderinggen/internal/overlay/` — compare against
   `refactored/internal/capabilities/overlays/chronon.go`; the compiler must live
   exactly once (PipelineGen per declaration). Remove the RenderingGen duplicate
   after PipelineGen reachability is proven by the local e2e render jobs.
3. `RenderingGen/renderinggen/cmd/*-certification` — port to PipelineGen flows or
   delete; keep at most a thin runner.
4. After RenderingGen is thin: reassess the module name typo
   (`github.com/Marcuss-ops/RenderginGen/…`) — renaming only pays off once the
   module is small; track separately as P3 (MODULE-NAME-COMPATIBILITY) if kept.

## Exit conditions (each must hold before MOVE/DELETE)

- [ ] A rendered job proves PipelineGen projector output == current projector
      output for the certified corpus (same RenderPlan JSON for same OverlayPlan).
- [ ] PipelineGen e2e render (queue → RenderingGen → chronon3d_cli) green with
      the duplicate removed.
- [ ] No CI/script/Docker references the removed symbols.

## Ordering guard

Do NOT start this migration while the GPU backend can still produce pixels
different from CPU for the same input (VULKAN SEMANTIC PARITY CLOSEOUT first —
grid premultiplied background, resolved major-line rule, conservative native
stroke gating). A large cross-repo reorganization on top of drifting backend
semantics multiplies the blast radius.
