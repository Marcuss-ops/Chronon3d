# Cinematic particles and bokeh environment

**Stato (2026-11-12): PARTIAL — contratto V3 esteso e quattro demo AE-grade
completate; parità GPU locale 5/5 PASS su RTX A4000, certificazione same-SHA e
matrice full-budget/clipping/bokeh-depth restano aperte.**

## TODO — cosa manca (agg. 2026-10-28)

- [ ] **Parità software/Vulkan byte-identica** — logica: il software kernel è l'autorità di placement/compositing; il replica Vulkan deve essere parità-certificato prima della promozione, altrimenti la capability resta fail-closed (`UnsupportedCapability`). Dove: emitter environment nel lowering `particle_emitter` + dispatch Vulkan strict-native. Gate: test di parità byte su casi full-budget/clipping/bokeh-depth. **2026-11-12:** uniform V3 (`dynamics`, `over_life_lut`) ricostruito; selection `Vulkan *particle*` 5/5 casi, 62/62 assertion su RTX A4000. La matrice completa e la certificazione same-SHA restano aperte.
- [ ] **Golden + random access** — logica: golden software per empty/full-budget/clipped/bokeh-depth, e rendering random-access byte-identico su conteggi worker diversi. Dove: `tests/` (fingerprint + golden), schema V3 (`environment`, `depth_range`, `bokeh`). Gate: golden verdi + determinismo cross-worker.
- [ ] **Certificazione su host Vulkan** — logica: il promotion gate richiede l'esecuzione su host Vulkan reale (llvmpipe non basta per la promozione). Gate: run same-SHA su host Vulkan; fin lì fail-closed.

## Scope

Add a bounded deterministic environment-particle mode with depth-driven bokeh.
The existing `particle_emitter` lowering is extended for software rendering;
native Vulkan parity remains a promotion gate.

## 2026-11-13 — GPU full-budget synchronization

The GPU-generated emitter now inserts a compute image barrier between its
512-instance raster chunks, matching the existing CPU-uploaded layer-batch
path. Without it, the 4096-particle clipped bokeh case produced a maximum
channel difference of 0.999799 against that path on RTX A4000. After the fix,
64-, 512- and 4096-particle cases pass the existing `2e-4` float tolerance
(45/45 assertions); the full `Vulkan *particle*` selection passes 6/6 cases,
107/107 assertions. The particle sampler passes 10/10 cases, 4991 assertions.
These are local results on a dirty checkout. Both sides of the new comparison
use Vulkan layer-batch rasterization, with CPU versus GPU particle sampling;
they are not software-renderer versus Vulkan byte parity, golden/random-access
coverage, or clean same-SHA certification. Those gates remain open.

Software raw-frame golden and direct-seek gates have since been added in
`scripts/ci/check_particle_environment_gate.py` with four 64x64 fixtures:
empty, 4096-particle full budget, clipped, and bokeh/depth. Locally all four
pass SHA-256 golden comparison, byte-identical 1/4-worker rendering, and direct
frame 2 versus sequential frame 2. The fresh software graph builder now applies
the current sampled particle state, matching the refresh path; without it the
clipped and bokeh seek comparisons failed. These are local dirty-tree results,
not clean-SHA CI certification.

An independent raw full-render probe with 30 bokeh particles, zero velocity,
and a 64x64 canvas still fails software/Vulkan parity: software has 67 nonzero
alpha pixels (x 0..27, y 0..29), Vulkan has 224 (x 3..59, y 2..61), with
maximum byte difference 244. The software result appears shifted by half a
canvas relative to Vulkan; raster/coverage differences remain to be resolved
as well. Do not promote the GPU or close the parity gate on the basis of the
sampler-only test.

## Proposed bounded contract

- Authored fields: `seed`, `count`, `bounds`, `depth_range`, `size_range`,
  `speed`, `turbulence`, `bokeh`, and a bounded color ramp.
- Hard budget: at most 4096 particles per environment and at most 64 MiB of
  canonical particle state per frame. Counts outside the budget fail closed.
- Determinism: particle positions derive from `(seed, particle_index,
  absolute_frame)`; no mutable RNG state and no dependence on worker order.
- Depth: store a normalized fake depth per particle; use it only for scale,
  opacity, and existing camera-driven DoF until a shared 3D environment model
  exists.
- Authority: software kernel defines placement, compositing, and frame output;
  Vulkan must provide a parity-certified replica or report
  `UnsupportedCapability`.
- Audio reactivity is excluded until the `ChrononMotion3D::OfflineBaking` PCM
  interface is available in this checkout.

## Acceptance gates

- Schema and decoder reject non-finite values, invalid ranges, and over-budget
  counts before fingerprinting.
- Fingerprint includes every authored field and seed.
- Random-access frame rendering is byte-identical across worker counts and
  renderer reuse.
- Software golden covers empty, full-budget, clipped, and bokeh-depth cases.
- Vulkan parity is tested on a Vulkan host before native promotion; otherwise
  the capability remains fail closed.

## 2026-11-12 — V3 particle contract and AE demo

**PARTIAL — CONTRACT + DEMO COMPLETE; FULL PARITY CERTIFICATION OPEN.**

- Added neutral-default V3 fields: `air_drag`, `emissive`, `emitter_velocity`,
  `size_over_life`, `opacity_over_life`. CPU sampler, Vulkan uniform/shader,
  decoder, fingerprint and V2 projection are aligned; legacy plans keep their
  fingerprint bytes.
- The Vulkan emitter now packs a 256-entry over-life LUT and quadratic drag;
  the shader mirrors the CPU sampler (sub-frame emission, curl/depth, emissive,
  bokeh energy conservation).
- Four 1920x1080, 24 fps, 240-frame/10 s clips were rendered strict-native with
  NVENC on an RTX A4000 and uploaded to Drive folder
  `AE_Particles_20261112` (36 files, SHA-256 verified).
- Focused GPU selection `Vulkan *particle*`: 5/5 cases, 62/62 assertions PASS
  with the NVIDIA ICD. Sampler: 10/10 cases, 4991/4991 assertions. C ABI
  particles: 11/11 cases, 159/159 assertions.
- Authoring finding: the quadratic drag term `v / (1 + drag*|v|*t)` freezes
  demo-scale particles at the spawn point for coefficients `0.35`-`2.2`; the
  demo plans therefore use `air_drag: 0.0`, while drag remains covered by the
  sampler tests.
- Renderer findings: a full-screen `adjustment` layer with `bloom` produced an
  all-white frame on this path, and per-layer glow was clipped to the particle
  bounds. Both were removed from the final demos.
- Still open: same-SHA certification on a clean host and the full
  full-budget/clipping/bokeh-depth byte-parity matrix. Do not mark the ticket
  complete from the focused test alone.

## Implementation status (2026-10-11)

`PARTIAL — SOFTWARE PATH IMPLEMENTED; GPU PARITY OPEN`.

- Emitter fields `environment`, `depth_range`, and `bokeh` are decoded in the
  V3 RenderPlan schema and validated. Environment emitters cap at 4096 samples
  and 64 MiB canonical sample state; ordinary emitters retain their existing
  limit.
- Depth is stateless and derived from seed/index/frame. Bokeh changes particle
  size and opacity, and environment depth adds deterministic scene placement.
- The V3 fingerprint includes the new fields while legacy plans retain their
  existing fingerprint bytes. V2 projection strips the V3-only fields.
- Software/Vulkan byte parity, a full-budget golden, clipping golden, random
  access worker-count parity, and native Vulkan fail-closed behavior still need
  implementation and verification. Do not mark this ticket complete until
  those gates pass on the required hosts.
