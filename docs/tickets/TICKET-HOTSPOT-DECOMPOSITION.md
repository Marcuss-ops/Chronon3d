# TICKET — Hotspot decomposition waves

Status: `ACTIVE`  
Owner: CLI/render maintainers  
Scope: orchestration boundaries, high-churn render paths, and compile-time text
headers identified by the 2026-09 audit.

## Progress snapshot — 2026-10-11 — native CPU gate

- `tools/verify_cpu_only_evidence.sh` passes on the current dirty worktree:
  A1 fail-closed export `33/33` assertions, direct software-composition export
  `25/25`, B3 segment assembly `53/53`, CLI CTest `1/1`, architecture gate
  `1/1`, text-header fan-out `19/22`, and `git diff --check`.
- Building `chronon3d_cli_tests` against the installed FFmpeg 8 headers found
  stale `FF_PROFILE_H264_*` identifiers. Production references and the two
  packet-assembler assertions now use `AV_PROFILE_H264_*`; the full CLI test
  target builds and passes afterward.
- Evidence is `PASS_SAME_TREE`, not same-SHA: the worktree has unrelated and
  pre-existing modifications. GPU/CUDA certification and GitHub same-SHA CI
  remain `NOT_RUN`; this ticket stays active until its GPU canary and remaining
  acceptance evidence are satisfied.

## Progress snapshot — 2026-10-02 — shared-lock contention

- `NodeMemoryTracker` now partitions node records into 16 mutex shards selected
  by node ID. Node live/peak byte aggregates are atomic; temporal sample and
  metadata state retain separate locks. Snapshot/reset acquire node shards in
  ascending order before sample and metadata locks, preserving a consistent
  lock order and lexically sorted report output.
- `FramebufferPool` now has 64 worker-local exact slots. Warm exact reuse and
  return operate on a slot lock, while global retained-byte budget, per-class
  capacity, best-fit eligibility, and LRU eviction still include both local
  slots and central buckets. Cold misses and maintenance retain the central
  authority. Contended slot locks are timed; the uncontended slot route avoids
  clock reads.
- The 8-worker/1,024-acquire probe measured `wait_us=35247`, `hold_us=399`
  before the change. Updated runs ranged from `wait_us=0..34`,
  `hold_us=3..8`; system noise is visible, but lock wait/hold remains far
  below baseline. Hold time covers only contended slot locks; the uncontended
  local route does not read the clock.
- Validation: framebuffer-pool focused suite 22/22 cases and 33,051/33,051
  assertions on Linux CPU (2026-10-11); cross-bucket LRU suite 5/5 cases and
  26/26 assertions. Its shared-worker probe reported `wait_us=1`, `hold_us=8`.
  The earlier full cache suite passed 146/146 cases and 997,574/997,574
  assertions; memory tracker passed 12/12 cases and 111/111 assertions, and
  architecture gate passed 206/206. Prior runs of the worker probe ranged
  from `0/3` through `34/8` µs, all far below pre-change `35247/399` µs.

## Obiettivo

Ridurre le funzioni procedurali e i translation unit ad alta densità senza
creare una seconda authority. Ogni estrazione deve conservare il contratto
esistente, avere un seam testabile e lasciare il comportamento osservabile
invariato.

## Todo actions / acceptance criteria

### Wave A — export, mux e CLI orchestration

- [x] **A1 — Native export contract tests.** I test CPU nativi coprono ora la
  route diretta `render_and_encode_native()` con una composizione software,
  MP4 pubblicato e sidecar; il test di contratto companion copre receipt/SHA e
  il failure path fail-closed nello stesso run.
  `render_and_encode_native()` in
  `apps/chronon3d_cli/commands/video/exporters/video_export_native.cpp`:
  MP4 valido, sidecar emesso, errore/exit path fail-closed e checksum/receipt
  coerenti.
