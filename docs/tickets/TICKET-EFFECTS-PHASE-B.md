# TICKET-EFFECTS-PHASE-B — Blur, drop shadow, bloom and GPU routing

## Stato
CONTRACT APPROVED — software certification and capability routing are the
next implementation phase after Glow V1.

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

- [ ] CPU contract tests for each effect: neutral parameter identity, alpha
  edges, landscape/portrait bounds, deterministic repeated pixels, and warmed
  scratch reuse without per-frame framebuffer allocation.
- [ ] Spatial-halo tests prove changed/moved/shrunk effects clear both old and
  new dirty regions.
- [ ] DirectYUV matrix maps each effect to FullGraph; Auto records the route,
  RequireDirectYuv returns a typed capability error.
- [ ] Vulkan dispatch has one explicit capability row per effect. Supported
  kernels have CPU/GPU oracle parity and no readback; unsupported rows return
  `UnsupportedCapability` before any pixel fallback.
- [ ] Build, focused effects/render-graph tests, and short landscape/portrait
  videos pass.

## Demolition debt

- **Owner:** effects, render-graph, Vulkan, and direct-video maintainers.
- **Reason:** these effects have software processors but no complete,
  centrally certified GPU/DirectYUV routing contract.
- **Exit condition:** all acceptance gates pass on one source SHA.
- **Removal scope:** effect-specific silent fallback and any duplicated
  capability decisions outside the canonical matrix.
