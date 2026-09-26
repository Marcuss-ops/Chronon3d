# TICKET-EFFECTS-PHASE-B — Blur, drop shadow, bloom and GPU routing

## Stato
IN PROGRESS — CPU contracts, spatial halos, and explicit Vulkan capability
rows are implemented. Vulkan blur/glow remain native; drop shadow and bloom
are explicitly unsupported in strict GPU mode. Video E2E and pixel parity
gates remain open.

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
- [x] Vulkan dispatch has one explicit capability row per effect. Existing
  native blur/glow paths remain available; strict GPU mode now returns
  `UnsupportedCapability` whenever a stack cannot be handled natively, before
  software fallback. Drop shadow/bloom remain typed `UnsupportedCapability`;
  blur/glow use native zero-readback dispatch. Pixel parity is still open.
- [ ] Build, focused effects/render-graph tests, and short landscape/portrait
  videos pass for blur, shadow, and bloom. Focused effects and render-graph
  suites pass; current video CLI rebuild is blocked by 8.1 GB available disk
  space being insufficient for the large static archive link. (Glow V1 has
  separate software/video coverage.)

## Demolition debt

- **Owner:** effects, render-graph, Vulkan, and direct-video maintainers.
- **Reason:** these effects have software processors but no complete,
  centrally certified GPU/DirectYUV routing contract.
- **Exit condition:** all acceptance gates pass on one source SHA.
- **Removal scope:** effect-specific silent fallback and any duplicated
  capability decisions outside the canonical matrix.