- [x] **A2 — Split native export.** `video_export_native.cpp` è ora un
  orchestratore di 337 righe; setup, warmup, loop, close, finalize e supporto
  telemetry sono in TU/fasi separate, con il target contract risolto fuori
  dall’orchestratore.
  `render_and_encode_native()` (804 righe) in unità per target-contract
  resolution, encoder/session setup, render loop e finalize/publish. La
  risoluzione del target contract deve diventare una funzione pura testabile.
- [x] **A3 — Split `.inc` production.** I 22 frammenti handwritten sotto
  `apps/` e `src/c_api/` sono stati assorbiti nei TU proprietari e rimossi.
  Restano soltanto i tre harness CUDA sotto `tools/hwlab/`, fuori dal gate
  production CPU e subordinati alla certificazione hardware.
- [x] **A4 — Render-plan command TU.** Portare
  `command_render_plan_internal.inc` a `.cpp` con header privato, mantenendo
  gli stessi simboli CLI, registrazione e receipt JSON. Il nuovo TU è
  `command_render_plan_internal.{hpp,cpp}` e il target CLI attivo compila.
- [x] **A5 — Sidecar serializer.** `JobTimings`/le sezioni tipizzate e il
  serializer JSON unico sono coperti dal frozen-key/value/null contract test;
  il test A1 verifica anche l’emissione del sidecar dal route native.
  `build_summary_and_job_sections()` è stato estratto da
  `pipe_timing_sidecar_summary.cpp` in dati tipizzati + serializer JSON;
  lo schema resta coperto dal contract test.
- [x] **A6 — Encoder open phases.** `NativeAvEncoder::open()` è un
  orchestratore di fasi esplicite (reset, resolve, codec/surface, options,
  codec probe, mux wiring, final validation) con error path conservato.
  Le fasi sono in `native_av_encoder_open.cpp`; ogni fase deve restituire la
  stessa ragione di errore pubblica.
- [x] **A7 — Bitstream target contract.** La policy di target è in
  `gop_target_contract.{hpp,cpp}` e i test coprono target encoder materialized,
  remux, incompatibilità e fail-closed splice.
  La policy di `make_bitstream_target_contract()` resta pura; il dispatch
  conserva solo il wiring job/encoder. Success, incompatibilità e fallback
  vietati sono coperti dal test di contratto.

### Wave B — packet/mux boundary

- [x] **B1 — Digest writer seam.** Separare da
  `src/media/video/mux/packet_assembler.cpp` l'hash/AVIO writer
  (`MuxAvioHashWriter`, checksum finale e metriche) dal ciclo di vita di
  `MuxSession`. Il seam è in `packet_assembler_hash_writer.{hpp,cpp}`;
  entrambi i TU passano la compilazione sintattica native-FFmpeg.
- [x] **B2 — MuxSession seam.** Lasciare in `MuxSession` solo apertura,
  stream setup, packet placement, audio resampling, trailer e stato della
  sessione. L'implementazione della sessione è ora in
  `packet_assembler_session.cpp`; l'autorità checksum resta nel seam
  `packet_assembler_hash_writer.{hpp,cpp}`.
- [x] **B3 — Assemble segments.** `assemble_segments()` resta l’orchestratore
  `validate → normalize → execute`, con helper in
  `packet_assembler_segments.{hpp,cpp}`. Il test nativo CPU aggiunto genera due
  segmenti H.264, verifica timestamp monotoni e checksum SHA-256 dell’output.
  Il test contract `test_packet_assembler_contract` resta il gate di
  equivalenza per codec, durata e normalizzazione timestamp.

### Wave C — render node policies

