# TICKET-VULKAN-EFFECT-PARITY — Native GPU parity for the effect catalog

## Stato

OPEN — the catalog exposes 48 effect types; the executable capability table declares 26 strict-native routes (including DropShadow and DirectionalBlur) and 22 unsupported types. All unsupported types fail closed and no CPU fallback is allowed for a strict GPU request. Current-checkout GPU hardware visibility does not substitute for rerunning parity on this SHA.

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
   Sharpen is DONE (24/47 native): `shaders/sharpen.comp` pointwise
   unsharp-mask combine over the certified separable box route,
   `try_native_full_frame_sharpen` (radius (0,10], full frame, fail-closed
   otherwise), capability row, census 23→24, oracle
   `golden/sharpen_native_parity` + `scripts/ci/check_sharpen_native_parity.py`
   (mean 1.939 / max 22 vs lossless CPU oracle; no-effect transport floor
   0.75 / 11).
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

- [x] Compile-time/test census accounts for every catalog `EffectType` exactly once (48 rows; 26 native / 22 unsupported in `test_effect_gpu_capability.cpp`).
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
  Echo → non-identity Brightness in EffectStackNode matches the software
  golden for both frames, confirming a temporal stage followed by a substantive
  native color stage. Vignette now lowers through the same color-matrix dispatch, with
  software/Vulkan pixel parity across parameter cases and clip coverage; a
  three-stage Brightness → Vignette → Invert stack matches the software result.
  Mosaic now lowers through a source-sampling dispatch. Block sizes 1, 3, and
  512, partial clip behavior, and a Brightness → Vignette → Mosaic → Invert
  stack match the software processors on llvmpipe. Mosaic preserves transparent
  source samples while selecting each block's center. Unsupported effects and
  the full catalog are not certified. Echo parity covers both stage orders:
  EffectStackNode Echo → Brightness and AdjustmentNode Brightness → Echo both
  match the registered software stack on two frames.
  Strict AdjustmentNode execution now delegates fixed-extent stacks to the
  same typed native stage executor. Brightness → Invert matches the software
  oracle (130 assertions). AdjustmentNode carries Echo's deterministic
  history key, frame-variant cache policy and preroll requirement from the
  graph builder into the same executor. Brightness → Echo matches the software
  oracle on both frames (88 assertions, including the preroll contract);
  effects that require output bounds beyond the input remain unsupported by
  the fixed-extent path. Brightness → ChromaKey and ChromaKey → Brightness also match the
  software oracle through AdjustmentNode (259 pixel-channel assertions).
  AdjustmentNode now also executes Blur → Invert through the shared native
  stage executor while preserving its fixed input extent; the output matches
  the software oracle with an explicit full-frame bbox (130 pixel-channel
  assertions). A partial-clip fixture also matches all channels (130
  assertions); Vulkan copies the CPU-equivalent blur halo before the following
  clipped color stage. Other effects requiring expanded output bounds remain
  outside this fixed-extent route. The native Glow route now prepares a
  colorized alpha field, applies the CPU-equivalent linearized Gaussian and
  composites the halo behind the source; full-frame Glow matches software at
  radii 0, 0.49, 0.5, 1.5, 3, and 10 (768 channel comparisons, 780 total
  assertions). Glow → Invert
  and Bloom → Invert both run through AdjustmentNode and EffectStackNode at
  radii 0.5, 1.5, 3,
  and 10 and match the software oracle (1,024 channel comparisons, 1,040 total
  AdjustmentNode assertions; the EffectStackNode fixture adds 1,024 channel
  comparisons and passes 1,032 total assertions). An additional two-frame
  AdjustmentNode oracle now
  covers Brightness → Echo → ChromaKey (88 channel assertions). An
  EffectStackNode fixture also matches two frames of Echo → Brightness →
  ChromaKey against the registered software stack (64 channel comparisons,
  92 total assertions) and pins the second-frame display-space framehash. The strict
  full-frame Bloom route promotes the original input surface before processing
  expanded output bounds, uses the software box kernel, and matches its oracle
  at radii 0, 0.5, 1.5, 3, and 10 (650 channel assertions); radius 10.1 fails
  closed because the CPU route downsamples above radius 10. Bloom → Invert in
  AdjustmentNode matches the software oracle at radii 0.5, 1.5, 3, and 10;
  this is focused parity evidence for those parameters, not exhaustive Bloom
  boundary or downsampling parity.
