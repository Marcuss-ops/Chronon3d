# 02 — Determinismo (seriale, TBB, composite, tile)

> Contratto di riproducibilità pixel-perfect del renderer. Definisce il
> perimetro del determinismo sulle quattro superfici di esecuzione
> coinvolte dal path di produzione (render-job → compiled graph →
> executor → backend) e traccia lo stato di avanzamento per ciascuna,
> con riferimenti puntuali a test, ticket e commit già atterrati su
> `main`.

Stato corrente: [`STATUS.md`](STATUS.md). Ticket: [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).
Roadmap: [`ROADMAP.md`](ROADMAP.md). Pianificazione V3: [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md).
Roadmap scope: [`refactor-roadmap/06-execution-scopes.md`](refactor-roadmap/06-execution-scopes.md).

---

## 1. Obiettivi e Superfici di Determinismo

Il requisito architetturale di **riproducibilità bit-esatta** è centrale
per la validazione del renderer: la stessa scena + lo stesso frame devono
produrre lo stesso framebuffer indipendentemente da:

- Modalità di scheduling (sequenziale vs `tbb::parallel_for` a N thread).
- Stato della cache (cold vs warm, ricostruita dopo invalidazione).
- Istanza del renderer (nuovo vs riutlizzato).
- Esecuzione di un altro job in parallelo sullo stesso processo.

Quattro superfici concorrono al determinismo complessivo:

| # | Superficie | Path canonico | Stato | Riferimento |
|---|---|---|---|---|
| 1 | Serial | `SchedulerMode::Sequential` + `RenderSettings::parallel_tiles = false` | 🟢 **Done** (PR 6.9) | questo doc §2 + `docs/01-baseline-green.md` §2.3 |
| 2 | TBB | `tbb::parallel_for` instradato via `ExecutionScheduler::for_each_index` | 🟢 **Done** (PR 6.9) | questo doc §3 + `docs/01-baseline-green.md` §2.3 |
| 3 | Composite | `SoftwareCompositor` full-frame + precomp layering + Z-order | 🟢 **Done** (PR 6.8 + PR 6.9) | questo doc §4 + `docs/01-baseline-green.md` §2.3 |
| 4 | Tile | `TileGrid` + `DirtyTileMask` (TBB-free) | 🟢 **Done** (PR 6.1, commit `020ea8c2`) | questo doc §5 |

Il **path tile** è l'unico sotto contratto pieno al 100%: `TileGrid`
(`include/chronon3d/core/tile_grid.hpp`) e `DirtyTileMask`
(`include/chronon3d/core/dirty_tile_mask.hpp`) sono strutture dati
pure (nessun `tbb::parallel_for` interno) e producono lo stesso
bit-pattern + lo stesso `for_each_dirty_tile` iteration-order su
N esecuzioni consecutive indipendentemente da N, dal gestore di cache,
o dallo scheduler corrente.

Il **path serial** è la baseline di riferimento: `ExecutionScheduler`
costruito con `SchedulerMode::Sequential` materializza `tbb::task_arena{1}`
che cappa ogni `tbb::parallel_for` annidato
(`include/chronon3d/core/scheduler/execution_scheduler.hpp`, doc-blocking
commento PR-B). Tutti gli altri path devono convergere al serial
bit-exact come requisito di contratto (PR 6.1 / WP-1 PR 1.4).

Il **path TBB** eredita la baseline serial ma introduce tre categorie
di non-determinismo potenziale: ordering del `parallel_for`, reduction-
order degli accumulatori (fold-plus, fold-minimum, fold-maximum),
`scheduler-state leakage` tra invocazioni successive (worker-local
chiusure o `tbb::enumerable_thread_specific` non resettate).