- [x] **C1 — Clear policy.** Estrarre da
  `src/render_graph/nodes/clear_node.cpp::ClearNode::execute()` la decisione
  pura tra native GPU, software e dirty-rect, inclusa la policy strict-mode.
  Il seam native/software è in `clear_policy.{hpp,cpp}` con test per tutte le
  combinazioni di capability; il seam ora espone anche `Reject` per strict
  mode. La policy pura dirty-rect/runtime è ora in `clear_policy.{hpp,cpp}`
  con test di eleggibilità, clip vuoto, clip grande e costo pixel. Il loop
  CPU di copia/clear e la telemetria vivono ora nel TU dedicato
  `clear_node_execution.cpp`; il nodo pubblico è solo dispatch.
- [x] **C2 — Transform policy.** Spezzare
  `src/render_graph/nodes/transform_node.cpp::TransformNode::execute()` in
  normalize/clip, scelta backend e applicazione dell'operazione; aggiungere
  test per foreground scale, affine, clipping e fallback. La scelta backend e
  il contratto puro normalize/clip sono in `transform_policy.{hpp,cpp}` con
  test native/software/reject e test geometrici. Il kernel di rasterizzazione
  è nel TU `transform_node_execution.cpp`, mentre il nodo conserva l'authority
  di stato e il wrapper pubblico.
- [x] **C3 — Temporal scene pipeline.** Ridurre
  `render_scene_via_graph_temporal()` in `src/render_graph/pipeline/scene.cpp`
  a fasi esplicite: frame parameters, graph execution, output lease e
  publication. Conservare i test pipeline esistenti come gate. La policy di
  isolamento dei temporal sample è ora in
  `scene_temporal_policy.{hpp,cpp}` con test; le fasi graph/output/lease sono
  ora nel TU `scene_temporal_execution.cpp` e `scene.cpp` conserva il wrapper.
- [x] **C4 — Fused batch policy.** Spezzare
  `execute_fused_batch_impl()` in
  `src/render_graph/executor/node_runner_fused_batch.cpp` e separare la policy
  di admission/error reporting dal loop di esecuzione. Proteggere con i
  canary GPU text e i test fused/unfused. La classificazione iniziale è ora in
  `fused_batch_policy.{hpp,cpp}` con test per skip/text/retired; il loop
  execution e la telemetria sono nel detail TU separato.
- [x] **C5 — Cached GPU text.** Spezzare `draw_cached_text_run()` in
  capability/selector decision, atlas lookup, geometry emission e submit;
  nessuna modifica alla compatibilità CPU fallback senza canary equivalente.
  La decisione iniziale è ora in `gpu_text_cached_policy.{hpp,cpp}` con test
  per rejection, MTSDF e packed fallback; atlas/geometry/submit sono nel TU
  execution. La correttezza runtime GPU resta subordinata all'hardware.

### Wave D — backend boundaries

- [x] **D1 — Vulkan surface API (structural only).** Dividere
  `vulkan_backend_surface_api.cpp` tra Vulkan-native surface/upload/composite
  e CUDA interop, mantenendo un solo `VulkanBackend` authority e gli stessi
  lock/capability gates. Gli adapter pubblici sono in
  `vulkan_backend_cuda_surface_api_private.cpp`, mentre lo stato `Impl` resta
  in `vulkan_backend_cuda_export_private.cpp`. La validazione runtime
  Vulkan/CUDA non è inclusa nell'evidence CPU.
- [x] **D2 — Fused runner support.** Gli helper del fused runner vivono nel
  detail TU/header privato e `node_runner.cpp` vede una sola entrypoint
  `detail::execute_fused_batch`; non è stata introdotta una seconda authority.
  `node_runner_fused_batch.cpp` in un detail TU/header privato solo se il
  dependency graph resta unidirezionale e il target compilato non guadagna
  una seconda entrypoint executor.

### Wave E — text header compile-time

- [x] **E1 — Resource header split.** Ridurre
  `text_render_resources.hpp` separando contratti leggeri, FreeType cache,
  Blend2D/Glyph atlas e scratch state in header focalizzati; mantenere
  `TextRenderResources` come facade di compatibilità durante la migrazione.
  Il fan-out broad è stato ridotto e ratchettato a 22 consumer legittimi; la
  ownership glyph-atlas/raster-cache resta una sola authority runtime.
