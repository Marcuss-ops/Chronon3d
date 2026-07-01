# Chronon3D — Release Gate

La certificazione corrente e le baseline storiche vivono in [`docs/baselines/`](docs/baselines/). Lo snapshot corrente è in [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).

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
7. artifact Linux riproducibili;
8. **P3-H — manifest-clean consumer (TICKET-011 + V0.1 SDK manifest).**
   Il progetto esterno `tests/install_consumer/`:
     (a) `#include`s SOLO header elencati in `cmake/Chronon3DPublicHeaders.cmake`
         (`chronon3d/chronon3d.hpp` + `chronon3d/sdk/*` +
         `chronon3d/timeline/composition.hpp`);
     (b) chiama SOLO `RenderEngine::render(...)` + helper SDK di output
         (es. `chronon3d::save_png`) dalla stessa superficie del
         manifest;
     (c) NON raggiunge direttamente o indirettamente (via `try_compile`
         o altri probe) nessun header `advanced/` /
         `internal.hpp` / `runtime.hpp` / `test/*`;
     (d) i `#include` *transitivi* dell'umbrella
         `chronon3d/chronon3d.hpp` (math/*, animation/*, geometry/*,
         timeline/composition.hpp, ...) fanno parte del manifest scope:
         l'umbrella non può esporre il vocabolario pubblico senza di
         essi. Non sono "creep", sono "manifest scope" documentato.
   Validazione end-to-end: `tools/install_consumer_test.sh`
   (`tests/install_consumer/` Phase 4 deve emettere `[BOUNDARY-OK]` e
   produrre un PNG non completamente nero). I run di Fase-2
   (`tools/sdk/check_archive_canaries.sh`) e di Fase-3
   (`tools/sdk/check_feature_ghosts.sh`, opt-in) restano i gate
   canonici per la copertura del simboli; il consumer manifest-clean
   è l'invariante pubblica della SDK.

## Criterio di chiusura

Un lavoro è chiuso soltanto quando:

- il codice usa il percorso canonico;
- i test pertinenti vengono eseguiti e passano;
- i gate non sono indeboliti;
- il branch è aggiornato su `origin/main`;
- la PR è piccola e reviewable;
- i documenti riportano lo stesso stato osservato.