- Solid Rect, RoundedRect and Circle strokes have a CPU/Vulkan pixel oracle for
  all three alignments with translated geometry, clipping, uniform scales
  0.75x, 1x, 1.25x and 2x, anisotropic scales 1.25x0.75 and 0.75x2, rotation
  0.37/-0.29 radians and a rotated anisotropic scale (1,769,836 assertions).
  The Vulkan shader evaluates coverage with the same inverse-transform
  coefficients as the CPU rasterizer, including pixel-edge rounding. An
  explicit shear case fails closed after exposing pixel disagreement. A
  regression fixture verifies Rect shear is rejected for Inside, Center and
  Outside alignment (8 assertions). The per-corner RoundedRect stroke fixture
  also matches the software oracle
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
- The native capability census is now 19/47; 28 catalog routes remain
  unsupported. The backend-registry and render-graph CTest targets both passed
  locally on llvmpipe after the Mosaic addition.
- TemperatureTint is now lowered through the native affine color transform
  using the software processor's clamped temperature/tint gains in straight
  alpha. Pixel oracles cover mixed and clamped parameters, half-alpha/HDR source
  samples, clip behavior and ordered strict AdjustmentNode execution. The
  native capability inventory was increased to 19 routes; local hardware
  certification is still outstanding.
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

After adding strict `AdjustmentNode` stack execution, commit
`456eb4e3b135bfa82ecab35131bfda8e4dc652f6` was also checked. The
`Vulkan-gated unit suites` job (run `37220681906`, job `111490216673`) again
had `runner_id=0`, no steps, and the same billing annotation; no GPU-host
certification ran for this commit.

After adding frame-addressed Echo to strict adjustment stacks, commit
`0f1b00f9ebc992e5732af4722e69ae805e20a051` was checked as well. Its
`Vulkan-gated unit suites` job (run `37221466498`, job `111492504050`) again
ended before runner allocation (`runner_id=0`, no steps) with the same billing
annotation. Hardware certification remains pending.

After native Bloom stack parity landed, commit
`39f130835260f7a27e527353938c57e7a3ba06b7` was checked in run `37227188905`.
Both Vulkan-gated jobs ended before runner allocation (`runner_name` empty,
steps empty) with the same account-billing annotation. The full developer-gate
workflow (`37227188955`) had the same pre-allocation failure. These runs do
not provide GPU-host certification; local llvmpipe remains the available
parity evidence.

The latest effect-stack parity commit, `51656c0740d3f54b7ce1952112d624a8402648b6`,
was checked in Native Video Certification run `37229448896`. Its `Vulkan-gated
unit suites` job (`111515841679`) completed before runner allocation
(`runner_id=0`, empty runner name, no steps) with the same account-billing
annotation. This run provides no hardware test evidence; same-SHA GPU-host
certification remains pending.

The follow-up documentation commit, `9de9613caa5eeb7cb83a8b2c2686da0ede66fe12`,
was checked in Native Video Certification run `37229649595`. Its `Vulkan-gated
unit suites` job (`111516439414`) likewise ended before runner allocation
(`runner_id=0`, empty runner name, no steps) with the same billing annotation.
Hardware certification remains pending.

The two-frame EffectStackNode Echo/keying parity commit,
`ed96c29794444d6dff8d9fb16564f889527c542b`, was checked in Native Video
Certification run `37230674950`. Job `111519465115` ended before runner
allocation (`runner_id=0`, empty runner name, no steps) with the same billing
annotation. Local tests pass on llvmpipe; this run does not certify hardware.

### Local implementation progress — 2026-10-05

- Added LiftGammaGain as native color-matrix mode 6. The shader evaluates the
  signed power curve in straight RGB, with the same finite-input check and
  software parameter clamps (`lift [-1,1]`, `gamma [0.1,4]`, `gain [0,4]`).
  The route is admitted by strict AdjustmentNode and its pixel-parity cases
  include fractional alpha, HDR input, clamp extremes, identity, and clipping.
- At LiftGammaGain landing, the native census was 20/47; 27 catalog effects
  remained explicitly unsupported. The current census is 21/47 after Offset.
  Hardware certification still depends on a remotely allocated GPU runner;
  local llvmpipe results are not hardware evidence.