- [x] **E2 — Consumer census.** Il census è registrato in
  `docs/evidence/CALLER-CENSUS-CPU.md` e il gate stampa i 22 consumer diretti
  dell'aggregator, senza rimuovere include transitivi alla cieca.
- [x] **E3 — Unicode header split.** UTF-8 e grapheme sono superfici
  focalizzate (`text_unicode_utf8.hpp` e `text_unicode_grapheme.hpp`), con
  `text_unicode_utils.hpp` mantenuto come facade compatibile senza consumer
  diretti.
- [x] **E4 — Include/build gate.** `check_text_header_fanout.py` misura il
  fan-out e impedisce consumer della facade Unicode; il gate è incluso nella
  evidence CPU insieme a build e test.

## Ordine operativo

1. A1–A4: export native e primo `.inc`, perché sono il rischio operativo più
   alto e attualmente privi di rete sufficiente.
2. B1–B3: mux, sfruttando i test contract già presenti.
3. C1–C5: policy pure nei nodi e nel path testo, con canary/regression gate.
4. A5–A7: sidecar e setup encoder/bitstream contract.
5. D1–D2: backend Vulkan/executor solo dopo baseline verde.
6. E1–E4: header text e misurazione compile-time.

## Progress snapshot — 2026-09-20

- `resolve_gop_target_contract()` è stato estratto in
  `apps/chronon3d_cli/utils/video/gop_target_contract.{hpp,cpp}`; il dispatch
  non contiene più la policy di risoluzione.
- `command_render_plan_internal.inc` è stato convertito in
  `command_render_plan_internal.{hpp,cpp}`; `chronon3d_cli_render` e i test CLI
  compilano senza l'include testuale di quel comando.
- `chronon3d_cli_render` compila con `-j10`.
- `chronon3d_cli_tests` compila e il test CTest registrato passa.
- `clear_policy.{hpp,cpp}` separa la decisione native/software di `ClearNode`;
  il test mirato copre anche dirty-rect, clip vuoto/grande e costo pixel;
  strict mode rifiuta prima del percorso software quando mancano backend o
  registry. Il loop pixel/telemetria resta aperto nella stessa action.
- `transform_policy.{hpp,cpp}` separa la decisione native/software/reject di
  `TransformNode` e ora centralizza normalize/clip; il test mirato copre
  bbox offset, clipping e intersezione vuota.
- `test_packet_assembler_contract.cpp` è registrato nel target
  `chronon3d_cli_tests`; la verifica runtime FFmpeg resta subordinata alla
  preset video/native-FFmpeg.
- `packet_assembler_segments.{hpp,cpp}` contiene ora i helper di contratto
  codec/timestamp; `packet_assembler.cpp` li usa via namespace `detail` e i
  due TU passano syntax-check native-FFmpeg.
- `fused_batch_policy.{hpp,cpp}` classifica l'admission iniziale del fused
  runner; il test mirato copre skip, text e retired senza attivare il backend
  GPU.
- `gpu_text_cached_policy.{hpp,cpp}` classifica i gate iniziali del cached text
  path; il test mirato copre rejection, MTSDF e packed fallback.
- `scene_temporal_policy.{hpp,cpp}` estrae la normalizzazione dei settings per
  i sample isolati; il test verifica che side-effect/diagnostic flags siano
  disabilitati e che i parametri non correlati restino invariati.
- I cinque seam policy del render graph passano tutti i test mirati nel
  preset veloce: Clear `6/6`, Transform `8/8`, Fused `4/4`, Cached GPU text
  `6/6`, temporal settings `10/10`.
- `MuxSession` è stato spostato fuori da `packet_assembler.cpp` in un TU
  dedicato; `chronon3d_media_native` e `chronon3d_cli_video_export` compilano
  con il nuovo confine.
