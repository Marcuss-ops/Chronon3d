# TICKET-EFFECTS-PHASE-B — Blur, drop shadow, bloom and GPU routing

## Stato
PARTIAL — CPU contracts, spatial halos, and native Vulkan zero-readback paths
for blur/glow/bloom/drop shadow are implemented. Local effects and render-graph
suites pass; landscape/portrait video E2E and Vulkan pixel certification remain
open.

## TODO — cosa manca (agg. 2026-10-28)

- [x] Implementare DropShadow in strict Vulkan con alpha-mask, offset, doppia
      blur e compositing source-over; il test di parità è in
      `tests/registry/test_backend_registry_vulkan_effect_parity.cpp`.
- [ ] Short video E2E software landscape/portrait per blur, shadow e bloom.
- [x] Build CLI `-j12` e suite locali effects/render-graph — PASS 2026-10-28.
- [ ] Parità pixel software/Vulkan per blur/glow/bloom.

## Contratto

`BlurParams`, `DropShadowParams`, and `BloomParams` remain the canonical plan
and scene parameters. This phase certifies the existing software processors,
their spatial bounds, alpha behavior, zero/identity fast paths, and warmed
scratch reuse. No alternate CPU processor or render executor is introduced.

GPU capability is declared in the existing capability authorities. DirectYUV
must classify these RGBA-only effects as requiring the faithful FullGraph
path; `RequireDirectYuv` rejects the request. Vulkan must either execute a
dedicated zero-readback shader path with pinned CPU/GPU pixel parity, or return
typed `UnsupportedCapability`. A non-strict software fallback is allowed only
when the caller selected Auto and the resolver reports that fallback; strict
GPU requests never fall back to CPU.

Blur, drop shadow, and bloom kernels must preserve premultiplied alpha,
canvas-edge clipping, deterministic accumulation, and conservative old/new
dirty bounds. Parameter values are clamped or rejected only according to the
existing contracts; no silent parameter reinterpretation is allowed.

## Acceptance gates

- [x] CPU contract tests for each effect: neutral parameter identity, alpha
  edges, landscape/portrait bounds, deterministic repeated pixels, and warmed
  scratch reuse without per-frame framebuffer allocation.
- [x] Spatial-halo tests prove changed/moved/shrunk effects clear both old and
  new dirty regions.
- [x] DirectYUV effect stacks map to FullGraph; Auto records the route,
  RequireDirectYuv returns a typed capability error (composition capability
  unit suite passed).
- [x] Vulkan dispatch has one explicit capability row per effect. Native
  blur/glow, bright-pass/blur/add bloom, and alpha-mask drop shadow use
  zero-readback dispatch; strict GPU mode refuses effects without a native row.
- [x] Rebuild CLI with `-j12`; focused effects and render-graph suites pass
  locally (2026-10-28).
- [ ] Short landscape/portrait video E2E for blur, shadow, and bloom (software
  lane); Vulkan pixel parity remains a separate host-GPU certification gate.

## Demolition debt

- **Owner:** effects, render-graph, Vulkan, and direct-video maintainers.
- **Reason:** these effects have software processors but no complete,
  centrally certified GPU/DirectYUV routing contract.
- **Exit condition:** all acceptance gates pass on one source SHA.
- **Removal scope:** effect-specific silent fallback and any duplicated
  capability decisions outside the canonical matrix.
