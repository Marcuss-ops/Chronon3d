# Ticket: Gel Text Pipeline in Five Testable Modules

**Status:** OPEN — implementation follows the approved visual gates below.
**Scope:** Canonical Chronon text layout rendered through the native Vulkan path. No external rendering engine and no second font/shaping authority.
**Modes:** Keep `GlossyGelText` (2.5D surface over canonical text coverage) separate from `ExtrudedText3D` (actual caps, counters, bevel/sides and camera depth). They may share shaped glyph IDs, metrics and outline extraction; neither mode may change the other's geometry.

## Required invariants

- Use one source phrase (`calories or ingredients`) and preserve font file/digest, HarfBuzz shaping, size, tracking, baseline, resolution, seed and camera for every compared mode.
- Main validation uses strict-native Vulkan. Software rendering is only a declared geometry/coverage reference. Never conceal GPU failure with CPU fallback.
- First render is one 1280×720 frame, uniform dark background, centered text, raw RGBA sink before encoding. Derive review PNGs with `tools/raw_frame_to_png.py` and its premultiplied-to-straight-alpha conversion. Encoded-video screenshots are not pixel oracles.
- All diagnostic modes share canonical glyph layout and coordinate space. Ablate features with parameters; do not rebuild the phrase as per-glyph layers.
- Record commit and dirty diff digest, GPU/driver, RenderPlan hash, font/shader digests, command, backend/lane, fps, dimensions, GPU timing and raw SHA-256 for each run.
- A visual PASS requires reviewer comparison at 720p. Performance is a separate gate; a shader test does not certify end-to-end 120 fps.

## Modules and visual unit tests

### M1 — Canonical layout, vector contours and distance field

Input: the canonical shaped TextRun, glyph IDs, font spans/variation axes, tracking and placed glyphs. Extract outlines from the same font through FreeType; do not reshape by codepoint. Preserve contour winding and counters. Build composed coverage and validate its distance field against a 2D distance-transform oracle for the union mask before choosing max-per-glyph distance or another approximation.

The atlas range is measured in atlas texels and must be converted to screen-space distance through the actual glyph quad transform. A nominal 8–16 texel range is only an initial test setting, not a universal value for 96-pixel glyphs. Inspect sign, zero crossing, saturation band and opened counters. Layout test also checks metric quads, advances, kerning, bearings and centered run bounds.

**Visual tests:** flat baseline and coverage on black; signed-distance heatmap with open counters and no atlas-cell leakage; compare zero crossing and distance in screen pixels to the composed-mask oracle.

### M2 — Vulkan projection, depth and linear-depth contract

Use the renderer's existing camera/MVP and coordinate conventions. Verify projection, viewport Y orientation, depth range, near/far and depth compare against a known plane and tilted mesh. Do not add a second projection implementation. For camera/DoF work, expose positive camera-space linear depth (e.g. `-viewPos.z` when view space looks down −Z) separately from hardware depth; validate the chosen attachment format and background-depth convention.

**Visual tests:** flat and tilted card, then two overlapping depth planes; inspect depth and linear-depth dumps, near/far clipping, alpha and camera-space placement. DoF stays disabled until these pass.

### M3 — GlossyGelText surface and composition

Build composed SDF/height/normals from canonical TextRun coverage. Expose independent debug output for distance, height, normals/validity, unlit body, specular-only, glow off/on and final optical grade. Preserve the filled core when enabling specular or glow. Validate gradients/normal transform in screen and world space, including the true medial-axis cases.

Glow may use a padded single glyph quad only if the available distance band covers it and the common TextRun geometry is unchanged; otherwise retain a separate ROI-bounded post-text glow pass. In either path, use the declared color space and premultiplied source-over contract. Linear/HDR RGB may exceed alpha for emission; transparent finite pixels must have RGB=0.

**Visual tests:** height ramp, normal heatmap, unlit cyan fill, specular-only, glow off/on with core pixel checks, then final raw image and difference overlay. Do not optimize the optical match until the unlit body and glow gates pass.

