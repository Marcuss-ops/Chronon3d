# TICKET-PERF-FRAME-PROOF-CHEAP — la prova che un frame non cambia deve costare meno del frame

**Stato (2026-11-02): APERTO — telemetria di fallback implementata e osservata
in un job Garlasco; ottimizzazione non implementata né verificata.** Le
modifiche di questa sessione sono sul confine del final job (`refactored`) e
sulla certificazione dell'artefatto (RenderingGen), non sul motore. Motivo, non
scusa: i tre P richiedono di
modificare il percorso certificato del renderer (immediate-mode → retained) e
il loro stesso gate impone **MP4 byte-identico** più una serie di sidecar ×3;
senza quella verifica una patch qui è indistinguibile da una regressione
visiva. Il taglio misurato finora (495 → 184 s, poi 74 s a cache completamente
calda) è tutto fuori dal motore.

**Attenzione alla premessa (agg. 2026-11-01, r64/r65).** Il render overlay vale
~80 s **solo quando renderizza davvero**: nelle run calde l'artefatto overlay
arriva dalla cache (stesso SHA, `render_loop_wall_ms` 6,4 s contro 74,3 s) e i
70 s del run non contengono quel lavoro. Il valore di questo ticket è quindi
tutto sulla **prima** esecuzione di un payload nuovo (il caso che conta per la
produzione), non sulla ripetizione dello stesso.

**Verifica 2026-11-02.** L'identità a monte continua a non esistere:
`execution_mode` è assegnato solo a `DynamicCallback`
(`src/timeline/compile_evaluate.cpp:91`) e `prepared_parameters` non ha
produttori fuori dai test (`code_search` su `Chronon3d`: solo
`tests/render_graph/...` e il supporto di serializzazione). La condizione di
attivazione non è verificabile senza un render GPU con sidecar ×3 e MP4
byte-identico; il receipt r49 resta l'evidenza disponibile (46,3 ms/frame, 53%
duplicati). È stata successivamente aggiunta la telemetria del fallback,
ora osservata su un job Garlasco; l'ottimizzazione non è implementata. Una
modifica non verificata sul riuso sarebbe indistinguibile da una regressione visiva.

**Canary Garlasco corrente (r101/r102, 2026-11-02).** I due job hanno
completato con successo e prodotto lo stesso SHA overlay
`227da1b914f2e50087fd921200d4cec2e4a57faba3d232ee43df1c1b7d4cf390`.
L'overlay è passato da 15,606 s (r101) a 14,635 s (r102), ma il wall totale è
salito da 46,482 s a 66,736 s: `prepare_join` è passato da 2,000 s a 12,302 s e
`remote_final_job` da 15,601 s a 19,161 s. Nei sidecar di entrambi i job
`timeline_eval_ms` è 0; il riuso fast-path dei batch è identico (1.584 frame).
Questo workload corrente quindi non conferma il costo r49 né dimostra un
guadagno attribuibile a questo ticket. Il delta overlay singolo (−0,971 s, due
run soltanto) non è un'accettazione; serve riallineare la fixture r49 al
workload corrente e completare il profilo ×3. `identity_fallback_frames` è
presente in 7/24 sidecar item e 3/9 riepiloghi batch di r102; vale 0 dove
presente. La propagazione completa e il gate di certificazione restano da
verificare.

## P1 — Attribuzione (primo passo obbligatorio): l'identità a monte **non esiste**

Verificato in questo checkout il 2026-11-01 (grep su `src/`, `include/`, `apps/`):

