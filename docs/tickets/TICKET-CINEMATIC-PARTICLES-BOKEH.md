# Cinematic particles and bokeh environment

**Stato (2026-11-13): PARTIAL — contratto V3 esteso, quattro demo AE-grade e
matrice byte-parity software/Vulkan 7/7 PASS localmente su RTX A4000 (golden,
1/4 worker, seek diretto e Vulkan strict-native); resta aperta la
certificazione same-SHA su host pulito.**

## TODO — cosa manca (agg. 2026-11-13)

- [x] **Parità software/Vulkan byte-identica** — logica: il software kernel è l'autorità di placement/compositing; il replica Vulkan deve essere parità-certificato prima della promozione, altrimenti la capability resta fail-closed (`UnsupportedCapability`). Dove: emitter environment nel lowering `particle_emitter` + dispatch Vulkan strict-native. Gate: test di parità byte su casi full-budget/clipping/bokeh-depth. **2026-11-12:** uniform V3 (`dynamics`, `over_life_lut`) ricostruito; selection `Vulkan *particle*` 6/6 casi, 107/107 assertion su RTX A4000. **2026-11-13:** matrice 7/7 byte-identica locale, inclusi blend normal/screen; certificazione same-SHA ancora aperta.
- [x] **Golden + random access** — logica: golden software per empty/full-budget/clipped/bokeh-depth, e rendering random-access byte-identico su conteggi worker diversi. Dove: `tests/` (fingerprint + golden), schema V3 (`environment`, `depth_range`, `bokeh`). Gate: golden verdi + determinismo cross-worker. **2026-11-13:** sette fixture con SHA-256 golden, 1/4 worker e seek diretto verdi localmente in `scripts/ci/check_particle_environment_gate.py`.
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
coverage, or clean same-SHA certification. Byte parity and golden/random-access
were subsequently closed locally in the 2026-11-13 matrix below; same-SHA
certification remains open.

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
maximum byte difference 244. A subsequent 2D multi-source placement fix aligns
the sample with `SourceNode::effective_matrix`, on both fresh construction and
refresh. On the same probe both backends now cover 224 pixels with identical
bounds (x 3..59, y 2..61); 636 bytes differ, max 54 and mean 0.523/255.
Two translated probes have alpha-mask differences of 2 and 0 pixels. The
projected, native-3D and screen-space cases retain their existing placement
resolver. A further correction prevents particle bokeh alpha being multiplied
twice by the software gradient and node opacity. In the same static 30-particle
probe this reduces the residual to 33 bytes differing by at most 1/255, with
identical alpha coverage. `scripts/ci/check_particle_bokeh_cpu_vulkan.py`
also checks a visible isolated bokeh particle against strict-native Vulkan
with maximum byte delta 1; it passes locally on RTX A4000. Initially the moving full-budget, clipped and bokeh fixtures failed byte
parity: native GPU drew motion capsules while software drew discs, and opaque
software paths differed in raster coverage. Both sides now match byte-for-byte
in the 2026-11-13 matrix below. Do not promote the GPU or close the parity gate
on the basis of the sampler-only test.

## 2026-11-13 — byte-parity matrix, blend coverage and residuals

`scripts/ci/check_particle_environment_gate.py --vulkan` passes **7/7** cases on
the local RTX A4000 with the NVIDIA ICD. Every case checks the software
SHA-256 golden, byte identity between 1 and 4 render workers, direct frame 2
against sequential frame 2, and byte identity against strict-native Vulkan,
including Vulkan direct seek.

| Case | Coverage | Result |
|---|---|---|
| `empty` | no particles | PASS |
| `full_budget` | 4096 particles | PASS |
| `clipped` | emitter beyond the canvas | PASS |
| `bokeh_depth` | depth-driven bokeh | PASS |
| `animated_color` | colour ramp, curl, drag, over-life curves | PASS |
| `blend_normal` | particle layer over a dim particle backdrop | PASS |
| `blend_screen` | screen-blended particle layer over a backdrop | PASS |

The blend fixtures use a second, sparse dim particle layer because blending
against a transparent canvas is a no-op and cannot exercise the mode. The GPU
instance path keeps per-instance `Normal` and the authored layer blend is
applied at compositing; the fixtures confirm that composition is byte-identical
to software for `normal` and `screen`.

Edge probes outside the required matrix, pinned by
`scripts/ci/check_particle_parity_edges.py`:

- **Native-3D rotated layer (`enable_3d: true`, z = 27°): fixed.** The CPU
  instance path transformed the emitter velocity with the transposed matrix
  (`m[0][1]`/`m[1][0]`), which is only correct for axis-aligned layers. The
  column-major transform makes the probe byte-identical: 0 bytes, previously
  3,233 with max 246. The edge gate requires exact equality.
- **Scaled moving bokeh (`scale: [0.7, 1.3]`):** one colour byte differs by
  1/255 over three frames; the alpha mask and bounds are identical.
- **Additive blend:** 1-2 bytes differ by at most 1/255, while `normal` and
  `screen` are byte-identical. The divergence is a float quantization boundary
  in the compositing/trail coverage, not particle placement.

The two 1/255 probes are pinned with an explicit tolerance (maximum 1) rather
than claimed as byte-identical; they remain outside the ticket's required
matrix. These are local dirty-tree results. Same-SHA certification on a clean
host is still open, so native promotion stays fail-closed.

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