### M4 — Depth of field / bokeh

Use camera-space linear depth, a documented CoC model, and a bounded gathering kernel (Vogel spiral is a candidate, not a mandated implementation). Specify foreground/background occlusion policy, transparent-pixel handling, highlight weighting, maximum radius and HDR behavior. Verify stage bandwidth and ROI before performance tuning.

**Visual tests:** CoC false-color visualization on a near/text/far setup; focus plane and blur radius sweep; highlight shape and occlusion checks; compare with DoF disabled. Do not enable this module in the base gel proof.

### M5 — Incremental visual and temporal protocol

Run gates in order; stop at the first failing stage and assign the defect to that module. Keep the existing diagnostic suite IDs where applicable: T00–T08 for geometry/material/glow, T09 topology, T10 tracking/merge, T11 subpixel and zoom, T12 light sweep, T13 camera 2.5D, T14 alpha/transmission, T15 deterministic seek. Extend with depth/CoC checks for M2/M4. Store raw frames, derived PNGs, debug buffers, overlays, frame/hash manifests and reviewer notes.

## Important implementation cautions

- The sample GLSL in the proposal is a design sketch, not production-ready source: reversed `smoothstep` edges are undefined by GLSL; the SDF sign, `fwidth`, texture range, UV orientation and normal transform must match the engine's actual representation; translucent output must obey the render graph's blending contract.
- Vulkan's NDC depth range is [0,1], but Y orientation is determined by the chosen viewport and projection convention. Inspect and test the current Chronon path before negating projection `[1][1]`.
- An SDF atlas is not automatically a replacement for composed TextRun SDF. Glyph quads can leak neighboring atlas cells unless padding, UV range and sampling are validated at every tested scale.
- The suggested Vogel gathering loop is not automatically physically correct: depth-aware gather, foreground/background coverage, premultiplication, HDR and color-space behavior need tests before adoption.
- `ExtrudedText3D` remains a distinct geometry path. Bevel and side-wall tests must include counters and winding; do not claim true 3D from a height normal or camera movement applied to a flat GEL card.

## Stato implementazione — 2026-11-06

Build Vulkan riuscita con `-j14` (target CLI, renderer core, text tile canary e backend registry). Renderer core: 217/217 casi e 73.361/73.361 assertion; canary Vulkan su device reale: 1/1 caso, 105/105 assertion. T11 è PASS di continuità dopo il fix della trasformazione affine: 61/61 frame con coverage; il crop durante lo zoom 10× è viewport clipping e non blank/dropout. T12 è PASS tecnico: la proprietà animabile `light_direction` attraversa RenderPlan v2, compiler, TextRun GPU e shader; sweep Vulkan/raw di 61 frame, con 26.369 pixel RGB modificati nella ROI fra frame 0 e 30. T10 applica il tracking −30 px, ma resta FAIL di leggibilità a quell’estremo; non è una perdita di coverage. Il corpo GEL applica Beer–Lambert RGB selettivo (1.2/0.4/0.05) sotto gli highlight. La composizione T14 ora applica una trasmissione del backdrop dipendente dal canale, derivando lo spessore ottico dal canale alpha GEL rosso: ON/OFF cambia 26.534 pixel RGB nella ROI, zero alpha e zero pixel fuori dalla scritta; il test Vulkan diretto passa 10/10 assertion. Questa è un’approssimazione: coverage antialias, opacità authored e spessore ottico condividono ancora l’alpha source, quindi non è una trasmittanza volumetrica fisicamente separata. Il raw sink con `require_gpu_native` termina il rendering GPU ma fallisce l’export perché non offre ancora readback esplicito del surface device-local. Restano incompleti la certificazione ottica M1/M3 contro riferimento e review umana, il dump/occlusion gate diagnostico M2, la suite visiva completa M4 per forma/occlusione bokeh e P2 performance. T16 ha verificato l’integrazione GEL: tre parole su piani Z distinti, focus interpolato 700→1200→1700, raw Vulkan ON/OFF; i frame differiscono in 34.400–87.521 pixel RGB ai campioni 0/15/30/45/60 (metriche T16). L’estrusione 3D è un percorso distinto e non chiude questi gate GEL.

