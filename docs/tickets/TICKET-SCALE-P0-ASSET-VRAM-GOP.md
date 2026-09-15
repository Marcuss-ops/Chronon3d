# TICKET-SCALE-P0 — pre-scaling P0 closure: shared asset identity, VRAM admission truth, single GOP executor authority

Stato: codice LANDED su `main` locale (build verde), evidenza testuale sotto.
Nessuna modifica ai canonici oltre la riga indice in
[`FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md).

Contesto: prima di alzare la concorrenza DirectYUV oltre il `mux` serializzato
di `daemon_service_ipc.cpp`, l'audit pre-scaling ha individuato quattro nodi P0
(lifetime degli asset condivisi, accounting VRAM, doppio executor GOP, doppia
policy `DeviceSelectionRequirements`) più due debiti MTSDF/Vulkan.

---

## P0-1 — `ImageCache` non trattiene più l'`AssetResolver` job-scoped

`VideoDeviceRuntime::ImageCache` è process/device lifetime e condivisa da tutti
i job DirectYUV di quel runtime; l'`AssetResolver` montato da un job è invece
job lifetime. Il percorso precedente installava il resolver del job nella cache
persistente:

```text
VideoDeviceRuntime (device)  →  ImageCache (device)  →  AssetResolver* (job)   ❌
```

Con un job solo restava nascosto; con due job concorrenti A poteva risolvere
con il root di B, e alla distruzione del job la cache tratteneva un puntatore
verso un oggetto distrutto.

Stato ora:

- `ImageCache::get_or_load(path, options, resolver)` /
  `find(path, options, resolver)` / `canonical_key_for(path, options, resolver)`
  sono il percorso canonico: la cache **non** trattiene il resolver, lo riceve
  per chiamata (`src/backends/assets/image_cache.cpp`,
  `include/chronon3d/backends/assets/image_cache.hpp`).
- `DirectYuvProgram::prepare()` usa l'overload esplicito
  (`src/media/video/direct_yuv/direct_yuv_program.cpp`).
- `setup_pipe_export_session()` non chiama più
  `image_cache().set_asset_resolver(job_ptr)`
  (`apps/chronon3d_cli/commands/video/exporters/pipe_export_pipeline_setup.cpp`).
- `set_asset_resolver()` resta solo per le cache per-runtime, dove cache e
  resolver hanno la stessa lifetime (`src/runtime/render_runtime.cpp`), ed è
  documentato come legacy da rimuovere.

Evidenza: `tests/cache/test_image_cache_resolver_identity.cpp` (4 case /
17 assertion, PASS) fissa che la risoluzione segue il resolver passato dal
chiamante e ignora quello installato, che due root diversi non aliasano la
stessa entry e che le `ImageDecodeOptions` partecipano della chiave.

## P0-2 — l'admission control descrive la VRAM reale

Il vecchio modello stimava `512 MB + 8 × w × h × 16` (≈1 GiB a 1080p) contro un
picco misurato di ~8.8 GB: un ordine di grandezza di errore, mascherato dal
mutex serializzato. Due correzioni:

1. `estimate_peak_vram_bytes()` /
   `resolve_device_requirements()` in
   `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp` sono l'unica
   policy: FullGraph scala dal punto osservato (8.8 GB @1920×1080, floor 2 GB),
   DirectYUV ha un floor conservativo di 1 GB, lavoro software dichiara 0.
2. La tabella locale del CLI pubblicava `vram_bytes = 0`
   (`video_export_pipe.cpp`), che **disabilita** il gate VRAM dello
   `DeviceScheduler` (guardia `capacity.vram_bytes > 0`): la stima onesta non
   avrebbe potuto fermare nulla. Ora il CLI sonda `cuDeviceTotalMem` e pubblica
   la memoria fisica, e `render_job_slots` viene dalla stessa authority
   (`resolve_render_job_slots`).

Effetto: su una scheda da 16 GB due job FullGraph a 1080p non entrano più
(2 × 8.8 > 16), e l'admission coincide con la realtà fisica.

Evidenza: 7 nuovi case in `tests/cli/test_daemon_render_concurrency.cpp`
(13 case / 94 assertion, PASS) fra cui "una scheda da 16 GB non può ammettere
due job FullGraph a 1080p".

### P0-2b — il working set deriva dal footprint compilato

La calibrazione da sola non è un modello: è un punto misurato. Il modello di
composizione è ora `derive_vram_working_set()` in
`apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp`, unico per daemon e
CLI, con le stesse categorie di `graph::DeviceMemoryPlan`:

```
planned_physical_bytes            (ResourcePlan / CompiledResourceTable)
+ peak_live_bytes                  (picco transiente oltre il piano)
+ planned_physical_bytes × depth    (ring per-frame-slot, depth 6)
+ decoder_budget + encoder_budget + atlas_budget + scratch_budget
+ 15% margine
= derived_bytes

estimated_peak = max(derived_bytes, calibration_floor)
```

Due proprietà che il vecchio numero singolo non aveva:

1. **Il compiled non può sotto-promettere.** Il charge è il massimo fra il
   modello compilato e la calibrazione misurata, quindi la correzione P0-2 non
   regredisce se il footprint è piccolo.
2. **Il gap è visibile.** `calibration_gap_bytes()` / `calibration_gap_ratio()`
   riportano quanto del picco il piano compilato **non** spiega. A 1080p con un
   piano realistico (~40 MB) il termine derivato è ~0.3 GB contro un floor di
   8.8 GB: il gap è ~96% del charge. Questo è il numero che deve scendere prima
   di poter togliere il floor, ed è la misura onesta del perché il modello
   compilato **da solo non può** essere l'authority oggi.
3. **Under-admission è fail-loud.** `reconcile_vram_working_set(ws, admitted)`
   confronta il derivato con quanto è stato effettivamente riservato: se il job
   compila a più di quanto gli è stato ammesso, il verdetto è `UnderCharged` e
   `fail_loud()`. È one-sided per costruzione (col floor il charge è normalmente
   molto sopra il compilato), quindi è una rete di sicurezza, non l'unico
   controllo.

Il numero post-compile viene emesso dal percorso reale in
`apps/chronon3d_cli/utils/job/render_job_finalize.cpp`, dove il `ResourcePlan`
è già liftato dal `CompiledFrameGraph` (nessuna quarta copia del loop di
lifting) — l'INTERO plan, non una costante:

```
[INFO] vram working-set: compiled=..MB peak_live=..MB ring=..MB derived=..MB derived_from_compiled=1
```

Evidenza: 5 nuovi case in `tests/cli/test_daemon_render_concurrency.cpp`
(**18 case / 123 assertion, PASS**) che pinnano: modello senza footprint ==
calibrazione; il ring è `footprint × 6`; un footprint oltre il floor diventa il
charge ed è monotono; un job software senza NVENC non riceve calibrazione ma
possiede comunque il proprio piano; e i quattro verdetti di
`reconcile_vram_working_set` incluso `UnderCharged`/`fail_loud()`.

## P0-4 — una sola `DeviceSelectionRequirements`

Il daemon e il fallback CLI costruivano la richiesta con due logiche diverse
(sessione NVENC, codec, VRAM, PCIe). Ora entrambe chiamano
`resolve_device_requirements(...)`; il builder ad-hoc del CLI è rimosso.

## P0-3 — un solo executor per lo schedule GOP

`GopExecutionExecutor` è la authority dichiarata (`GopExecutionPlan` →
executor → una `MuxSession` caller-owned). La produzione CLI passava ancora da
`execute_hybrid_gop_plan` (ricostruzione `HybridSegment[]`, MP4 temporanei per
segmento, remux). In questo ticket lo schedule **valida** attraverso
`GopExecutionExecutor::execute()` prima di qualunque I/O: una schedule non
valida viene rifiutata dalla authority invece che accettata dalla
ricostruzione locale. La demolizione dell'I/O su filesystem resta debito
tracciato qui sotto.

Evidenza: la validazione è nel percorso di produzione
(`apps/chronon3d_cli/utils/video/video_job_execute_dispatch.cpp`) e compila nel
build video; l'equivalenza byte-per-byte è l'exit condition del debito.

---

## MTSDF — identità content-addressed (e regressione di performance evitata)

`text::prepare_mtsdf_glyph()` è l'unica preparation authority backend-neutral;
il refactor recente ha unificato la duplicazione FreeType, ma la cache di
preparazione era ancora keyata sul **path** del font e il nuovo codice
calcolava lo SHA-256 dell'intero file **a ogni richiesta di glifo** (un face CJK
da 20 MB avrebbe letto 20 MB per glifo).

Ora:

- `text::prepared_font_content_digest(resolver, font_path)` è l'unica authority
  di identità: SHA-256 dei byte reali, memoizzato per (path canonico, size,
  mtime) — una lettura completa per versione di file, non per glifo
  (`src/backends/text/mtsdf_preparation.cpp`,
  `include/chronon3d/text/mtsdf_preparation.hpp`).
- `GlyphKey` usa il digest del contenuto; anche la chiave della face LRU lo
  include, altrimenti una face aperta sui byte vecchi genererebbe glifi poi
  salvati sotto il digest nuovo.
- Residenza face limitata (LRU 32, `CapacityMode::Count`) invece di una
  `unordered_map` illimitata.
- `DirectYuvTextTextureCache` (cache packed CUDA) usa la **stessa** identità
  tramite `key_for()`: `find`, `store` e `get_or_create` condividono una sola
  funzione di chiave, quindi la lowering non può più fare HIT sul path
  scavalcando il controllo di contenuto
  (`src/media/video/direct_yuv/text_texture_cache.{hpp,cpp}`).

Evidenza: `tests/text/test_mtsdf_font_identity.cpp` (5 case / 36 assertion,
PASS) e la suite preesistente `chronon3d_mtsdf_generator_tests` (7 case /
178 assertion, PASS) che dimostra che la generazione glifo non è cambiata.

Residuo dichiarato: `PreparedFontIdentity` non modella ancora l'istanza di
variazione (face index/variation axes) — nel repo non esiste una authority
delle variation, quindi la chiave usa face index 0. Va estesa quando la
selezione variability entra in scope.

## Vulkan — separazione planned/unplanned

`VulkanSurfaceAuthority` usa due domini numerici distinti: placement compilato
(`bind()`, slot plan-locali) e allocatore unplanned (`ensure()`,
`next_slot` da `kUnplannedSlotBase = 1 << 20`). La materializzazione compilata
resta compiler-owned e non può raggiungere il fallback dinamico. Non è
certificata same-SHA: resta il punto del debito sotto.

---

## Demolition Debt — hybrid GOP segment-file executor (P0-3)

| Campo | Valore |
|---|---|
| **Owner** | video/media lane (CLI video export) |
| **Reason** | `execute_hybrid_gop_plan()` è sopravvissuto come secondo executor: ricostruisce il grouping da `work_units`, ricrea i confini di segmento, decide copy/reencode, scrive un MP4 per segmento e poi rimuxa. Serve solo finché la scrittura diretta in una `MuxSession` non è certificata sul corpus. |
| **Exit condition** | `GopExecutionExecutor` + una sola `MuxSession` produce output byte-identico al percorso a segmenti sul corpus hybrid, e `execute_hybrid_gop_plan` ha zero caller. |
| **Equivalence test/gate** | test di parità `GopExecutionExecutor::execute` + uguaglianza hash del mux sui due percorsi (stesso corpus, stesso SHA input). |
| **Removal scope** | `struct HybridSegment`, workspace `.hybrid-*`, `segment_paths`, il caller hybrid di `assemble_segments()`, `execute_hybrid_gop_plan()`, e i commenti di debito in `video_job_execute_dispatch.cpp`. |
| **Status** | `ACTIVE` (fase 1 landata: validazione schedule attraverso l'executor) |

Percorso pagato oggi per lavoro che potrebbe essere un solo mux:
`encode → write → close → open → demux → timestamp-normalize → remux → delete`.

---

## Verifica (2026-09-15, questo change set)

```
linux-video-fast-dev (Vulkan + CUDA interop + FFmpeg + text + daemon)
  chronon3d_cli                                  269/269  EXIT 0  0 errori
  (include tutti i TU toccati: pipe_export_pipeline_setup.cpp,
   video_export_pipe.cpp, render_job_finalize.cpp, daemon_service_ipc.cpp,
   e i consumatori di pipe_export_session.hpp / daemon_render_concurrency.hpp)

linux-fast-dev (tests ON) — eseguiti dalla RADICE del repo
  chronon3d_daemon_render_concurrency_tests    18 case / 123 assertion   PASS
  chronon3d_cache_tests                                     997574       PASS
  chronon3d_mtsdf_font_identity_tests                        36         PASS
  chronon3d_mtsdf_generator_tests                           178         PASS
  chronon3d_backend_registry_tests                          108         PASS
  chronon3d_compositor_tests                             613890         PASS
  chronon3d_render_job_contract_tests                       287         PASS
  chronon3d_render_graph_tests                  378 case / 18653       PASS

gates
  check_architecture.py                191/191 PASS
  check_test_hygiene.sh                    3/3 PASS
  check_doc_sha_dedup.sh                DEDUP_PASS
```

Nota di metodo: le suite test dipendono da path relativi
(`tests/fixtures/Inter-Bold.ttf`), quindi **vanno eseguite dalla radice del
repo**; lanciate da `tests/` producono falsi rossi (`REQUIRE(exists(kFont))`
fallisce in 7/7 e 1/5 case) che non hanno nulla a che vedere col codice.

## Non eseguito / residuo

| Item | Stato | Nota |
|---|---|---|
| Suite video-gated (DirectYUV text cache, executor GOP) | **PASS** | lane `linux-video-test` configurata e costruita (648/648 step, EXIT 0): `chronon3d_cache_tests` include ora `cache/test_direct_yuv_text_cache.cpp` (**997596** assertion, +22 rispetto al lane fast-dev) e `chronon3d_cli_tests` include `cli/test_packet_assembler_contract.cpp` (GOP executor). Focus mirati: **DirectYUV text cache 2 case / 5 assertion PASS**, **GOP 9 case / 122 assertion PASS**, **mux boundary 3 case / 11 assertion PASS** |
| Piano VRAM derivato dal footprint compilato | **PASS (modello) / PARTIAL (site)** | `derive_vram_working_set()` è l'unico modello di composizione: `planned_physical_bytes` + `peak_live_bytes` + ring (footprint × `kDefaultFrameRingDepth`) + budget NVDEC/NVENC/atlasi/scratch + margine 15%, le stesse categorie di `graph::DeviceMemoryPlan`. La calibrazione misurata è il **floor** che il charge non può sottopassare. Resta PARTIAL sul *site*: il payload `RENDER_JOB` porta solo `plan_path`, quindi al punto di admission pre-compile **non esiste** alcun footprint e il daemon usa legittimamente il floor; il numero post-compile è derivato dal grafo reale in `render_job_finalize.cpp` |
| Gate `estimated_peak_vram` vs `observed_peak_vram` | **PARTIAL** | `reconcile_vram_working_set()` rileva l'under-admission (compiled > charged: `verdict == UnderCharged`, `fail_loud()`), coperto da test. Il confronto dati-vs-stima su una run GPU-host resta **NOT RUN**: nessuna GPU in questa sessione |
| Lane `linux-video-test` | **LANDED** | liberati i due alberi `.tmp/chronon-builds/*` (33 GB; il daemon e `pipelinegen` girano da `build/chronon/linux-video-release`, nessun file aperto sotto `.tmp`), poi `cmake --preset linux-video-test` + build mirata: 648/648 step, EXIT 0, 19 GB liberi residui. Il vcpkg per-preset esisteva già (`vcpkg_installed/linux-video-test`), quindi nessuna reinstallazione di dipendenze |
| P0-3 fasi 2/3 (mux diretto, cancellazione segmenti) | **OPEN — gate ora eseguibile** | La lane video-test non è più un blocco: il parity gate è eseguibile. Fase 2 significa cambiare la proprietà di encoder/mux: `render_and_encode_ffmpeg_pipe` oggi possiede encoder e output path e deve invece ricevere una `MuxSession` caller-owned in cui finiscano sia i packet copiati sia quelli NVENC. Superficie del cambio: `setup_pipe_export_session`, il sink nativo, `PacketAssembler`, `FfmpegExportOptions` + continuità dei timestamp. Resta il lavoro più grande del ticket e non è stato avviato: l'exit condition è la **parità byte-identica** fra i due percorsi sul corpus hybrid, non la compilazione |
| Certificazione same-SHA main + Vulkan planned/unplanned | **NOT RUN** | `CURRENT_STATUS` resta valido: main non è "green" senza evidenza same-SHA |
| `compatibility_composition` / C22→C23 | **OPEN** | invariato da questo ticket |
| `chronon3d_render_graph_tests` | **RESOLVED — 378/378 PASS** | Non causato dal change set P0. Il `REQUIRE` aggiunto ha sbloccato la run (378 casi invece di 31, la `CHECK` indicizzava un vettore vuoto) ed è emerso un terzo rosso latente. Entrambe le cause diagnosticate con esperimenti e chiuse secondo la decisione presa: vedi §Diagnosi layer-batch |

### Diagnosi layer-batch (esperimenti riproducibili)

Domanda: il lowering è sbagliato o il test è stale? Evidenza:

1. `a78a8a29e` **non** è coinvolto. Ripristinando la sola versione pre-`a78a8a29e` di
   `frame_graph_builder_program_detail.hpp` (backup in `.tmp/diag/program_detail.before.hpp`)
   e ricompilando: entrambi i fallimenti restano **identici** — stesso hash
   `0x65304b41dbf40d8f` e stesso `layer_batches.size() == 0`.
2. La causa è il gate `const bool frame_bound_image_source = source_is_image &&
   node_info.binding_meta.has_binding(); if (frame_bound_image_source) return false;`
   introdotto da `00be928ab` (2026-09-14). Rimuovendo **solo** quelle due righe
   (mantenendo `batch_is_sound`): **17/17 assertion PASS** — l'invariante
   `100 Image layers → 1 CompiledLayerBatch` e il golden committato
   `fnv1a64=0x610ce49755184814` (rigenerato 2026-08-27) tornano verdi.
3. `frame_graph_builder_metadata_detail.hpp` imposta `binding_meta.active = true`
   per **ogni** nodo `Source`/`TextRun` raggiungibile: sia il ramo
   `has_layer_location` sia il ramo `is_root_source`. Quindi
   `has_binding()` non discrimina "frame-bound / external GPU asset" da
   "static-compatible", come invece dichiara il commento del gate
   ("batching static-compatible layers is still allowed below"): nessuna
   sorgente immagine può essere static-compatible, quindi nessuna immagine può
   più finire in un batch. Le sorgenti di testo invece sì (batch
   `TextRun + Transform`), come pinna
   `test_frame_graph_compiler_validation.cpp`.
4. `00be928ab` non ha aggiornato né i due test né il golden, e non ha aggiunto
   alcun test di regressione per il comportamento che il gate dichiara di
   correggere (le sue modifiche di test sono solo vulkan-debug e video).

5. Il SIGSEGV nascondeva un **terzo** fallimento. La vecchia `CHECK` su
   `layer_batches[0]` faceva indicizzare un vettore vuoto e il binario moriva lì:
   doctest riportava `31 | 29 passed | 2 failed | 347 skipped`, cioè 347 casi
   mai eseguiti. Con la guardia `REQUIRE` la suite gira per intero:
   **378 casi, 375 pass, 3 failed**. Il terzo caso è
   `FrameGraphCompiler - TextRun bypasses Shape processor validation`
   (`test_frame_graph_compiler_validation.cpp:109,114`).
6. Il terzo fallimento ha **una causa diversa**, e separa i due commit.
   Ripetendo l'esperimento con la sola parte pre-`a78a8a29e` e girando la suite
   completa: **376 pass / 2 failed** → il caso TextRun **passa**, i due
   fallimenti immagine restano. Quindi:
   * `a78a8a29e` (`batch_is_sound`) causa solo il terzo caso: la sua clausola
     `members.size() < 2` sopprime il batch **degenerato di un solo membro**,
     e la fixture del test è un grafo con il solo nodo TextRun come output.
     Un batch di un membro non fonde nulla: la clausola è una policy
     ragionevole, quindi qui è l'**attesa del test** (proxy `lowered_into_batch`
     per il vero intento "TextRun non passa dal ShapeProcessor") a essere
     superata. La parte di `batch_is_sound` che conta — la contenzione dei
     consumer dentro l'isola — resta invece corretta e copre il bug reale
     documentato nel suo commento.
   * `00be928ab` (`frame_bound_image_source`) causa i fallimenti #1 e #2 e
     **non** tocca il caso TextRun.

Decisione: **il lowering è il difetto nel caso #1/#2** — il gate è troppo
ampio, non implementa il proprio intento dichiarato e ha invalidato un golden
certificato senza rigenerarlo: il test e il golden hanno ragione. Per il caso
#3 il lowering è *nuovo ma difendibile* (niente batch degenerati) e l'attesa
del test è superata: va aggiornato il proxy, non il gate.

Correzione applicata in questo perimetro: `CHECK` → `REQUIRE` sull'indice di
`layer_batches[0]` in `test_frame_graph_compiler_runtime.cpp`. Non è cosmetica:
ha fatto emergere il terzo fallimento latente e ha riportato la suite da 31 casi
eseguiti a 378.

Decisione applicata (opzione **B** — immagini native-only; il fused image path
è ritirato) e caso #3 chiuso aggiornando il proxy del test:

| Artefatto | Modifica |
|---|---|
| `frame_graph_builder_program_detail.hpp` | commento del gate riscritto: dichiara che l'esclusione è **totale** (non un sottoinsieme "frame-bound") e che non esiste più un layer immagine static-compatible; variabile rinominata `image_source_uses_native_path` perché `frame_bound_image_source` non descriveva ció che fa. **Nessun cambio di comportamento** (il golden rigenerato lo prova) |
| `test_frame_graph_compiler_runtime.cpp` | invariante riscritta: `100 Image layers stay on the native path and are never fused` → batch list **vuota**, i 100 layer immagine coperti da operations standalone, nessuna `lowered_into_batch`, output composite ancora eseguito, nessuna op fusa. `CHECK` → `REQUIRE` sull'indice (era SIGSEGV) |
| `frame_graph_program.golden` | rigenerato: `0x610ce49755184814` → `0x65304b41dbf40d8f`, con il motivo del cambio e il valore precedente annotati nel file |
| `test_frame_graph_compiler_validation.cpp` | proxy TextRun sostituito con l'intento reale: il node compila con `ShapeType::None` (bypass ShapeProcessor), possiede ≥1 operation standalone, non è fuso e non compare in nessun batch |

Risultato: `chronon3d_render_graph_tests` **378/378 PASS** (18653 assertion),
contro 31 casi eseguiti / 2 rossi prima.

## Demolition Debt — fused storage-image batch (ritirato)

| Campo | Valore |
|---|---|
| **Owner** | render graph / compiler lane (con la native image path lane) |
| **Reason** | Il fused storage-image batch non ha un contratto descriptor/readback equivalente per gli asset immagine: le immagini sono state spostate su `SourceNode` native path e `is_fusible_layer_node()` le esclude tutte. Restano in piedi i meccanismi del percorso ritirato senza produttore nel compiler, e vanno rimossi solo dopo la certificazione del sostituto. |
| **Exit condition** | native image path certificato: parità pixel e zero layer persi sul corpus overlay (incluso lo scenario `background + foreground + watermark` che ha motivato i due band-aid), più una run video reale. |
| **Equivalence test/gate** | confronto pixel native-path vs baseline certificata sul corpus overlay; `chronon3d_compositor_tests` + `chronon3d_render_graph_tests` verdi. |
| **Removal scope** | ramo immagine di `lower_layer_batches`, percorsi immagine di `node_runner_fused_batch.cpp`, istanze immagine in `CompiledLayerInstance`, `lowered_into_batch` per nodi Source, e i residui di `batch_is_sound` che esistono solo per i batch immagine. |
| **Status** | `ACTIVE` |

## Evidenza di build

```text
linux-video-fast-dev   (Vulkan + CUDA + native FFmpeg + text + daemon)
  cmake --build ... --target chronon3d_cli -j 12   → EXIT 0, 0 errori

linux-fast-dev         (tests ON)
  chronon3d_cache_tests                         146/146 case, 997574 assertion  PASS
  chronon3d_daemon_render_concurrency_tests      13/13 case,     94 assertion  PASS
  chronon3d_mtsdf_font_identity_tests             5/5  case,     36 assertion  PASS
  chronon3d_mtsdf_generator_tests                 7/7  case,    178 assertion  PASS
  chronon3d_backend_registry_tests               19/19 case,    108 assertion  PASS
  chronon3d_compositor_tests                     69/69 case, 613890 assertion  PASS
  chronon3d_render_job_contract_tests            14/14 case,    287 assertion  PASS
  chronon3d_render_graph_tests                  378/378 case, 18653 assert   PASS
    (prima: 31 casi eseguiti, 2 rossi — il SIGSEGV su layer_batches[0] vuoto troncava la suite)

gates
  python3.11 tools/check_architecture.py        GATE_PASS 191/191
  tools/check_test_hygiene.sh                   HYGIENE_PASS 3/3
  tools/check_filename_drift.sh                 nessun drift nuovo sui file toccati
```

Nota: il build video è una verifica di **compilazione**; nessun job video è stato
eseguito su GPU in questa sessione.