- Lo split degli header text ora include esplicitamente
  `text_render_resources.hpp` nei quattro stage software che usano la
  forward declaration; il target render-graph torna compilabile nel preset
  senza GPU.

## Progress snapshot — 2026-09-21

- `video_export_native_support.{hpp,cpp}` contiene ora l’ammissione del
  `DeviceScheduler` locale, inclusa la probe capability NVENC/NVDEC e la
  risoluzione dei render-job slots; `render_and_encode_native()` conserva il
  coordinamento della sessione e del loop.
- `text_run_node_geometry.inc` è stato convertito in
  `text_run_node_geometry.cpp`: costruttore, placement refresh, world matrix,
  predicted bbox e cache key restano nello stesso contratto ma non sono più
  inclusi testualmente nel TU dell’orchestratore.
- La callback AVIO del mux è stata allineata alla firma const richiesta da
  FFmpeg corrente; `chronon3d_cli_render` e `chronon3d_cli_video_export`
  compilano con `-j10`.
- Il focused render-graph run passa con 9 test case e 47 assertion; le prove
  GPU/CUDA end-to-end e gli split Vulkan/CUDA restano subordinati alla macchina
  con hardware.
- Il cached GPU text route compila i selector una volta per run e risolve
  `SelectorWeightParams` una volta per frame; il loop glyph usa solo il
  consumer canonico `selector_weight()` e non il legacy evaluator.
- L'overload preparato mantiene l'equivalenza tra binding per-glyph e binding
  per-frame; il lowering GPU rifiuta ora anche easing non-default, che non è
  rappresentato nel payload `TextRunDynamic`.
- Il gate architetturale `text_selector_legacy_runtime_consumer` impedisce ai
  consumer di produzione di reintrodurre `evaluate_selector(s)`. Il focused
  selector run passa con 8 test case e 70 assertion; il render-graph focused
  run passa con 5 test case e 518 assertion.

## Progress snapshot — 2026-09-22

- `PreparedText` è ora payload canonico immutabile: il campo derivato
  `prepared_run` e l’invalidazione implicita nel builder sono stati rimossi.
  La materializzazione di `PreparedTextRun` resta nella factory; il riuso del
  layout resta nella cache canonica di `FontEngine`. I test CPU di identity
  mutation e frame-stability passano.
- A1 è coperto da due test nativi CPU: route reale
  `render_and_encode_native()` su composizione software e contratto companion
  MP4/sidecar/receipt/SHA/failure. Il preset `linux-video-test` ha passato
  entrambi i casi (`21/21` assertion sulla route, `33/33` sul contratto).
- B3 ha un test nativo CPU di due segmenti FFmpeg: `53/53` assertion,
  checksum dell’artefatto assemblato e timestamp video monotoni.
- La firma del callback AVIO è stata allineata a libavformat corrente
  (`const uint8_t*`); la build native-FFmpeg CLI passa.
- `tools/verify_cpu_only_evidence.sh` registra SHA sorgente, configurazione
  native-FFmpeg, build, A1, B3, CLI suite, architecture inc gate e diff
  hygiene. `docs/evidence/CPU-ONLY-EVIDENCE.md` documenta il perimetro.
- `docs/evidence/CALLER-CENSUS-CPU.md` fissa il census Camera/RenderPlan e
  conferma che non esiste una seconda authority dimostrata da rimuovere.
- Restano aperti: certificazione runtime GPU/MTSDF e same-SHA GitHub CI.

## Progress snapshot — 2026-09-22 CPU wave 2

- A3 production `.inc` debt è a zero in `apps/` e `src/`; restano solo i tre
  harness CUDA di `tools/hwlab/`.
- Clear, Transform e Temporal hanno TU execution dedicati; C4/C5 conservano
  policy/execution TU separati. Il render-graph suite CPU passa.
