# TICKET — Temporal film grain freeze + halo-effect blackout (vulkan-video host path)

Data: 2026-10-14. Contesto: VintageDocumentaryTitle_01 (5s, 1920x1080@30, GPU).
Stato: workaround verificati in produzione; fix C++ parziali landed nel worktree
(non committati). Residuo aperto per gli owner engine.

## 1. Grana animata congelata su layer statici (trovato + mitigato)

Sintomo: `noise` con `animated=true` su un layer senza track di animazione
produce frame byte-identici (verificato via PNG: md5 uguali; via mp4 ad alto
bitrate: std delle differenze = 0.000).
Repro minima: `tB6` — color layer + `noise{amount 0.3, seed 42, animated true}`.

Fix landed (worktree, non committati):
- `include/chronon3d/scene/model/core/effect_stack.hpp`: nuovo predicato
  canonico `effect_stack_has_temporal_effect()` (Echo + Noise animato).
- `src/effects/effect_catalog.cpp`: factory → `frame_variant_cache("noise-animated")`
  (precedente: Echo).
- `include/chronon3d/render_graph/nodes/effect_stack_node.hpp`:
  `has_temporal_effect()` delega al predicato (guardia merge optimizer).
- `src/render_graph/core/scene_hasher.cpp`: `layer_is_static[_at]` + dynamic
  fingerprint considerano gli effetti temporal (gates del riuso precoce).
- `src/compiler/graph_builder/graph_builder_static_analysis.cpp`:
  `check_static_recursive_impl` considera gli effetti temporal (policy dei composite).

Verifica: con una track qualsiasi sul layer (anche opacity costante 1.0→1.0)
la grana varia davvero per frame — pattern indipendenti, firma
corr(d1,d2)=0.5 su tre campi iid, std≈40-84 proporzionata all'amount.

Residuo (per gli owner): il caso puro-statico resta frozen nonostante i 4 fix —
la frame-dipendenza non si propaga ai consumer bypassati (composite zero-copy):
il nodo rumore frame-variant pubblica il digest di default
(`cache_key_required=false`) e l'output serve cache_hit. Serve propagazione
della key-requirement attraverso i nodi bypassati, oppure classificazione
temporale nel `compute_cache_key_requirement` reverse sweep.
Workaround di produzione (usato nel piano Title01): traccia opacity costante
1.0→1.0 su ogni layer con effetti animati — impatto visivo nullo, valutazione
per-frame garantita.

## 2. Effetti con halo azzerano il layer su vulkan-video (trovato, NON fixato)

Sintomo: `gaussian_blur`, `bloom`, style `glow` su QUALSIASI layer via
`--backend vulkan` (video, host feed) producono frame neri (mean pixel 0.0),
senza errori nei log. Su software gli stessi piani rendono correttamente.
Repro minima: color layer + `gaussian_blur{radius 8}` → nero.
Sopravvivono su vulkan: `chromatic_aberration`, `noise`, `vignette`,
text `shadow`, particles, track opacity/position/scale.
Ipotesi: gli effetti che espandono i bounds (scratch con halo) falliscono sul
path vulkan-video host-feed; quelli strettamente in-place funzionano.
Workaround di produzione (usato nel piano Title01): halation via text
`shadow` bianca (blur 6, opacity 0.25, offset zero) + ghosting via
chromatic ±0.8. Niente blur/bloom/glow finché il path non è certificato.

## 3. Track blur animata: fallimento distruttivo (trovato, NON fixato)

`[native-blur] create_surface: native surfaces are not supported` sul backend
software; il layer sparisce invece di degradare al fallback. Un fallimento
effetto non dovrebbe mai cancellare il layer (fail-closed sul singolo effetto,
non sul contenuto). Workaround: ingresso solo via opacity + blur statico.

## 4. Still/PNG-sequence valuta sempre il frame 0 (trovato, NON fixato)

`--frame N` / `--frames A-B` con output PNG ignorano il range: scrivono N file
tutti valutati al frame 0 (opacity animata congelata). Il path video avanza
correttamente. Impatto: anteprime still inaffidabili; usare estrazione ffmpeg
dal video.

## Test eseguiti (build linux-video-fast-dev, ctest)

- PASS: chronon3d_effects_tests, chronon3d_render_graph_tests,
  chronon3d_cache_tests (162/162), chronon3d_compositor_tests (70/70),
  55/55 casi cache/reuse/static/temporal/noise/grain/fingerprint di core,
  WP1 scheduler determinism 7/7.
- FAIL pre-esistenti/ambientali, fuori dal perimetro del diff (stack senza
  effetti temporal → percorsi identici): prepared-render-job RadialBlur (1),
  legature opentype cpu (8), `CHRONON3D_CLI_BINARY` mancante (2).
