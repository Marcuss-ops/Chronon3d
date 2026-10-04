# TICKET-VULKAN-EFFECT-PARITY — Native GPU parity for the effect catalog

## Stato

OPEN — software exposes 47 effect types; strict Vulkan has native dispatch
routes for 17 effect types (Blur, Glow, Bloom, Tint, Brightness, Contrast,
Saturation, HueRotate, Invert, Exposure, Fill, Levels, Threshold, Keying,
Vignette, Mosaic, Echo) and explicitly rejects DropShadow.
All remaining types are unsupported on this capability surface and fail closed. No CPU fallback
is allowed for a strict GPU request.

## Obiettivo

Every effect accepted by the canonical software effect catalog must have a
semantically equivalent native Vulkan implementation, or remain explicitly
rejected until its implementation and parity gates land. “GPU required” must
never be satisfied by software execution. Keep the existing EffectCatalog,
RenderGraph, GraphExecutor, surface authority and effect parameter contracts;
do not add a second renderer, registry, cache, or effect API.

## Contratto di parità

- Preserve each CPU effect's input/output alpha, color-space, edge, bounds,
  clipping, temporal and parameter-clamp semantics.
- Spatial and multi-input effects use only graph-owned native surfaces and
  planned scratch; no host readback or hidden CPU fallback.
- Animated effects sample the existing frame/time context and remain
  deterministic for the same inputs and frame.
- Strict native execution fails closed before dispatch if any enabled effect in
  the stack is not certified for its exact parameter/input contract.
- Capability rows are exhaustive over `EffectType`; adding a catalog effect
  without a declared native state must fail a compile/test gate.

## Waves

1. **Native surface primitives and simple color effects** — exhaustive
   capability accounting; Brightness, Contrast, Saturation, HueRotate, Invert,
   Exposure, Fill, Threshold, and other one-input pointwise transforms.
2. **Blur/light effects** — DropShadow and full parameter parity for Blur,
   Glow, Bloom, DirectionalBlur, RadialBlur, Sharpen, and Vignette.
3. **Spatial transforms and stylize** — Offset, Mosaic, CornerPin, Twirl,
   Bulge, MeshWarp, TurbulentDisplace, WaveWarp, Ripple, ChromaticAberration,
   Halftone, Stroke and Fake3DWave.
4. **Generators and multi-input effects** — Noise, FractalNoise, Checkerboard,
   FourColorGradient, LightRays, LUT3D, Curves, Levels, Keying, DisplacementMap
   and Echo, subject to the existing graph input and temporal contracts.

Waves may be split into small tickets; each ticket owns its shader ABI,
fingerprint/capability changes, software-vs-Vulkan pixel oracle, and rejection
cases. The catalog-wide ticket closes only when every catalog row is native and
the full software/GPU effect corpus passes on the same SHA.

## Acceptance gates

- [ ] Compile-time/test census accounts for every `EffectType` exactly once.
- [ ] Each wave has parameter-boundary tests and software/Vulkan pixel parity
  fixtures for transparent, opaque, clipped, edge and ROI cases as applicable.
- [ ] Multi-effect stacks preserve authored order and parity.
- [ ] Temporal effects render stable frame hashes and never reuse static cache
  entries incorrectly.
- [ ] Native strict export demonstrates zero readback/staging and no CPU effect
  dispatch for the entire supported catalog.
- [ ] GPU-host same-SHA certification passes; a host without GPU reports
  `NOT RUN`, not PASS.

## Local progress — 2026-10-04

- Keying lowers through the existing Vulkan color-matrix route. The local
  Vulkan pixel-parity fixture covers luma/chroma, spill, matte clipping/choke,
  garbage matte and partial clip. Fixed display-space framehash canaries for
  luma and chroma each match the registered software processor and the
  expected 8-bit sRGB FNV-1a hash (268 pixel-channel/hash assertions).
- Strict multi-effect stacks execute currently certified routes in authored
  order through graph-owned intermediate surfaces. Both a Brightness →
  disabled stage → Invert fixture and a Blur → Invert pixel oracle match the
  software reference. The blur stack route uses the CPU processor's repeated
  box-filter kernel; the existing Gaussian kernel remains unchanged for its
  other callers. Brightness → LumaKey and LumaKey → Brightness each match the
  software oracle, and the outputs differ as expected (259 assertions).
  Echo → non-identity Brightness also matches the software golden for both
  frames, confirming a temporal stage followed by a substantive native color
  stage. Vignette now lowers through the same color-matrix dispatch, with
  software/Vulkan pixel parity across parameter cases and clip coverage; a
  three-stage Brightness → Vignette → Invert stack matches the software result.
  Mosaic now lowers through a source-sampling dispatch. Block sizes 1, 3, and
  512, partial clip behavior, and a Brightness → Vignette → Mosaic → Invert
  stack match the software processors on llvmpipe. Mosaic preserves transparent
  source samples while selecting each block's center. Unsupported effects and
  the full catalog are not certified. The Echo integration oracle now also
  covers Brightness → Echo in addition to Echo → Brightness: both frames match
  the registered software stack across all 32 pixel channels (87 assertions).
  Strict AdjustmentNode execution now delegates fixed-extent stacks to the
  same typed native stage executor. Brightness → Invert matches the software
  oracle (130 assertions); a spatial Blur stack fails closed before reaching
  the legacy backend API. Expanding or temporal adjustment stacks remain
  unsupported by this fixed-extent path.