- E1/E2/E3/E4 hanno ora header focalizzati, census e gate fan-out eseguibile;
  il broad aggregator resta una facade compatibile con 22 consumer ratchettati,
  senza introdurre una seconda authority glyph-atlas/raster-cache.

## Definition of done

- [x] Ogni funzione indicata è sotto la soglia concordata dal reviewer oppure
  documentata come orchestratore sottile.
- [x] Nessuna nuova authority, registry, cache o compatibility path.
- [x] Ogni split ha test/gate mirato e build del target toccato.
- [x] I test falliti preesistenti restano esplicitamente distinti dai
  regressi introdotti.
- [x] `git diff --check`, architecture gate e consumer census passano.
- [x] Il ticket viene chiuso solo dopo build/test same-tree e verifica dei
  simboli/caller residui.

## Demolition debt

- **Owner:** CLI/render maintainers.
- **Reason:** concentrazione di policy e orchestrazione in funzioni ad alto
  churn e include transitive pesanti.
- **Exit condition:** ogni seam estratto è usato dal percorso production e la
  funzione/file originale non contiene più responsabilità estranee.
- **Equivalence gate:** contract tests export/mux, pipeline/render-graph
  tests, GPU text canary, architecture gate e build target-specific.
- **Removal scope:** include `.inc` non più necessari, helper duplicati e
  bridge temporanei introdotti solo per la migrazione.
- **Status:** `ACTIVE`; il primo seam di Wave A è stato estratto e compilato,
  mentre il test diretto e le successive estrazioni restano aperti.

## Progress snapshot — 2026-10-02

- L'analisi/bake di `audio_react` è stata estratta da `render_plan_compiler.cpp`
  in `audio_react_plan_baker.{hpp,cpp}`; `compile_render_plan()` mantiene il
  coordinamento e la compilazione del target `chronon3d_render_plan` passa.
- Il limite runtime della banda audio ora coincide con il Nyquist effettivo
  dell'analisi mono 48 kHz (`24000 Hz`), anche nel decoder e nello schema v3;
  richieste prima accettate all'ingresso fallivano solo durante il bake.
- Il dispatch per livello emette timing/log per nodo solo con
  `CHRONON3D_DIAG_EXEC_LOG` abilitato; nel percorso normale non legge il clock
  né formatta la riga diagnostica.
- La materializzazione `EffectKindPlan → EffectParams` è stata isolata in
  `render_plan_effect_preparation.{hpp,cpp}` con switch esaustivo coperto da
  `-Werror=switch`; il compiler TU mantiene solo il coordinamento.

- Il decoder effetti è stato separato da `render_plan_decoder_layer.cpp` in
  `render_plan_decoder_effects.cpp`; mapping del tipo effetto e parsing sono
  un solo modulo, con il mapping condiviso esposto dal detail header esistente.
- L'ID memoria nodo ora resta inline in `ScopedNodeMemory`; il tracker evita
  heap per gli ID canonici anche quando non rientrano in SSO.

## Progress snapshot — 2026-10-02 (tracker/pool contention)

- `ScopedNodeMemory` effettua ora una presa del lock del tracker all'ingresso
  e una all'uscita: `begin_scope()` fonde registrazione/acquire;
  `finish_scope()` fonde fold delle metriche/release. `set_live_bytes()` usa
  una singola transizione atomica rispetto agli snapshot.
- I test mirati del tracker passano. Il probe preesistente del pool a 8 worker
  (1024 acquire esatti, classe 64×64) misura `lock_wait_us=35247` e
  `lock_hold_wall_us=399`: il rapporto mostra contesa reale e invalida la
  precedente decisione G48 di non sharding. La mappa best-fit, il budget LRU
  globale e i limiti per classe sono ancora sotto un solo mutex; richiedono una
  wave di sharding dedicata con misure prima/dopo e preservazione di tali
  invarianti. Nessuno sharding del pool è stato applicato in questa tranche.