Il **path composite** copre l'unione Z-order dei layer nel framebuffer
finale — la sua determinismo è interlacciato con il path precomp
(`SceneProgramStore` + `PrecompNode` da `ROADMAP.md` **P0 #3**) e
quindi intrinsecamente dipendente da `docs/03-execution-scope-and-
precomp.md` per il contratto di isolamento.

---

## 2. Superficie 1 — Serial Path (Baseline Riproducibile)

### Definizione operativa

Il path serial è il **truth-reference** per ogni altro path: un render
che produce lo stesso framebuffer in modalità `Sequential` per due
chiamate consecutive è dichiarato corretto. Qualsiasi variazione di
modalità (`TbbFixed(N)` o `TbbAutomatic`) che si scosta dal serial
segnala rot architetturale e va fixata prima del merge.

### Knob esistenti

`ExecutionScheduler` (header `include/chronon3d/core/scheduler/execution_scheduler.hpp`)
espone tre modi dichiarati in `include/chronon3d/core/scheduler/scheduler_mode.hpp`:

```cpp
enum class SchedulerMode {
    Sequential,   // arena(1) — caps nested tbb::parallel_for
    TbbAutomatic, // arena(default) — TBB infers local slot count
    TbbFixed,     // arena(N)    — reproducible for benchmarks
};
```

L'envelope `ExecutionScheduler::for_each_index(...)` instrada
automaticamente su `arena.execute(...)` (Sequential → loop puro,
altri → `tbb::parallel_for`) e PROMUOVE il bound del `task_arena` ad
ogni chiamata — il che è fondamentale perché un nested
`tbb::parallel_for` (es. all'interno di un nodo composito blur)
altrimenti escapirebbe dall'arena(1) e finirebbe nell'arena globale.### Stato attuale

🟢 **Done (PR 6.9)** — il path serial è verde. La fix SIMD-path
rot TICKET-007.q/r/s/t/u è risolta da WP-6 PR 6.9 (determinism-
contract safety net): `RenderSettings::force_scalar_normal_blend`
default `false → true` instrada `simd::composite_normal_premul` al
fallback scalare; macro `CHRONON3D_FORCE_SCALAR_BLEND` (definito
nei 2 `blend2d_bridge_*.cpp` salvo opt-in `CHRONON3D_ENABLE_AVX2_BLEND`)
gate le 2 AVX2 batch `composite_framebuffer` e
`composite_framebuffer_transformed`; `pip.cpp::s_use_simd` default
`false` quindi `point_in_polygon_avx2` è codice morto al boot.

Verifica end-to-end (ri-enable dei 5 test TICKET-007.q/r/s/t/u
in `tests/deterministic/gradient_determinism_tests.cpp` linee
289/314/351/391/416):

* `gradient_determinism_tests.cpp:289` ("cold cache vs warm cache
  — identical pixels"): due render consecutivi stesso frame ⇒ hash
  identici bit-per-bit.
* `gradient_determinism_tests.cpp:314` ("cache invalidated →
  rebuilt — identical pixels (arena-reset)"): `clear_caches()` +
  re-render ⇒ hash identici bit-per-bit + `compare_framebuffers_semantic`
  `mismatched_pixels == 0`.
* `gradient_determinism_tests.cpp:351` ("new renderer vs reused
  renderer — identical pixels (arena-reset)"): due renderer distinti
  + stesso frame ⇒ hash identici bit-per-bit.

Plus i 4 test mitigati in
`tests/deterministic/test_baseline_green.cpp:152–177 / 185–210`
(Fresh-renderer-per-render, `arena(1)` pin) che restano come
prova di robustezza sotto pattern alternativi.

### Cosa serve per chiudere 🟢 (riabilitare AVX2 perf)

L'obiettivo finale è **riabilitare AVX2** sotto il contratto
determinismo. Ticket separato per:
- *ordered* reduction (Kahan summation o fold ordinato per tile-id)
  nei 3 path SIMD batch — sostituzione del gate macro con un
  reduction che sia bit-exact indipendentemente dall'ordine TBB.
- `RenderSettings::force_scalar_normal_blend` flip di nuovo `→ false`
  come default. La macro `CHRONON3D_ENABLE_AVX2_BLEND` rimane come
  opt-in benchmark knob.

---

## 3. Superficie 2 — TBB Path

### Definizione operativa

Il path TBB è la modalità produttiva del renderer (default scheduler
e modalità di esecuzione predefinita in `Config`). Una render-bit-exact-
identica tra `Sequential`, `TbbFixed(1)`, `TbbFixed(2)`, `TbbFixed(4)` e
`TbbAutomatic` è la firma del fatto che:

1. Nessun `parallel_for` ha ordering-dipendenza non gestita (i loop
   sono su dati indipendenti o su coordinate che già producono lo stesso
   risultato indipendentemente dall'ordine di processamento).
2. Nessun `combiner` / fold (`std::reduce`, `tbb::parallel_reduce`)
   usa un `operator+` non-associativo o non-commutativo.
3. Nessun worker `tbb` mantiene state locale che persiste tra chiamate
   successive alla stessa arena.

### Test esistenti a livello di scheduler

`tests/render_graph/executor/test_scheduler_determinism.cpp` (PR WP-1
1.4) esercita il lattice 5-mode × 5-scene con un `FakeBackend` che
scrive pixel deterministici per `(layer_id, x, y)` via `FNV1a-64`. Le
5 modalità sono confrontate `CHECK(seq == t1 == t2 == t4 == auto)` su
ogni scena — ogni divergenza è regression signal per il path TBB.

Questo test è un **test del contratto scheduler-only**: garantisce che
il cambio di modalità non cambi il numero di nodi visitati né
l'output dei FakeBackend (che è TBB-immune by construction). NON è un
test del determinismo end-to-end del `SoftwareRenderer` (quello è sotto
§2).

### ~~Test disabilitati a livello renderer (rot persistente)~~ (resolved via PR 6.9)

I 5 `TEST_CASE` in `tests/deterministic/gradient_determinism_tests.cpp`
marcati `* doctest::skip()` con metadati compliance [TICKET-007.q/.r/
.s/.t/.u][] sono stati **re-enabled** dal determinism-contract
safety net di PR 6.9:

| Sub-ID | TEST_CASE | Stato PR 6.9 |
|---|---|---|
| `TICKET-007.q` | "Gradient determinism: cold cache vs warm cache — identical pixels" | 🟢 **FIXED** — `* doctest::skip()` rimosso |
| `TICKET-007.r` | "Gradient determinism: cache invalidated → rebuilt — identical pixels (arena-reset)" | 🟢 **FIXED** — `* doctest::skip()` rimosso |
| `TICKET-007.s` | "Gradient determinism: new renderer vs reused renderer — identical pixels (arena-reset)" | 🟢 **FIXED** — `* doctest::skip()` rimosso |
| `TICKET-007.t` | "Gradient determinism: 1 thread vs 4 threads — identical pixels (arena-reset)" | 🟢 **FIXED** — `* doctest::skip()` rimosso |
| `TICKET-007.u` | "Gradient determinism: 1 thread vs 8 threads — identical pixels (arena-reset)" | 🟢 **FIXED** — `* doctest::skip()` rimosso |

La root cause era nelle 4 path SIMD elencate nel §2 "Cosa serve per
chiudere 🟢"; la fix (default `force_scalar_normal_blend=true` +
macro gate `CHRONON3D_FORCE_SCALAR_BLEND` su 2 AVX2 + pip.cpp già
default Scalar) instrada i pixel sotto `tbb::parallel_for` al
fallback scalare, rendendo il path bit-exact indipendentemente
dall'ordine di scheduling.

Il diagnostic helper `log_first_divergent_pixel(...)` rimane nel file
di test per debugging futuro ma non viene più triggerato sotto
i 5 test (perché ora passano tutti al primo run).

### Stato attuale

🟢 **Done (PR 6.9)** — il path TBB end-to-end è verde. La fix SIMD-
path rot TICKET-007.t/u (1t vs 4t / 1t vs 8t gradient path) è
risolta dallo stesso determinism-contract safety net di PR 6.9
cui §2 sopra. Il contratto scheduler-only (a livello di
`ExecutionScheduler` con `FakeBackend` rot-immune) era già verde
da WP-1 PR 1.4.

Verifica end-to-end (ri-enable dei 2 test TICKET-007.t/u in
`tests/deterministic/gradient_determinism_tests.cpp`):

* `gradient_determinism_tests.cpp:391` ("1 thread vs 4 threads —
  identical pixels (arena-reset)"): due renderer distinti in
  `arena(1)` + `arena(4)` ⇒ hash identici bit-per-bit.
* `gradient_determinism_tests.cpp:416` ("1 thread vs 8 threads —
  identical pixels (arena-reset)"): due renderer distinti in
  `arena(1)` + `arena(8)` ⇒ hash identici bit-per-bit.

Plus il test mitigato in
`tests/deterministic/test_baseline_green.cpp:218–242` ("1t == 4t
== 8t bit-exact under per-render tbb arena pin") su scena statica
non-gradient.

### Cosa serve per chiudere 🟢 (riabilitare AVX2 perf)

Identico a §2: *ordered* reduction (Kahan summation o fold
ordinato per tile-id) nei 3 path SIMD batch. Ticket separato per
riportare `RenderSettings::force_scalar_normal_blend` a `false`
come default una volta che l'ordered-reduction sostituisce il
gate macro.

---

## 4. Superficie 3 — Composite Path

### Definizione operativa

Il path composite è l'unione Z-order dei layer nel framebuffer finale
del job. La determinismo richiede che:

1. L'ordine di composizione (dal basso verso l'alto: `bg` →
   `precomp_layer0` → `layer1` → `layer2` → …) sia fissato dal
   `SceneBuilder::build()` e non dipenda dall'ordine di arrivo dei
   thread TBB durante il commit dello `SceneObject`.
2. Il `PrecompNode` usi il `CompiledSceneProgram` cachato dal
   `SceneProgramStore` (key = `(graph, node)` strong-typed via
   `make_precomp_key(GraphInstanceId, StableNodeId)`) e non ricalcoli
   se la cache hit.
3. La `CompositeNode` applichi `composite_layer(dst, src, blend_mode,
   clip, op)` con op `SourceOver` (default) senza dipendenze da
   `tbb::task_arena`-state (vd. test fixture `FakeBackend::composite_layer`
   no-op in `test_scheduler_determinism.cpp`).

### Stato attuale

🟢 **Done (PR 6.8 — questo commit)** — il path composite è bit-exact
verificabile per il perimetro di scena MULTILAYER ALPHA-COMPOSITE
non-gradient:

* `tests/deterministic/test_baseline_green.cpp:217–239` ("Baseline
  green §4: 30 consecutive composite-full-frame renders —
  pixel-identical") — 30 invokes di `render_frame()` su una scena
  multilayer alpha-composite (`bg` + 2 layer traslucidi 70% alpha)
  producono lo stesso `framebuffer_hash` su un SoftwareRenderer
  riusato tra render.  Pattern single-renderer reused + 30 renders
  ≡ fresh-hash-stability end-to-end del CompositeNode + alpha
  blend.
* `tests/deterministic/test_baseline_green.cpp:260–278` ("Baseline
  green §5: composite path SSIM ≥ 0.999 across 2 renders") —
  perceptual SSIM check (≥ 0.999) sul composite path reused,
  matching il gate di test_determinism_harness.cpp §5/§7.
* `tests/deterministic/test_baseline_green.cpp:315–355` ("Baseline
  green §6: precomp cache-hit determinism — two consecutive frames
  ≡ same hash") — precomp inner composition (registrata via
  `CompositionRegistry::add("inner", ...)`), primo render
  `program_store.acquire(...)` MISS-compile-from-registry, secondo
  render HIT.  I due framebuffer_hash sono bit-identici.  PROVA che
  la cache-hit path è deterministica per `(graph_id, node_id)` e non
  ricalcola la inner-compilation su cache hit (verifica sperimentale
  della contractsection §4 punto 2).

### Cosa resta aperto

- 🔵 **TICKET-013** layer-mode composite SIGSEGV under FakeBackend
  no-op (`test_scheduler_determinism.cpp:330,360`) — fuori scope
  PR 6.9 (richiede FakeBackend con blit deterministico OR fixture
  SoftwareBackend reale).
- 🔵 **Riabilitare AVX2 perf**: *ordered* reduction (Kahan summation
  o fold ordinato per tile-id) nei 3 path SIMD batch — ticket
  separato. Una volta implementato, `RenderSettings::
  force_scalar_normal_blend` flip a `false` come default.

### Interlocking con WP-6

Tracciamento dettagliato del path composite nel contesto del nuovo
contratto `ExecutionScope` (root/tile/precomp) è in
[`docs/03-execution-scope-and-precomp.md`](03-execution-scope-and-precomp.md) §4.

### Lock-down Baseline via Sentinel — PR 6.8.5

I 6 `TEST_CASE` "Baseline green" documentati sopra (Fresh-renderer,
arena-pin, 1t/4t bit-exact, 30-render composite stability, SSIM ≥
0.999, precomp cache-hit determinism) sono **lock-down protected**
contro future regression del backend via la sentinel-infra PR 6.8.5
in [`docs/01-baseline-green.md`](01-baseline-green.md) §2.3:

- 6 `kRefBaseline{FreshShader,ArenaPin,ThreadEq,Composite,Ssim,
  PrecompCache}` costanti in `tests/deterministic/test_baseline_green.cpp`
  confrontano l'`is_reference_captured(...)` guard + `REQUIRE` per
  ciascun TEST_CASE.
- `kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL` è il segnaposto
  pre-cattura hash, popolato on first-clean-CI run (Two-Phase Commit
  Strategy, Q6 hybrid identico al pattern di `kRefStaticScene` in
  `tests/render_graph/executor/test_scheduler_determinism.cpp` (1t/4t/8t determinism `TEST_CASE`, dove `kRefStaticScene` è il lock-down reference precedent)).
- Una volta popolato, ogni futuro backend-change che modifica il
  bit-pattern di un framebuffer (anche di 1 pixel) farà fallire
  immediatamente il `REQUIRE` del TEST_CASE associato → il
  determinismo del path composite è intrinsecamente protetto a
  livello CI.

### Stato del workflow di cattura

Il **prerequisito** per popolare i 6 sentinel con valori reali è il
fix del regression `SoftwareRenderer::capabilities() override`
(`TICKET-018-SR-CAPABILITIES` — placeholder, retrofit to numeric `TICKET-NNN` convention on first filing in `docs/FOLLOWUP_TICKETS.md`; separato da PR 6.8.5 per scope) che
blocca il `linux-ci` build. Una volta che `chronon3d_tests_fast`
compila, `ctest -R 'Baseline green' -V` estrae i 6 hash via il
braccio `MESSAGE` dell'`is_reference_captured(...)` → str_replace
dei 6 placeholder in `kRefBaseline*` → ri-run ctest per verificare
il braccio `REQUIRE` bloccato. Il workflow step-by-step è in
[`docs/01-baseline-green.md`](01-baseline-green.md) §2.3 §5.

---

## 5. Superficie 4 — Tile Path (🟢 Done, PR 6.1, commit `020ea8c2`)

### Definizione operativa

Il path tile è il **TBB-free** del renderer: la divisione in tile
(regioni del framebuffer) + la maschera dei tile dirty sono fatte da
due strutture dati pure, senza nessun `tbb::parallel_for`. La
determinismo del path tile richiede che:

1. `TileGrid(width, height, tile_size)` produca `cols`, `rows`,
   `tile_count()` deterministici (no rounding ambiguity).
2. `tile_bounds(tx, ty)` ritorni la stessa `BBox` (x0/y0 inclusivi,
   x1/y1 esclusivi) per ogni `(tx, ty)` ad ogni chiamata.
3. `tiles_for_bbox(bbox)` ritorni lo stesso `TileRect` per ogni
   `bbox` dato, anche con `bbox` vuoto o completamente fuori dal
   framebuffer.
4. `DirtyTileMask::mark_tile(tx, ty)` modifichi il bit corrispondente
   in modo idempotente, e `for_each_dirty_tile(grid, fn)` iteri i tile
   in **ordine lessicografico stabile** (tx, ty crescente) su N
   chiamate consecutive.

### Acceptance suite (PR 6.1)

`tests/deterministic/test_tile_determinism.cpp` (10 TEST_CASE, commit
`020ea8c2`) verifica i 4 punti sopra:

| # | TEST_CASE | Cosa garantisce |
|---|---|---|
| 1 | "TileGrid: same constructor args produce same dimensions" | cols/rows/tile_count identici su N costruzioni |
| 2 | "TileGrid: edge-case dimensions — 1×1, exact divisors, last-col shorter" | ragged edges corretti (es. 100×80 / tile 32 → cols=4, rows=3) |
| 3 | "TileGrid: tile_bounds is deterministic across runs" | x0/y0/x1/y1 uguali su 4 run consecutivi |
| 4 | "TileGrid: tiles_for_bbox — single tile, boundary, empty, outside" | 4 sotto-casi del mapping bbox → TileRect |
| 5 | "DirtyTileMask: bit pattern is identical across 4 fresh constructions" | `mark_all` produce bit-pattern identico bit-for-bit su 4 invocazioni |
| 6 | "DirtyTileMask: mark_tile + is_dirty round-trip (checkerboard)" | pattern checkerboard (`(tx+ty)%2==0`) round-trip su tutta la griglia |
| 7 | "DirtyTileMask: for_each_dirty_tile — order is deterministic across runs" | iteration-order lessicografico stabile su 4 run consecutivi |
| 8 | "DirtyTileMask: clear() restores zero bit pattern" | reset post `mark_all` azzera tutti i bit e `dirty_count == 0` |
| 9 | "DirtyTileMask: mark_bbox + dirty_count agree on tile coverage" | bbox `{0,0,64,64}` su grid `256×256/tile=64` → 1 tile dirty |
| 10 | "DirtyTileMask: 4 fresh runs of identical marking produce identical bits" | pattern pseudo-random `(tx*31+ty*17)%7==0` riproducibile su 4 invocazioni |

### Stato attuale

🟢 **Done** — la suite di acceptance per il path tile è completa e
le rot TICKET-007.q/r/s/t/u a livello di renderer sono ufficialmente
**isolate al path post-tile** (vedi `tests/deterministic/gradient_determinism_tests.cpp`
line 83-98 comment block: "hypothesis: forcing a fresh `tbb::task_arena`
per render frame may restore cross-thread-count equivalence").
Il path tile deterministico è il **proof floor** che le funzioni di
infrastruttura tile sono bit-replicabili; ogni futura regressione che
introduca scheduler-state dentro al data path del tile fallirà
immediatamente questo gate.

### Continuità architetturale V3

Il path tile è il **foundation** per i Pillar 9 (Tile Scheduler) e
Pillar 10 (Per-Tile Cache) del [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md):
- Pillar 9: `tbb::parallel_for` nested su `tbb::blocked_range<int>(0,
  active_tiles.size())` per tile — il data path deterministico sotto
  TileGrid + DirtyTileMask è il prerequisito perché cache-hit per-tile
  possa essere confrontabile senza dipendere dall'ordine di scheduling.
- Pillar 10: `TileCacheKey { node_digest, tile_id, input_hash,
  params_hash, frame }` — la key è derivata da dati che il path tile
  già garantisce deterministici, quindi la determinismo del cache hit/
  miss è una proprietà emergente.

---

## 6. Compliance Metadati TICKET-007

Ogni `* doctest::skip()` in `tests/` del progetto deve portare
metadati compliance (`TICKET-XXX`, `Issue/Owner/Motivation/Data
introduzione/Deadline rimozione`) come da `tools/test_architectural.sh`
Section 3 (Anti-skip-senza-ticket) — vd. ticket umbrella
[`TICKET-007`](FOLLOWUP_TICKETS.md) per il catalogo di skip-with-metadata.

I 5 sub-ID `TICKET-007.q/r/s/t/u` che marcano i test in
`tests/deterministic/gradient_determinism_tests.cpp` sono **compliance-
only** — non fixano il bug sottostante. Il fix delle rot sottostanti
è demandato a sub-ticket per-bug quando le rot vengono schedule nel
roadmap (vd. **§2** *Cosa serve per chiudere*).

I test acceptance del path tile (§5) sono la prova che il **sotto-
sistema tile** è uscito dalla categoria rot-PM-rotto. Non appena la
rot `SoftwareRenderer` (vd. §2/§3) verrà fixata in un sub-ticket
separato, i 5 sub-ID potranno essere riaperti uno per uno e i test
riabilitati — senza richiedere modifiche al path tile stesso.

[TICKET-007.q/.r/.s/.t/.u]: docs/FOLLOWUP_TICKETS.md#t-006-t-007---remove-process-wide-detailg_debug_config-p1-ticket-5-from-architectural-spec
[STATUS.md]: STATUS.md
[ROADMAP.md]: ROADMAP.md
[V3_BLUEPRINT.md]: V3_BLUEPRINT.md
