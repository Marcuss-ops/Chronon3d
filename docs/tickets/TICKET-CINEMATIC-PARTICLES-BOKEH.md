# Cinematic particles and bokeh environment

**Stato (2026-11-14): PARTIAL — contratto V3 esteso, quattro demo AE-grade,
matrice byte-parity software/Vulkan 7/7 PASS localmente su RTX A4000 (golden,
1/4 worker, seek diretto e Vulkan strict-native), sprite billboards supportati
e lane emitter GPU-native ora collegata al `SourceNode` di produzione con
fallback CPU e clip bounded; resta aperta la certificazione same-SHA su host
pulito.**

## 2026-11-14 — delivery MP4 1080p e verifica native-2.5D

**PASS locale di delivery; non è certificazione same-SHA.**

- Build release `chronon3d_cli` con `-j 8` riuscita (322/322 step); il binario
  include il wiring GPU particellare e il clip fix. Build type Release,
  backend Vulkan, ICD NVIDIA RTX A4000.
- Sei clip `particle_lab` renderizzate a 1920×1080, 24 fps, 240 frame,
  10.000 s: dust, embers, ash fall, bokeh orbs, chalk burst, light motes.
  Per la leggibilità a 1080p, 03/05/06 usano copie di consegna dei piani con
  particelle più grandi/dense (03 e 06) e burst loopato (05); i piani
  sorgente non sono stati modificati.
- La clip native-2.5D `philosophers_20s_particles_text_v14_clean_native25d`
  è stata renderizzata sui primi 240 frame (10 s). Timing sidecar:
  `execution_path=full_graph_native`, `gpu_native_surface_frames=240`,
  `cpu_pixel_readback_bytes=0`, `gpu_readback_bytes=0`,
  `software_fallback_nodes=0`.
- Tutti i 7 MP4 sono stati validati con `ffprobe` (1920×1080, 24/1, 240
  frame, 10.000000 s, H.264/yuv420p) e decodificati per intero con
  `ffmpeg -f null -`; gli SHA-256 sono nel `manifest.tsv` e in `SHA256SUMS`
  caricati su Drive.
- Nel probe particellare strict il graph costruisce il marker dell'emitter
  (`[gpu-particles-build] ... instances=320`) e non emette il diagnostico del
  fallback CPU (`[gpu-particles-draw]`), coerente con il ritorno anticipato
  del lane compute dopo il dispatch riuscito.

Consegna Drive (sottocartella `Particle_Lab_Native25D_20261114`):