| Fatto | Evidenza |
|---|---|
| `execution_mode` è **hardcoded** a `DynamicCallback` in compilazione | `src/timeline/compile_evaluate.cpp:91` — nessun assegnamento a `StaticTopologySlots` in tutto il repository (l'unico altro uso è il restore dalla cache, `src/cache/compiled_artifact_cache.cpp:363`) |
| La tavola parametri per-frame **non viene mai popolata** | `CompiledFrameGraph::prepared_parameters` è dichiarata (`include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp:295`) e serializzata nello schema, ma **nessun** produttore la riempie |
| Quindi il percorso overlay valuta la scena via callback a ogni frame | `src/render_graph/pipeline/composition_frame_single.cpp:35-52`: `template_scene` è usato solo se `is_static_topology`, che resta `false` |

Conseguenza onesta: il «lookup digest → duplicato O(1)» del *Fix* qui sotto **non è
un controllo da spostare, è un artefatto da costruire**. Finché
`execution_mode` resta `DynamicCallback`, nessuna patch dentro
`render_single_frame()` può decidere il duplicato senza valutare: qualunque
digest calcolato a monte deve essere prodotto dal compilatore dell'item, e la sua
completezza (ogni campo che influenza il `combined_fp`) è ciò che il gate
byte-identico deve provare. Questo è il lavoro vero del P1, e non è stato
iniziato.

## TODO — cosa manca (agg. 2026-11-01)

- [ ] **P1 — Togliere la valutazione dal percorso del duplicato.** Bloccato
dall'attribuzione qui sopra: prima si costruisce l'identità nel compilatore,
poi si sposta la decisione. Logica: oggi
  `render_single_frame()` esegue `evaluate_scene()` per **ogni** frame prima che
  `TileExecutionPolicy` possa decidere `DuplicateFrame`; l'identità che serve a
  decidere (fingerprint `combined_fp`) è un prodotto della valutazione. Servono
  un'identità **a monte** (digest del piano compilato dell'item + indice slot per
  frame) e un controllo O(1) che preceda la valutazione. Dove:
  `src/render_graph/pipeline/composition_frame_single.cpp:37-52` (eval sempre,
  fast-path solo `is_static_topology`), `src/render_graph/pipeline/tile_execution_policy.cpp:460`
  (duplicato deciso *dopo*), `src/render_graph/pipeline/frame_delta_compiler.cpp:285`
  (eleggibilità calcolata da fingerprint di valutazione). Gate: sidecar r49
  replica — costo medio dei 844 frame duplicati da **43,5 ms a ≤2 ms**, wall del
  batch da 74,3 s a ≤45 s, MP4 byte-identico (SHA-256) al controllo.
- [ ] **P2 — Contare il fallback.** Logica: quando l'identità a monte manca, il
  percorso deve ricadere sull'eval e dichiararlo (`identity_fallback_frames`),
  così un duplicato «gratis» non può essere in realtà un falso positivo. Dove:
  `include/chronon3d/core/profiling/render_counter_macros.hpp:445`
  (`duplicate_frame_emissions`), `src/render_graph/pipeline/tile_execution_policy.cpp:128`.
  Gate: contatore presente nel sidecar e zero nei batch certificati.
  **Implementato e osservato su Garlasco r102:** `identity_fallback_frames`
  conta i frame single-sample con duplicazione abilitata che valutano il
  callback dinamico; il campo è presente in 7/24 sidecar item e 3/9 riepiloghi
  batch e vale 0 dove esposto. Propagazione completa e batch certificato
  restano aperti; P1 e P3 restano aperti.
- [ ] **P3 — Demolire il doppio calcolo.** Logica: una volta che il duplicato non
  valuta più, il ramo `duplicate_frame_eligible` non deve restare raggiungibile
  dal percorso che ha già valutato, altrimenti convivono due authority. Dove:
  `frame_delta_compiler.cpp` + policy. Gate: nessun chiamante legacy.

## Evidenza misurata (r49, overlay batch, RTX A4000)

Fonte (percorsi dalla radice del progetto):
`ops/jobs/garlasco_editorial_e2e_20261030/job-full-r49.json`,
blocco `overlay_render/artifact/chronon_telemetry.job` + sidecar per-frame
(1607 frame, 67,0 s di programma, 1920×1080, 24 fps). Tutti i totali sotto sono
letti dal receipt, non dedotti.

| Voce (totale sul batch) | Valore | Per frame |
|---|---:|---:|
| frame | 1607 (1602 steady) | — |
| frame **resi** (`vulkan_frames`, `nvenc_frames`) | 763 (47%) | — |
| frame **duplicati** (`fast_path_reused_frames`) | **844 (53%)** | — |
| `render_loop_wall_ms` | **74 257,8 ms** | **46,3 ms** |
| ├ `timeline_eval_ms` | **68 306,5 ms (92%)** | **42,6 ms** |
| ├ `graph_execute_ms` | 3 654,3 ms (4,9%) | 2,3 ms |
| ├ `gpu_execute_ms` | 3 027,4 ms (4,1%) | 3,97 ms per frame reso |
| │ └ glow (955 passate) | ~2,7 ms/frame reso | 68% del tempo GPU |
| └ encoder (`submit`+`flush`+`packet`) | 991,7 ms (1,3%) | 0,6 ms |
| GPU utilization media | **6,6%** (picco 44%) | — |
| VRAM picco | 7 616 MB / 16 376 | — |

