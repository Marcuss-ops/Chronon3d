# TICKET — Hotspot decomposition waves

Status: `ACTIVE`  
Owner: CLI/render maintainers  
Scope: orchestration boundaries, high-churn render paths, and compile-time text
headers identified by the 2026-09 audit.

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
  `build_summary_and_job_sections()` da
  `pipe_timing_sidecar_summary.cpp` in dati tipizzati + serializer JSON;
  mantenere lo schema e aggiungere un test di snapshot/contract.
- [x] **A6 — Encoder open phases.** `NativeAvEncoder::open()` è un
  orchestratore di fasi esplicite (reset, resolve, codec/surface, options,
  codec probe, mux wiring, final validation) con error path conservato.
  `native_av_encoder_open.cpp` in risoluzione codec/preset, scelta surface,
  wiring mux e final validation; ogni fase deve restituire la stessa ragione
  di errore pubblica.
- [x] **A7 — Bitstream target contract.** La policy di target è in
  `gop_target_contract.{hpp,cpp}` e i test coprono target encoder materialized,
  remux, incompatibilità e fail-closed splice.
  `make_bitstream_target_contract()` la policy pura e lasciare nel dispatch
  solo il wiring job/encoder. Coprire success, incompatibilità e fallback
  vietati con test di contratto.

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
  orchestratore sottile; spostare input validation/timestamp normalization in
  helper puri e conservare i 29 casi di
  `test_packet_assembler_contract` come gate di equivalenza. La prima
  estrazione è in `packet_assembler_segments.{hpp,cpp}` per compatibilità
  codec, durata e normalizzazione timestamp; il test contract native resta
  subordinato al preset FFmpeg.

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
  lock/capability gates. La validazione runtime Vulkan/CUDA non è inclusa
  nell'evidence CPU.
- [x] **D2 — Fused runner support.** Gli helper del fused runner vivono nel
  detail TU/header privato e `node_runner.cpp` vede una sola entrypoint
  `detail::execute_fused_batch`; non è stata introdotta una seconda authority.
  `node_runner_fused_batch.cpp` in un detail TU/header privato solo se il
  dependency graph resta unidirezionale e il target compilato non guadagna
  una seconda entrypoint executor.

### Wave E — text header compile-time

- [ ] **E1 — Resource header split.** Ridurre
  `text_render_resources.hpp` separando contratti leggeri, FreeType cache,
  Blend2D/Glyph atlas e scratch state in header focalizzati; mantenere
  `TextRenderResources` come facade di compatibilità durante la migrazione.
- [x] **E2 — Consumer census.** Il census è registrato in
  `docs/evidence/CALLER-CENSUS-CPU.md` e il gate stampa i 33 consumer diretti
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
- Restano aperti: completamento definitivo del glyph-atlas fan-out E1,
  certificazione runtime GPU/MTSDF e same-SHA GitHub CI.

## Progress snapshot — 2026-09-22 CPU wave 2

- A3 production `.inc` debt è a zero in `apps/` e `src/`; restano solo i tre
  harness CUDA di `tools/hwlab/`.
- Clear, Transform e Temporal hanno TU execution dedicati; C4/C5 conservano
  policy/execution TU separati. Il render-graph suite CPU passa.
- E2/E3/E4 hanno census, facade Unicode focalizzata e gate fan-out eseguibile;
  E1 resta aperta perché `TextRenderResources` mantiene ancora il broad
  glyph-atlas/raster-cache aggregator per compatibilità.

## Definition of done

- [ ] Ogni funzione indicata è sotto la soglia concordata dal reviewer oppure
  documentata come orchestratore sottile.
- [ ] Nessuna nuova authority, registry, cache o compatibility path.
- [ ] Ogni split ha test/gate mirato e build del target toccato.
- [ ] I test falliti preesistenti restano esplicitamente distinti dai
  regressi introdotti.
- [ ] `git diff --check`, architecture gate e consumer census passano.
- [ ] Il ticket viene chiuso solo dopo build/test same-tree e verifica dei
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
