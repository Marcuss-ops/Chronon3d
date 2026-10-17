# Text animation and Field2D backlog closure

Track the requested RenderPlan deltas across T1–T6. All six deltas are
implemented in this worktree; the evidence below records the focused acceptance
checks performed for this closeout.

## Acceptance

- [x] T1: text animator transform, baseline, fill, background, and stroke
  properties evaluate animated tracks per frame; character offset remains
  static because changing it requires re-shaping.
- [x] T2: stagger supports edge, spatial, radial and center orders, plus
  grouped layer-level offsets.
- [x] T3: stroke cap, join and bounded open-path taper are carried through the
  RenderPlan and software rasterizer; unsupported Vulkan taper fails closed.
- [x] T4: deterministic Field2D is sampled for shape fills, masks,
  displacement maps and fractal noise, with focused runtime, fingerprint and
  bounds tests. Field sampling is analytic and allocation-free
  (bounded to 16 generator octaves and 8 operators), so it does not create a
  raster cache or a second cache family.
- [x] T5: text selectors expose seeded coherent noise and index order with a
  bounded `index_step`; decoder, CPU evaluation, fingerprint and GPU fallback
  behavior are covered.
- [x] T6: ordered masks can sample the same-frame rendered alpha of an
  existing prepared text layer; source validation, dependency-cycle checks,
  fingerprints, and transformed source coordinates are covered.
- [x] Focused acceptance suites pass: core selector/stagger/Field2D 24 cases /
  202 assertions; text animator properties 5 / 17; C ABI selector/Field2D/mask
  12 / 97; effects Field2D 2 / 626; renderer mask sources 2 / 12; scene
  Field2D/fill 2 / 57; stroke cap/join/taper 11 / 44; render-graph text source
  sampling 1 / 4. Schema JSON and `git diff --check` pass.

## Progress

- **2026-09-29:** T4 and T5 focused acceptance passed.
- **2026-09-29:** T6 now has a bounded `type: "text"` / `source` plan shape,
  text-layer/reference/cycle validation, fingerprint coverage, and a graph
  input path that samples the source layer's same-frame prepared text alpha.
  Decoder mask tests pass (6 cases / 56 assertions), graph source sampling
  passes (1 case / 4 assertions), and ordered mask composition passes (1 case /
  8 assertions), including transformed source coordinates.

## Triage 2026-10-17 — scale_anim centroid drift (test §9.2, albero linux-video-test)

`tests/text/test_video_animation_curves.cpp` §9.2 (Scale) fallisce su
`d01/d12 < 3.0 px`: misurato `|f0-f15| = 3.54268`, `|f0-f30| = 0`. Il raster agli
estremi scala 1.0 (f0 e f30) è **identico al pixel**: la pipeline è
deterministica. Il drift nasce dal pivot, non da nondeterminismo:

- il testo "SCALE" è piazzato Center/Middle su (cx, cy) e scala ancore
  al centro canvas;
- il centroide d'inchiostro dei glifi non coincide con quello geometrico
  (asimmetria verticale del contorno, ≈7 px a scala 1);
- Uniform scale attorno al centro P manda il centroide da P + o a P + 1.5·o:
  drift = 0.5·|o| = 3.54 px, sopra la soglia spec.

Direzione di fix (nel motore testo, NON sull'asserzione): ancorare la scala
all'inchiostro-centroide (o compensare o) perché la spec §9.2 verbatim valga per
glifi asimmetrici. Le asserzioni d01/d02/d12 restano 3.0 px: non toccarle.
Effetto zero-copy: f0 == f30 già garantito, il vino per il reuse frame-driven
resta intatto.