| Classe | Frame | Costo medio | Spesa |
|---|---:|---:|---:|
| resi | 763 | 48,1 ms (= eval 42,6 + graph 2,3 + GPU 3,97) | 36,7 s (49%) |
| duplicati | 844 | **43,5 ms con 0 ms di render** | **36,7 s (49%)** |

Il render loop copre 67,0 s di programma in **74,3 s** (0,88× realtime, 21,1 fps).

Il confronto interno al motore: nella `command_benchmark_saturation` lo stesso
`timeline_eval` misura **0,26–0,74 ms** per frame
(`TICKET-PERF-REVOLUTIONARY-IDEAS` §5b/P4;
[`100x-IDEE-NUOVE-2026-10-31.md`](../../../100x-IDEE-NUOVE-2026-10-31.md) N3). In
produzione, sullo stesso motore, costa **57–163× di più**. Nota che il sidecar
`diagnostics` riporta `timeline_patch_wall_us` alimentato dallo stesso `eval_us`
(`composition_frame_single.cpp:62-65`): sono due nomi per una misura sola, non
due misure.

## Diagnosi

La fast-path di duplicazione è **già corretta e già attiva**
(`write_duplicate_frame()` fa re-emissione del pacchetto; l'encoder intero pesa
991,7 ms su 74,3 s). Il difetto non è la fast-path, è la sua **posizione**: per
dimostrare che il frame non è cambiato bisogna campionare il timeline, e
campionare il timeline *è* il lavoro che si voleva evitare. Con il 53% dei frame
duplicati, **metà del wall del batch è la prova che non c'era niente da fare**.

Nota: non è pacing. Un loop che aspettasse il clock a 24 fps chiuderebbe a 1,00×
realtime; questo chiude a **0,88×** (74,3 s per 67,0 s di programma), quindi
nessun frame è in attesa del proprio slot: il tempo è lavoro. E il `timeline_eval`
medio (42,6 ms) è **quasi tutto** il costo di un duplicato (43,5 ms): un duplicato
non costa meno perché non disegna, costa perché **decide**.

## Fix (ipotesi, da misurare)

Il piano compilato per item produce, in un solo passaggio, una tavola di slot
(frame → digest di stato). Il render loop, per frame, fa: lookup digest → uguale
al precedente? allora `DuplicateFrame` senza valutare; diverso, allora è un frame
da rendere e la valutazione è già in cache per quell'item. È lo stesso schema già
usato per la camera, dove i parametri sono baked per tutti i frame
(`src/compiler/compiler.cpp:140-144`).

## Non obiettivi

- Non toccare l'encoder: 991,7 ms sull'intero batch (1,3%) non è il costo.
- Non toccare la GPU: 3,97 ms su 46,3 (8,6%), e il glow è fill-bound (vedi
  [`TICKET-PERF-GLOW-HALF-RES`](TICKET-PERF-GLOW-HALF-RES.md)).
- Non armare le dirty region per questo ticket: `dirty_area_ratio` è 1,000 su
  ogni frame di produzione (`TICKET-PERFORMANCE-AUDIT-2026-10`) e vale al più
  ~3 ms dei 46, la metà di ciò che qui si toglie. Contano solo dopo che il frame
  è sceso sotto ~8 ms.
- Non cambiare l'invarianza statica già certificata
  (`TICKET-STATIC-OVERLAY-DURATION-INVARIANT`, P1): questo ticket cambia *quando*
  si decide, non *cosa* è duplicabile.

## Decisione collegata

[`ADR-DRAFT-DECIDE-BEFORE-EVALUATE`](ADR-DRAFT-DECIDE-BEFORE-EVALUATE.md).
L'altro lato dello stesso confine (overlay che si ri-encoda nel final job):
[`TICKET-FINAL-JOB-BITSTREAM-REUSE`](../../../refactored/docs/operations/TICKET-FINAL-JOB-BITSTREAM-REUSE-2026-11-01.md).
