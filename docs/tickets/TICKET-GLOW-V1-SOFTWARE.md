# TICKET-GLOW-V1-SOFTWARE — Certify deterministic CPU glow

## Stato
COMPLETE — software CPU behavior and native-video gates verified.

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

- [x] CPU contract: radius zero and intensity zero are identity; a single
  translucent colored pixel produces bounded halo coverage without dark or
  unpremultiplied fringes; radius 4.0 versus 4.5 changes the measured halo.
- [x] Determinism: the unit regression compares every RGBA float from default
  TBB scheduling against a serial one-worker render; pixels match exactly.
- [x] Landscape and portrait CPU renders produced native H.264 video: 960×540,
  90 frames, 3.000 s and 540×960, 12 frames, 0.400 s. Decoded landscape frame
  hashes cover all 90 frames (35 distinct frames); portrait hashes cover all
  12 frames (12 distinct frames).
- [x] Dirty-rect temporal regression expands source old/new bounds by the
  computed glow halo; shrinking radius and moving/removing the source clears
  the old halo.
- [x] Scratch reuse regression processes a large ROI followed by a smaller
  ROI and a transparent frame; no stale pixels, and framebuffer allocation
  telemetry reports no new pool allocation after warm-up. The initial large
  Gaussian kernel capacity is retained when a later frame uses a smaller one.
- [x] GPU strict-native request without host capability fails closed before
  setup (`required_gpu_capability_unavailable`); the host has no native Vulkan
  capability, so the effect-specific runtime `UnsupportedCapability` branch
  could not execute here.
- [x] Focused build passed; `chronon3d_render_graph_tests` and
  `chronon3d_effects_tests` both pass (44 s total).

## Demolition debt

- **Owner:** render-graph and software-effects maintainers.
- **Reason:** Glow still relies on the legacy temporary framebuffer acquisition
  helper and has not been certified as a temporal dirty-region effect.
- **Exit condition:** every acceptance gate above passes on one source SHA.
- **Removal scope:** per-frame temporary kernel/vector/framebuffer growth and
  any unaudited alternate Glow path found during implementation.