- Verified locally: LiftGammaGain parity (4 parameter cases; maximum observed
  absolute error 0.00390625 on clamped extreme parameters), ordered strict
  AdjustmentNode composition (130 assertions), full backend registry CTest
  (1/1), full render-graph CTest (1/1), and capability census (96 assertions).
- Offset is now native for both sample filters and all edge modes, using the
  software sampler's pixel-center and zero-offset no-op rules. Its local parity
  case covers the 8 filter/edge pairs, integer Wrap offset, identity and a
  partial-clip Bilinear case (22 assertions; maximum error 1.19209e-7).
  Strict AdjustmentNode composition also includes fractional mirrored
  Bilinear Offset.
- At Offset landing, the native census was 21/47; 26 effects remained
  explicitly unsupported.
- After Offset integration, backend registry CTest passes (1/1, 18.96 s),
  render-graph CTest passes (1/1, 39.07 s), architecture gate passes 206/206,
  and `git diff --check` is clean. The tests use local llvmpipe.
- Noise is now native using a shader implementation of the software SplitMix64
  per-pixel hash. The lowering carries seed/frame bits losslessly and mirrors
  grain size, animated frame selection, monochrome/RGB modes, tone response,
  clip and software no-op conditions. Its parity cases cover static monochrome,
  animated RGB at frame 37, partial clip, amount zero and invalid response.
- Local Noise evidence: focused parity passes 10 assertions with maximum
  absolute error 5.96046e-8; ordered AdjustmentNode composition passes 130
  assertions; full backend registry and render-graph CTest pass (1/1 each).
- Historical census after Noise: 22/47; 25 effects remained explicitly unsupported.

Checkerboard now has a strict Vulkan generator route. It mirrors software cell
parity, feathering, color interpolation, premultiplied alpha and clipping. Four
focused parity cases pass on local llvmpipe (8 pixel assertions; maximum error
5.96046e-8); they cover fractional cell size, translucent colors, clipping,
feather clamp and invalid-size no-op. The ordered AdjustmentNode composition
test also includes Checkerboard and passes 130 assertions. Current native
census is 23/47 with 24 effects explicitly unsupported. Local CTest passes the
backend registry and render-graph suites (1/1 each); hardware certification
remains open.

## Telemetry prerequisites — 2026-10-07

The previous effect-usage sidecar recorded only native and strict-admission
counts; legacy sidecars in this checkout contain no per-effect observations, so
no frequency-based priority can be inferred from them. The active dispatch path
now records software dispatch count, measured processor CPU nanoseconds, and
separate fallback count/time when a native-surface backend was attempted and
software subsequently ran. Fused color stages receive invocation and fallback
counts; their individual runtime remains unmeasured because the pipeline is
executed as one fused batch. Strict-native admission rejection remains distinct
from an executed CPU fallback. After fresh sidecars are collected, rank by
fallback count, frequency, then measured CPU fallback time; do not infer the
“top five” before that data exists.

## Local implementation progress — 2026-10-09 (DropShadow alpha parity)

- Corrected the software DropShadow compositor to treat framebuffer RGB and
  both blurred shadow lobes as premultiplied linear RGBA: contact is composited
  over ambient, then the original content over the shadow. The previous
  component-wise `max` plus straight-alpha `blend_normal` path combined the
  lobes incorrectly and multiplied premultiplied content RGB a second time.
- The Vulkan composite route now has an explicit premultiplied source-over
  mode for ordinary Normal composites; replace copies and weighted Echo
  composites keep their distinct contracts. This is used by the existing
  native DropShadow stages and avoids host readback/software fallback.
- Expanded the strict software/Vulkan oracle with transparent and fractional
  alpha content, varied RGB, integer/fractional and negative offsets, radii
  0/0.5/3/5, and a partial-clip case. The final post-edit focused run passed:
  1 case, 522 assertions; radius 32.1 and non-finite input continue to fail
  closed. The comparator tolerance was not relaxed.
