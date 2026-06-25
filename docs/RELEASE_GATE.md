# Chronon3D — Release Gate

> Snapshot: `main@24388800` — 25 giugno 2026. Linux-only.
>
> Requisiti per dichiarare una release valida.
> Per lo stato corrente vedi [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Per le milestone vedi [`ROADMAP.md`](ROADMAP.md).
> Ultima baseline macchina-verificata: [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md).

## Baseline verde — requisiti (tutti sullo stesso commit)

Prima di qualsiasi release, tutti i controlli seguenti devono passare sullo **stesso commit**:

| # | Controllo | Descrizione |
|---|---|---|
| 1 | Build core | `cmake --build` su preset Linux con target core |
| 2 | Test core | `ctest` su test core, nessun fallimento |
| 3 | Build lean | Compilazione senza contenuti opzionali |
| 4 | Test lean | Test senza contenuti, nessun fallimento |
| 5 | Architecture gate | `tools/check_architecture_boundaries.sh` bloccante |
| 6 | Renderer boundary gate | Gate renderer/backend bloccante |
| 7 | No-content build/test | Build e test senza moduli content |
| 8 | Scene/camera test | Link + run di `chronon3d_scene_tests` |
| 9 | Install consumer | Progetto esterno installa, compila e linka SDK |
| 10 | Full validation | Build e test completi su preset di validazione |
| 11 | Documenti allineati | Commit, comandi, exit code documentati |

## Text Production V1

Tutti i punti seguenti devono essere veri:

1. almeno 20 preset generali e 8 preset subtitle verificati;
2. input word-timing JSON/SRT funzionante;
3. styling per parola e highlight produttivi;
4. ogni preset ha golden su 16:9 e 9:16, testo corto/lungo e almeno tre timestamp;
5. consumer SDK installato usa i preset senza includere header interni;
6. output seriale e parallelo deterministico.

## Camera Production V1

Tutti i punti seguenti devono essere veri:

1. tutte le nuove composizioni usano `CameraDescriptor` o `CameraProgram`;
2. `CameraSession` è posseduta dal render job;
3. nessun lookup o compilazione avviene per frame;
4. orientation e projection hanno un solo contratto matematico;
5. test camera compilati collegano ed eseguono in CI;
6. adapter legacy hanno parity test e una fase di rimozione;
7. consumer SDK esterno può costruire e usare una camera compilata.

## SDK Product V1

Tutti i punti seguenti devono essere veri:

1. un progetto vuoto può installare e usare `Chronon3D::SDK`;
2. render di almeno una composizione reale fuori-tree (testo + camera → PNG);
3. nessun link diretto a target interni;
4. documentazione e artifact associati allo stesso tag;
5. API pubblica minima documentata;
6. header interni esclusi dagli esempi;
7. artifact Linux riproducibili.

## Baseline osservata — `main@446a60e2` (2026-06-23)

L'unica baseline macchina-verificata ha eseguito **4 controlli** (non tutti i gate).
Dettaglio completo in [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md).

| # | Controllo | Esito | Blocker |
|---|---|---|---|
| BG-1 | Software renderer boundary gate | ✅ PASS | — |
| BG-2 | `linux-full-validation` configure | ✅ PASS | — |
| BG-3 | `linux-lean-dev` configure | ✅ PASS | — |
| BG-4 | Build `chronon3d_text_preset_visual_tests` | ❌ FAIL | TICKET-039 |

**Verdetto**: 3/4 ✅ — baseline non verde. I gate 5 (architecture), 8 (scene/camera test),
9 (install consumer) e 10 (full validation) non sono stati eseguiti in questa baseline.

### Progressi sull'HEAD corrente (da re-baselinare)

- **🟢 TICKET-039** — RISOLTO nel commit `68c3e0f0` (TXT-00 build/link green).
- **🟢 TICKET-038** — RISOLTO nel commit `91debc36` (3 source-level ROT chiuse).
- **🟢 Text Preset Tests** — `chronon3d_text_preset_visual_tests` cablato nel render aggregator
  (commit `44def204`).

I due blocker noti della baseline `446a60e2` sono quindi chiusi nel grafo dei commit dell'HEAD corrente,
ma non è ancora stata registrata una nuova baseline completa. Per dichiarare `24388800` una baseline
verde serve un run macchina-verificato di tutti gli 11 gate sul commit candidato.

## Criterio di chiusura

Un lavoro è chiuso soltanto quando:

- il codice usa il percorso canonico;
- i test pertinenti vengono eseguiti e passano;
- i gate non sono indeboliti;
- il branch è aggiornato su `origin/main`;
- la PR è piccola e reviewable;
- i documenti riportano lo stesso stato osservato.