- Solid Rect, RoundedRect and Circle strokes have a CPU/Vulkan pixel oracle for
  all three alignments with translated geometry, clipping, uniform scales
  0.75x, 1x, 1.25x and 2x, anisotropic scales 1.25x0.75 and 0.75x2, rotation
  0.37/-0.29 radians and a rotated anisotropic scale (1,769,836 assertions).
  The Vulkan shader evaluates coverage with the same inverse-transform
  coefficients as the CPU rasterizer, including pixel-edge rounding. An
  explicit shear case fails closed after exposing pixel disagreement. A
  per-corner RoundedRect stroke fixture now also matches the software oracle
  for Inside/Center/Outside under a rotated anisotropic transform (61,455
  pixel-channel assertions). Shear and masks remain unsupported.
- Echo now uses job-owned, frame-addressed Vulkan surface history and weighted
  premultiplied source-over, including as an ordered stack stage. Local fixtures
  cover exact frame/key lookup, reset, no-readback surface copies, two-frame
  graph output values, direct comparison against the registered software
  processor for every output channel, and a fixed second-frame display-space
  framehash. The capability census reports 17 native routes.
- After Echo/keying framehash additions and rotated stroke parity changes,
  CTest passed the Vulkan backend-registry, render-graph, and Vulkan
  debug-context suites on llvmpipe (3/3); backend-registry was rerun after the
  keying-stack order fixture and passed. The Echo/history focus passed 2 cases
  / 48 assertions; keying framehashes passed 268 assertions, keying stack order
  passed 259 assertions; the expanded stroke pixel oracle passed 1,769,836
  assertions. The per-corner stroke fixture passed 61,455 pixel-channel
  assertions; the complete backend-registry CTest passed locally on llvmpipe.
- The native capability census is now 17/47; 30 catalog routes remain
  unsupported. The backend-registry and render-graph CTest targets both passed
  locally on llvmpipe after the Mosaic addition.
- This is not GPU-hardware or same-SHA CI certification. The remaining catalog
  routes and unsupported stroke transforms are still open.

## Remote certification blocker — 2026-10-04

GitHub Actions runs for `fb052dadf7bd1fe27a5377cbc63c4daeb1a305ca`, including
the `Vulkan-gated unit suites` job, ended before runner allocation (`runner_id`
0, empty runner name, no steps). The check-run annotation reports that the job
was not started because recent account payments failed or the spending limit
must be increased. This records an account-level execution blocker; it is not
test evidence and does not certify this SHA on GPU hardware.

The same condition was rechecked after pushing `e87d65fe0a85abe3b04071b1d7e14abb5c0b9c3d`:
the `Vulkan-gated unit suites` job (run `37215701067`, job `111475639697`)
again completed without runner allocation (`runner_id=0`, no steps) and carried
the same billing annotation. The tested commit is `main`/`origin/main`; remote
GPU-host certification remains pending until Actions can allocate a runner.

After adding the native Vignette stage, commit
`584e085472f045e4cb5139e3da560acf978d2a77` was also pushed to `main`. Its
`Vulkan-gated unit suites` job (run `37217448778`, job `111480751356`) has
`runner_id=0`, no steps, and the same account-billing annotation. Local
llvmpipe tests pass, but this run provides no GPU-host evidence.

After the Echo stack-order fixture, commit
`bf8e23184f8e811fcb2a672b75808bc74e7753e0` was pushed to `main`. The
`Vulkan-gated unit suites` job (run `37219496458`, job `111486769601`) again
ended before runner allocation (`runner_id=0`, no steps) with the same billing
annotation. This remains an external certification blocker, not test evidence.

## Demolition debt

- **Owner:** render-graph and Vulkan backend maintainers.
- **Reason:** native GPU support is incomplete while software is the broader
  canonical effect implementation.
- **Exit condition:** all catalog entries pass the native and parity gates.
- **Equivalence gate:** same-input software/Vulkan pixel oracle plus full-stack
  strict-native export and zero-readback evidence.
- **Removal scope:** the temporary unsupported capability rows and any
  effect-specific CPU-only routing guards that become obsolete.
- **Status:** ACTIVE.