- Build of `chronon3d_backend_registry_tests` succeeded. The later full
  `chronon3d_backend_registry_tests` CTest run did **not** pass: 97/98 cases
  passed, while `GPU readiness gate certifies the 7-point checklist` failed
  allocating a 1920x1080 Vulkan surface (`backend.create_surface(...).ok()`
  false). At that time `nvidia-smi` showed only 195 MiB free of 16,376 MiB;
  an unrelated Ollama model server and a persistent Vulkan render daemon
  occupied the device. Neither process was stopped. The independent
  `chronon3d_vulkan_interop_generation_tests` passed (1/1). Re-run the full
  registry CTest when the GPU host has adequate free memory.
- The same-SHA strict Vulkan/NVENC motion run available before the final
  DropShadow rebuild recorded 101/125 image motions passed and 24 rejected:
  the 12 `web_rect_*` cases failed closed on the missing DropShadow route,
  and 12 `lightleak_*` cases failed closed on their effect stack. This run is
  historical evidence for why DropShadow was implemented, **not** certification
  of the rebuilt commit. Phrase (202/202), entity-caption (56/56), and
  presentation (70 outputs) completed successfully in their recorded runs,
  but they likewise predate the final DropShadow test edit and must not be used
  as same-SHA certification.
- A bounded `web_rect` GPU rerun against the rebuilt CLI could not complete in
  the shared host environment: surface allocations returned
  `VK_ERROR_OUT_OF_DEVICE_MEMORY` and the render failed at `ClearNode`. At the
  time the device had only 229 MiB free of 16,376 MiB, with the persistent
  daemon and unrelated Ollama workload still resident; do not interpret this as
  an effect-parity result.
  Consequently the web_rect and light-leak strict runtime gates, a full
  post-rebuild image corpus run, and same-SHA GPU/NVENC certification remain
  OPEN. Do not count this resource-exhausted attempt as a pass.

## Local implementation progress — 2026-10-10 (DirectionalBlur native route)

- Added the native DirectionalBlur route (census 25→26): `shaders/directional_blur.comp`
  (uniform box taps along the host-computed unit direction, Clamp-edge bilinear
  matching `sampling::Sampler2D`, raw premultiplied accumulation),
  `GpuKernelId::DirectionalBlur` pipeline, `directional_blur_surface` backend op,
  and `try_native_full_frame_directional_blur` lowering (full frame only, finite
  angle, length in (0, 64], taps in [2, 64]; length 0 and samples 1 forward the
  input untouched; partial clips fail closed). Strict and non-strict
  EffectStackNode hooks are wired; the capability row advertises
  `NativeZeroReadback`.
- The strict software/Vulkan oracle covers angles 0/37/90/180/−45/123 degrees,
  fractional lengths, explicit and auto-derived tap counts, the length-0 and
  samples-1 identity forwards, transparent/fractional-alpha/HDR content, and
  fail-closed rejection (length 65, samples 65, non-finite angle/length,
  partial clip). Full local evidence on llvmpipe: `chronon3d_backend_registry_tests`
  99/99 (1,913,600 assertions), `chronon3d_render_graph_tests` 421/421 (19,189 assertions,
  including the 26-native census), `chronon3d_effects_tests` 275/275 (5,532,872 assertions),
  `chronon3d_shader_abi_consistency_tests` 17/17. Same-SHA GPU-hardware
  certification remains pending (remote runner billing blocker).
- Two CPU defects were fixed as the cause of oracle mismatches, both against the
  documented contract: `generate_taps` ignored auto-derive for non-cardinal angles
  (`samples <= 1` returned identity instead of `max(2, ceil(length/2))`, while the
  header and the axis fast paths define 0 as auto; `samples == 1` remains the pinned
  single-tap identity), and all three blur paths sampled transparent out-of-bounds
  instead of the documented Clamp edges (now via `sampling::Sampler2D`). The
  centroid test expectation was corrected to the true invariant (texel-centre
  10.5, provably preserved by symmetric taps and weights; the old 10.0 only held
  under the identity bug). The catalog-count test was updated 47→48 for the
  already-landed RoughenEdges entry, and the kernel-count gate 17→18.
- Remaining unsupported wave (22): Fake3DWave, RadialBlur, Stroke, FractalNoise,
  FourColorGradient, LightRays, Lut3D, Curves, CornerPin, Twirl, Bulge, MeshWarp,
  TurbulentDisplace, WaveWarp, Ripple, DisplacementMap, Halftone, LensDistortion,
  LuminanceResponse, FoldWarp, CreaseShading, RoughenEdges.

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
