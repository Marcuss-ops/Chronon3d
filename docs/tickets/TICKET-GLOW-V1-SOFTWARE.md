# TICKET-GLOW-V1-SOFTWARE — Certify deterministic CPU glow

## Stato
CONTRACT APPROVED — implement and verify before starting the following
backlog item.

## Contratto

Glow V1 keeps the existing `GlowParams` and the unified `GlowPipeline` as its
only authoring and execution path. This milestone certifies the software CPU
implementation for any positive canvas size in landscape and portrait
orientation. Radius and intensity are sampled continuously; output uses
premultiplied alpha correctly, clips at canvas bounds, and is deterministic
for identical input, including fixed reduction order wherever parallel work
could alter sums.

Dirty-region bounds include the complete effect support around both old and
new source bounds. Reusing a session across frames, changing ROI dimensions,
or shrinking the glow radius cannot expose pixels left in a scratch buffer.
Steady-state video frames reuse framebuffer and working-vector capacity; no
per-frame heap framebuffer allocation is allowed. Scratch remains owned by
the existing session/context authority; no parallel pool or executor is
introduced. Existing callers (layer effects and text glow) keep the same
visible alpha/compositing contract.

Glow V1 is software-only. Any GPU request requiring native glow capability
fails with the existing typed unsupported-capability result; it cannot fall
back silently. This ticket does not certify Gaussian blur, drop shadow, Bloom,
or other effects for GPU.

## Audit findings to resolve

- Glow currently builds a floating Gaussian kernel and temporary ROI
  framebuffers during execution; verify the session pool prevents framebuffer
  allocation and move reusable kernel/row storage into the existing scratch
  owner where necessary.
- `compute_layer_spatial_halo()` and `scene_dirty.cpp` already carry effect
  halos. Add a temporal regression that checks both old and new bounds after
  glow movement/radius changes, including the halo edge.
- Existing Gaussian passes parallelize independent rows and pixels; preserve
  fixed per-pixel tap order. Audit additive overlap/accumulation for any
  schedule-dependent reduction.

## Acceptance gates

- [ ] CPU contract: radius zero and intensity zero are identity; a single
  translucent colored pixel produces bounded halo coverage without dark or
  unpremultiplied fringes; subpixel radii sample continuously.
- [ ] Determinism: repeated runs and serial/parallel worker counts yield the
  same pixel hash.
- [ ] Landscape and portrait CPU renders, including a real short video, have
  stable frame hashes and no temporal streaking.
- [ ] Dirty-rect temporal regression expands source old/new bounds by the
  computed glow halo; shrinking radius and moving/removing the source clears
  the old halo.
- [ ] Scratch reuse regression processes a large ROI followed by a smaller
  ROI and a transparent frame; no stale pixels, and framebuffer allocation
  telemetry is zero after warm-up.
- [ ] GPU request without certified Glow capability fails with
  `UnsupportedCapability`; no software fallback.
- [ ] Focused renderer/backend tests and build pass.

## Demolition debt

- **Owner:** render-graph and software-effects maintainers.
- **Reason:** Glow still relies on the legacy temporary framebuffer acquisition
  helper and has not been certified as a temporal dirty-region effect.
- **Exit condition:** every acceptance gate above passes on one source SHA.
- **Removal scope:** per-frame temporary kernel/vector/framebuffer growth and
  any unaudited alternate Glow path found during implementation.
