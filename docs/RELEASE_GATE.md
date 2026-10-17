# Chronon3D — Release Gate

> Per v0.1, identità, scope e criterio same-SHA sono definiti esclusivamente in [`docs/RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md).

La certificazione corrente e le baseline storiche vivono in [`docs/baselines/`](docs/baselines/). Lo snapshot corrente è in [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).

## Baseline verde v0.1 — requisiti (tutti sul release SHA)

Il release contract canonico per v0.1 è `docs/RELEASE_V0_1_CONTRACT.md`:
`v0.1` punta a `7e86278e5535b799ec5c54960e520ce38c77244a`. La baseline
`main@7eb5c2ba` è soltanto evidenza storica e non certifica il tag.


Prima di qualsiasi release, tutti i controlli seguenti devono passare sullo **stesso commit**:

Chronon3D è Linux-only. CMake interrompe la configurazione per toolchain non
Linux; release build e certificazioni si eseguono esclusivamente su Linux.

| # | Controllo | Descrizione |
|---|---|---|
| 1 | Build core | `cmake --build` su preset Linux con target core |
| 2 | Test core | `ctest` su test core, nessun fallimento |
| 3 | Build lean | Compilazione senza contenuti opzionali |
| 4 | Test lean | Test senza contenuti, nessun fallimento |
| 5 | Architecture gate | `tools/check_architecture.py` (regole in `tools/architecture_rules.toml`) bloccante |
| 5a | Architecture gate selftest | `tools/architecture_gate_selftest.sh` (10 fixture negative + mirror wave9) — certifica il certificatore |
| 5b | CMake DAG gate | regole `cmake_dag` (target demoliti + edge leaf→aggregate) nello stesso engine, sopra i CMake source-of-truth |
| 6 | Renderer boundary gate | Gate renderer/backend bloccante |
| 7 | No-content build/test | Build e test senza moduli content |
| 8 | Scene/camera test | Link + run di `chronon3d_scene_tests` |
| 9 | Install consumer | Progetto esterno installa, compila e linka SDK |
| 10 | Full validation | Build e test completi su preset di validazione |
| 11 | Documenti allineati | Commit, comandi, exit code documentati |

## Text Production V1

Tutti i punti seguenti devono essere veri:

1. almeno 20 preset generali e 8 preset subtitle verificati (categorie distinte; i subtitle non contano nei generali; catalogo corrente: 20 + 8);
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
     (a) `#include`s SOLO header elencati in `cmake/Chronon3DPublicHeaders.cmake`;
     (b) chiama SOLO `RenderEngine::render(...)` + helper SDK di output
         (es. `chronon3d::save_png`) dalla stessa superficie del
         manifest;
     (c) NON raggiunge direttamente o indirettamente (via `try_compile`
         o altri probe) nessun header `advanced/` /
         `internal.hpp` / `runtime.hpp` / `test/*`;
     (d) gli header transitivi richiesti dalla compilazione fanno parte di
         `CHRONON3D_SDK_REQUIRED_TRANSITIVE_HEADERS`; non sono API supportata
         e il consumer non deve includerli direttamente. Il progetto non
         espone un umbrella header `chronon3d/chronon3d.hpp`.
   Validazione end-to-end: `tools/install_consumer_test.sh`
   (`tests/install_consumer/` Phase 4 deve emettere `[BOUNDARY-OK]` e
   produrre un PNG non completamente nero). I run di Fase-2
   (`tools/sdk/check_archive_canaries.sh`) e di Fase-3
   (`tools/sdk/check_feature_ghosts.sh`, opt-in) restano i gate
   canonici per la copertura del simboli; il consumer manifest-clean
   è l'invariante pubblica della SDK.
9. **C ABI v2**: `chronon_abi_version()` deve riportare il contratto v2;
   create_v2, input JSON a lunghezza esplicita, buffer caller-owned,
   serializzazione BUSY e stato strutturato per asset modificati devono essere
   verificati da consumer C/C++ sul medesimo SHA.

## Doctor — capability check encoder

`chronon doctor` (flag `--json` e/o `--deep`) certifica la readiness
 dell'ambiente di encoding interrogando l'eseguibile `ffmpeg` sul `PATH`.
 I capability check encoder sono:

| Check | Probe canonico | Assenza → |
|---|---|---|
| `encoder.ffmpeg` | `ffmpeg -version` | `FAIL` (blocca `ready`) |
| `encoder.h264` | `ffmpeg -h encoder=libx264` | `FAIL` (codec baseline, blocca `ready`) |
| `encoder.h265` | `ffmpeg -h encoder=libx265` | `SKIP` (opzionale) |
| `encoder.av1` | `ffmpeg -h encoder=libaom-av1` | `SKIP` (opzionale) |
| `encoder.nvenc` | `ffmpeg -h encoder=h264_nvenc` | `SKIP` (hardware NVIDIA) |

Regole:

1. `encoder.ffmpeg` e `encoder.h264` devono risultare `PASS` perché il doctor
   riporti `ready=true` (assenza → `FAIL`);
2. `encoder.h265`, `encoder.av1`, `encoder.nvenc` sono opzionali: assenza →
   `SKIP` (advisory), non bloccano `ready`;
3. se `ffmpeg` non è presente, tutti i check codec degradano a `SKIP`
   (nessun probe viene tentato);
4. il probe è una capability probe reale (`ffmpeg -h encoder=<name>`), non la
   sola presenza dell'eseguibile (`ffmpeg -version`).

## Certificazione Globale Linux

Gli orchestratori prodotto `tools/verify_*_linux.sh` (15 sub-gate, verdict aggregato
`CHRONON_PRODUCT_FUNCTIONAL_{PASS,FAIL,BLOCKED}`) sono stati consolidati nei gate
CI/manifest il 2026-09-03 (`722aec744`). I gate eseguibili correnti sono:

- developer gate chain: `tools/gates/manifest.sh`, eseguita da `tools/run_developer_gates.sh` e dal pre-push hook;
- CI gate set: `CI_GATES` from `tools/gates/manifest.sh`, with workflow jobs in `.github/workflows/ci.yml`;
- certificazione SDK installata: `tools/install_consumer_test.sh` + `tools/verify_sdk_product.sh`;
- certificazione zero-copy: `tools/verify_zero_copy_end_to_end.sh` ([`docs/ZERO_COPY_GATES.md`](ZERO_COPY_GATES.md)).

La baseline storica della vecchia orchestrazione (verdict aggregato
`CHRONON_PRODUCT_FUNCTIONAL_BLOCKED`: 14/14 PASS + 1 forward-pointed) resta come
evidenza in [`docs/baselines/main-ef9c83f1-baseline.md`](baselines/main-ef9c83f1-baseline.md);
la reference verde storica `main@7eb5c2ba` (11/11, conteggio della vecchia
orchestrazione) resta in
[`docs/baselines/main-7eb5c2ba-baseline.md`](baselines/main-7eb5c2ba-baseline.md).

**Il release SHA v0.1 è BLOCKED / NOT CERTIFIED** finché la stessa SHA non supera
build, CTest e gate. Vedi [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) per
lo stato aggiornato.

## Criterio di chiusura

Un lavoro è chiuso soltanto quando:

- il codice usa il percorso canonico;
- i test pertinenti vengono eseguiti e passano;
- i gate non sono indeboliti;
- il branch è aggiornato su `origin/main`;
- la modifica è piccola e reviewable;
- i documenti riportano lo stesso stato osservato.