- [Cartella Drive](https://drive.google.com/drive/folders/1zfVp6CvkDz0usbrJDG04UcISldSAXF-g)
- [01 — Cinematic dust](https://drive.google.com/file/d/1Eot-5pwLsCXuI3O5F8ZxomweGguqNWLd/view)
- [02 — Embers](https://drive.google.com/file/d/1rmThGcXsj5Wej3eo9TXoCAkDtWltnAGQ/view)
- [03 — Ash fall](https://drive.google.com/file/d/1y55dDrZ1WgNx9S44cEzJ1IbtwJHLCBUu/view)
- [04 — Bokeh orbs](https://drive.google.com/file/d/1rVG0lmUUWJpxLXWHNGKsGq8GleZMKP2v/view)
- [05 — Chalk burst loop](https://drive.google.com/file/d/1Wqpv_D3xFtSC8idXby4daqqtMfKjidV1/view)
- [06 — Light motes](https://drive.google.com/file/d/1UgkO8ocZ4jb8KlV2F68e2XZIH_0530DB/view)
- [07 — Native-2.5D philosophers](https://drive.google.com/file/d/18UNtEnq62LStK_FUQz4ftpEcqnGYfpT-/view)
- [Contact sheet](https://drive.google.com/file/d/17IKw8d-4WGpOK5PCo-xukSxMLemumPt_/view)
- [README](https://drive.google.com/file/d/1KvAU0NRqp4svQtx7Th1nthG6ORju__Dn/view)

Limite residuo: il compute emitter GPU continua a coprire solo layer
screen-space; i layer particellari projected/native-3D restano sul fallback
CPU a istanze e sono esclusi dal lane compute. La clip native-2.5D applica la
camera a immagini/testo e usa particelle screen-space. La certificazione
same-SHA su host pulito resta aperta.

## 2026-11-14 — sprite billboards e wiring GPU-native di produzione

**PASS locale su RTX A4000; path emitters restano fail-closed.**

- `particle_emitter.comp` accetta il flag `4=sprite` e genera un billboard
  affine con scala, rotazione, tint e opacity per particella; il source è la
  superficie sprite preparata, campionata da `layer_batch.comp`.
- `VulkanBackend::execute_gpu_particle_batch` accetta emitter con
  `sprite_asset` non vuoto e usa la superficie sorgente come texture del
  billboard. Gli emitter `EmitterShape::Path` continuano a restituire
  `UnsupportedCapability` prima di risolvere le superfici.
- Il test sprite usa una sorgente `Rgba32Float` 1×1, un billboard 8×8 e una
  singola particella deterministica su canvas 64×64; l'oracolo CPU è
  `PrimitiveKind::ParticleSprite`. Energia CPU/GPU 80/80, `max_difference = 0`.
- Il test path verifica il fail-closed con handle non validi e
  `EmitterShape::Path`.
- `SourceNode` seleziona il lane GPU-native in modalità strict quando
  l'emitter non è `Path`, `count` è in `(0, 4096]`, il layer non ha
  proiezione camera/native-3D e il backend espone native surfaces e layer
  batch. Al compute passano la matrice di placement (meno la trasform del
  marker) e l'opacità del layer; gli sprite usano il path risolto.
- La superficie sorgente disegna le particelle con blend `Normal`; il blend
  dell'emitter viene applicato una sola volta dalla composizione del layer.
- Clip: il `clip_rect` del nodo viene convertito in coordinate locali della
  surface e il dispatch del layer batch è limitato a `[x0,x1) × [y0,y1)`
  tramite `dispatch_end`; i pixel fuori clip restano trasparenti. `Path`,
  projected/native-3D e backend senza kernel restano sul fallback CPU a
  istanze campionate; gli errori reali del kernel non vengono mascherati dal
  fallback.
- Test di produzione: `SourceNode selects the GPU particle emitter in strict
  native mode` usa un'istanza CPU deliberatamente fuori schermo e un emitter
  visibile. Se il lowering ignorasse l'emitter, il frame sarebbe vuoto. Esito
  1/1 casi, 7/7 assertion; marker lowering 3/3 casi, 10/10 assertion.
- Test clip/hash: `SourceNode clips the GPU particle emitter and matches the
  CPU fallback` confronta il clip GPU con l'oracolo CPU (1/1, 17/17; fuori
  clip alpha 0). `RenderNode content hash tracks the GPU particle emitter
  payload` verifica optional, seed e count (1/1, 4/4).

Verifica eseguita:

```
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ./build/chronon/linux-video-release/tests/chronon3d_backend_registry_tests \
  --test-case='Vulkan *particle*'
# 10/10 test cases, 143/143 assertions PASS

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ./build/chronon/linux-video-release/tests/chronon3d_backend_registry_tests \
  --test-case='GPU particle marker*'
# 3/3 test cases, 10/10 assertions PASS

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ./build/chronon/linux-video-release/tests/chronon3d_backend_registry_tests \
  --test-case='SourceNode selects the GPU particle emitter*'
# 1/1 test cases, 7/7 assertions PASS

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ./build/chronon/linux-video-release/tests/chronon3d_backend_registry_tests \
  --test-case='SourceNode clips the GPU particle emitter*'
# 1/1 test cases, 17/17 assertions PASS

./build/chronon/linux-video-release/tests/chronon3d_scene_tests \
  --test-case='RenderNode content hash tracks the GPU particle emitter payload'
# 1/1 test case, 4/4 assertions PASS
```

Build del target `chronon3d_backend_registry_tests` con `-j 12`; configure con
mold rilevato, nessun OOM e ~18 GiB disponibili all'avvio del build finale.

| Capability | Stato | Evidenza |
|---|---|---|
| Emitter procedurale (box/ellipse/edge, bokeh/environment) | PASS locale | parità CPU/GPU e matrice 7/7 del 2026-11-13 |
| Sprite billboard da superficie sorgente | PASS locale | nuovo test, sorgente 1×1, max diff 0 |
| Wiring GPU-native nel `SourceNode` | PASS locale | dispatch reale, istanza CPU fuori schermo + emitter visibile, 1/1 |
| Clip del layer | PASS locale | GPU/CPU 1/1, 17/17; fuori clip trasparente |
| Hash dell'emitter | PASS locale | optional/seed/count, 1/1, 4/4 |
| Transform/opacità del layer | PASS locale | oracolo CPU, max diff 1e-6, bbox identico |
| Path emitter | fail-closed e fallback CPU | nuovo test negativo, `UnsupportedCapability` |
| Blend Normal/Screen | PASS locale | matrice 7/7 del 2026-11-13 |
| Blend esotiche | fuori scope / fail-closed | validazione pubblica |

Limiti: il risultato è su worktree dirty, non è certificazione same-SHA su host
pulito; il compute non copre ancora `Path` o projected/native-3D, che
restano sul fallback CPU; il caso sprite copre sorgente 1×1, scala 1 e
rotazione 0 (il path affine esistente copre transform/tint/opacity); le blend
mode oltre Normal/Add/Multiply/Screen/Overlay restano fail-closed.

## TODO — cosa manca (agg. 2026-11-14)

- [x] **Sprite billboards GPU-native** — logica: l'emitter compute accetta `sprite_asset`, produce un billboard affine e campiona la superficie sprite; i path restano fail-closed. Dove: `particle_emitter.comp`, `vulkan_backend_particle_ops_private.cpp`, `vulkan_backend_surface_api_effects_private.cpp`, test registry. Gate: test sprite + test path. **2026-11-14:** 10/10 casi, 143/143 assertion PASS su RTX A4000; certificazione same-SHA ancora aperta.
- [x] **Wiring GPU-native nel render graph di produzione** — logica: `SourceNode` seleziona l'emitter compute in strict mode con condizioni bounded, clip locale e fallback CPU per Path/projected/native-3D/backend senza kernel; matrice di placement, opacità, sprite e clip sono passati al compute. Dove: `source_node_execute.cpp`, `render_node.hpp`, materializer, hash transizioni, API Vulkan, `layer_batch.comp`. Gate: dispatch reale + parità clip/sprite/transform. **2026-11-14:** particle 10/10 (143 assertion), marker 3/3 (10 assertion), SourceNode dispatch 1/1 (7 assertion), clip 1/1 (17 assertion) su RTX A4000; same-SHA ancora aperta.
- [x] **Hash regression dell'emitter GPU** — `hash_render_node_content_v2` include presenza, seed e count dell'emitter; test scene dedicato. **2026-11-14:** 1/1 casi, 4/4 assertion PASS.
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
