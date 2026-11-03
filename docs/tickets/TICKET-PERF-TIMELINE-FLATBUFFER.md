# TICKET-PERF-TIMELINE-FLATBUFFER — valutare il timeline una volta per item

**Stato (2026-11-02): APERTO, non implementato — dipendenza non soddisfatta.**
Dipende da
[`TICKET-PERF-FRAME-PROOF-CHEAP`](TICKET-PERF-FRAME-PROOF-CHEAP.md) e ne
condivide il gate (MP4 byte-identico, `timeline_eval ≤3 ms/frame`, serie ×3).
Non è stato toccato in questa sessione: il run r53 chiude a 184 s con l'overlay
render invariato (~80 s), che è esattamente la quota che questo ticket e il suo
predecessore devono togliere.

**Verifica 2026-11-02.** Il predecessore non ha ancora l'identità a monte
(`prepared_parameters` senza produttori), quindi il P2 di questo ticket non è
avviabile senza prima chiudere FRAME-PROOF. Il dato disponibile resta
`timeline_eval` 42,6 ms/frame (r49, 92% del loop) contro 0,26–0,74 ms nel
benchmark; il profilo per-frame al 100% richiesto dal P1 non è stato eseguito.
Nessuna patch.

**Controllo sul Garlasco corrente (r101/r102, 2026-11-02).** I job hanno
prodotto lo stesso SHA overlay; il tempo overlay è 15,606 s / 14,635 s, mentre
il wall totale è 46,482 s / 66,736 s. Entrambi i sidecar riportano
`timeline_eval_ms = 0` e i batch riusano gli stessi 1.584 frame. Questa
telemetria non replica r49 (42,6 ms/frame) e non autorizza a proiettare il
target di 13 s sul flusso attuale. Prima di implementare P2 occorre identificare
la differenza di workload/percorso o correggere la strumentazione; i due run
non sono una serie ×3 e non provano un miglioramento.

## TODO — cosa manca (agg. 2026-11-01)

- [ ] **P1 — Trovare dove i 42,6 ms si separano dai 0,74 ms del benchmark.**
  Logica: lo stesso motore misura 0,26–0,74 ms/frame in
  `command_benchmark_saturation` e 42,6 ms/frame sul batch di produzione; finché
  la differenza non è attribuita a una causa sola (topologia non statica,
  ricampionamento per frame, ri-parsing delle espressioni, rebuild del blocco
  parametri) ogni riscrittura è scommessa. Dove:
  `src/compiler/typed_program_sampler.cpp:48-219` (blocco parametri ricostruito
  per frame, `upper_bound` per track a `:30-36`), `src/render_graph/pipeline/composition_frame_single.cpp:37-52`,
  `src/render_plan/render_plan_decoder_layer_style.cpp` (decode per frame invece
  che per item). Gate: profilo per-frame attribuito al 100% su un item reale, con
  i 42,6 ms divisi per voce e nessun residuo >10%.
- [ ] **P2 — Tavola piatta per item.** Logica: pre-compilare per ogni frame dello
  item una riga POD (transform/opacity/parametri già risolti), così che il costo
  per frame sia un indice, non una valutazione. Precedente in repo: la camera è
  già baked per tutti i frame (`src/compiler/compiler.cpp:140-144`). Estende la
  proposta N3 di [`100x-IDEE-NUOVE-2026-10-31.md`](../../../100x-IDEE-NUOVE-2026-10-31.md)
  («pose strip SoA precompilata»)
  dal solo `timeline_eval` all'intero confine scena→grafo. Dove:
  `src/compiler/`, `src/render_plan/`, `composition_frame_single.cpp:52`
  (`dynamic_scene = evaluate_scene(ctx)`). Gate: `timeline_eval` **≤3 ms/frame**
  sul corpus overlay (r49 ricostruito) e MP4 **byte-identico**; nessun frame
  soglia oltre il budget 41,67 ms.
- [ ] **P3 — Espressioni compilate, non ri-parse.** Logica: `C-10` di
  `TICKET-PERF-REVOLUTIONARY-IDEAS` è il naturale complemento: se le proprietà
  animate vengono ri-parse da stringa a ogni frame, la tavola piatta non serve a
  niente perché il costo si sposta a valle. Dove: `composition_->evaluate` nel
  graph eval path. Gate: `loop_graph_eval` misurato prima/dopo sullo stesso
  golden.
- [ ] **P4 — Esporre il numero in produzione.** Logica: `timeline_eval_ms` esiste
  nel sidecar ma non è un gate: senza soglia nel receipt, una regressione a
  +10 ms/frame passa come «rumore». Dove:
  `apps/chronon3d_cli/commands/video/common/pipe_export_profile.cpp:86`,
  `apps/chronon3d_cli/commands/video/exporters/pipe_timing_sidecar_frames.cpp:26`.
  Gate: soglia nel validator del receipt + serie ×3.

## Evidenza misurata

Batch overlay r49 (67,0 s di programma, 1607 frame, RTX A4000), dal receipt
`overlay_render/artifact/chronon_telemetry.job`:

| Voce | Totale | Per frame | Quota loop |
|---|---:|---:|---:|
| `render_loop_wall_ms` | 74 257,8 ms | 46,3 ms | 100% |
| `timeline_eval_ms` | **68 306,5 ms** | **42,6 ms** | **92,0%** |
| `graph_execute_ms` | 3 654,3 ms | 2,3 ms | 4,9% |
| `gpu_execute_ms` | 3 027,4 ms | 3,97 ms (frame reso) | 4,1% |
| encoder totale | 991,7 ms | 0,6 ms | 1,3% |

Nello stesso motore, in benchmark, `timeline_eval` è **0,26–0,74 ms**
(`TICKET-PERF-REVOLUTIONARY-IDEAS` §5b/P4;
[`100x-IDEE-NUOVE-2026-10-31.md`](../../../100x-IDEE-NUOVE-2026-10-31.md) N3).
Discrepanza: **57–163×**, sullo stesso host, con lo stesso motore.

## Perché questo è il secondo ticket e non il primo

Da solo non sposta il gate: se resta il percorso «valuta per decidere che non
c'è niente da fare», il 53% dei frame continua a pagare l'intera valutazione
anche se la valutazione scende a 1 ms. L'ordine corretto è
[`TICKET-PERF-FRAME-PROOF-CHEAP`](TICKET-PERF-FRAME-PROOF-CHEAP.md) (togliere la
valutazione dai duplicati) e **poi** questo (rendere cheap la valutazione dei
frame veri). Con entrambi: 763 frame resi × ~14 ms + 844 duplicati × ~0 → batch
da 74,3 s a ~13 s, cioè **frame ≤8 ms** e 500 overlay/min in ~5 lane invece di
28 (aritmetica: 500 item/min = 8,33 item/s × 73 frame = 608 frame/s; a 8 ms/frame
una lane fa 125 frame/s).

## Non obiettivi

- Non toccare la GPU (3,97 ms/frame reso, 8,6%; 6,6% di utilizzazione media) né
  il numero di pass: 955 passate `glow`, 8 264 `composite`, 200 `transform`
  sull'intero batch, senza alcun segno di contesa.
- Non aggiungere lane: l'audit del ticket 100× misura **25% dei campioni in spin
  TBB**; le lane si cannibalizzano già.
- Non «parallelizzare la valutazione»: è seriale per natura (un frame dipende dal
  precedente per il delta) e il problema è il costo, non la concorrenza.

## Decisione collegata

[`ADR-DRAFT-DECIDE-BEFORE-EVALUATE`](ADR-DRAFT-DECIDE-BEFORE-EVALUATE.md).