## Exit gates

1. M1 flat/layout, coverage and distance-oracle tests pass.
2. M2 known-plane projection and depth tests pass.
3. M3 body/core and glow preservation pass, then reviewer accepts 720p optical match.
4. M4 depth/CoC and bokeh tests pass only if DoF is required by the approved reference.
5. M5 deterministic temporal suite passes on the declared device/build. Performance PASS is reported separately from visual PASS.


### M2/M4 — integrazione GEL con piani Z e fuoco variabile (T16) — 2026-11-06

Il render graph esistente pubblica profondità positiva camera-space per pixel coperto nel provenance buffer DoF e calcola il CoC thin-lens dalla camera corrente; `camera_samples` interpola anche il fuoco. T16 esercita tale percorso con tre parole GEL a profondità Z distinte e focus distance 700/1200/1700, confrontando Vulkan/raw DoF ON/OFF per 61 frame. Hash, differenze, piani e PNG campione sono registrati nel run manifest. I raw cambiano da 21.061 a 87.521 pixel RGB nei frame campionati. L’immagine conferma defocus e rack focus; la parola più vicina viene intenzionalmente ingrandita e tagliata dal viewport. Questo è un PASS d’integrazione, non sostituisce il dump falso-colore della depth, i test di occlusione foreground/background né la review della forma circolare dei bokeh highlight.

### Trasmissione del backdrop GEL (T14) — 2026-11-06

`gel_transmission` attraversa il piano RenderPlan, il TextRun materiale, `CompositeNode`, `RenderBackendSurfaceApi` e i compositor Vulkan/software esistenti. L’alpha source-over codifica l’assorbimento rosso, da cui il compositore ricava la trasmissione RGB `(1, 0.4/1.2, 0.05/1.2)` e conserva il contributo premoltiplicato di riflessione/emissione del gel. I piani T14 ON/OFF usano lo stesso checkerboard e layout. Render Vulkan raw a 1280×720: 26.534 pixel RGB diversi nella ROI del testo, zero pixel alpha diversi, zero pixel RGB diversi fuori ROI. Il test Vulkan del compositore passa 10/10 assertion; renderer core 217/217 e canary Vulkan 105/105. Raw, PNG, log e metriche sono registrati in `docs/evidence/gel-text-stress-20261105/` e il confronto è caricato su Drive.

Il dato alpha combina coverage, opacità authored e thickness; la trasmissione per canale è quindi un’approssimazione visiva, non uno spessore RGB separato o un modello volumetrico certificato. `--gpu-hot-path-mode require_gpu_native` rende il surface finale device-local e il raw sink host fallisce senza download esplicito. I render ablation Vulkan normali e il test compositor GPU passano; il gate raw strict-native resta aperto finché il sink non dichiara un readback esplicito. Gli altri exit gates del ticket restano aperti.


### Beer–Lambert RGB sul corpo GEL — 2026-11-06

Lo shader usa ora `exp(-sigma_a * dome_depth)` con coefficienti `(1.2, 0.4, 0.05)` sulla luce diffusa del corpo, senza attenuare highlight speculari o Fresnel e senza cambiare il contratto alpha premoltiplicato. La helper `absorb()` ha test numerici per assorbimento selettivo, spessore nullo e conservazione alpha. Ricompilazione completa `-j14` riuscita (558/558); filtro GEL 8/8 casi, 748/748 assertion; canary Vulkan 105/105. Il raw T06/T12 mostra il cambiamento; T14 ON/OFF cambia RGB nella ROI ma zero pixel alpha. Manifest: `beer-lambert-rgb-render-metrics.json`. Non è ancora implementata una trasmittanza RGB destination-aware del backdrop; non si dichiara simulazione volumetrica fisica.
