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

- [ ] **A1 — Native export contract tests.** Aggiungere test diretti per
  `render_and_encode_native()` in
  `apps/chronon3d_cli/commands/video/exporters/video_export_native.cpp`:
  MP4 valido, sidecar emesso, errore/exit path fail-closed e checksum/receipt
  coerenti.
- [ ] **A2 — Split native export.** Spezzare
  `render_and_encode_native()` (804 righe) in unità per target-contract
  resolution, encoder/session setup, render loop e finalize/publish. La
  risoluzione del target contract deve diventare una funzione pura testabile.
- [ ] **A3 — Split `.inc` production.** Convertire i 26 include `*.inc` in
  translation unit/header propri, uno alla volta, iniziando da
  `apps/chronon3d_cli/commands/render/command_render_plan_internal.inc`.
  Esclusi dal primo pass i quattro `benchmark_saturation_*` salvo necessità
  del build graph.
- [x] **A4 — Render-plan command TU.** Portare
  `command_render_plan_internal.inc` a `.cpp` con header privato, mantenendo
  gli stessi simboli CLI, registrazione e receipt JSON. Il nuovo TU è
  `command_render_plan_internal.{hpp,cpp}` e il target CLI attivo compila.
- [ ] **A5 — Sidecar serializer.** Estrarre
  `build_summary_and_job_sections()` da
  `pipe_timing_sidecar_summary.cpp` in dati tipizzati + serializer JSON;
  mantenere lo schema e aggiungere un test di snapshot/contract.
- [ ] **A6 — Encoder open phases.** Spezzare `NativeAvEncoder::open()` in
  `native_av_encoder_open.cpp` in risoluzione codec/preset, scelta surface,
  wiring mux e final validation; ogni fase deve restituire la stessa ragione
  di errore pubblica.
- [ ] **A7 — Bitstream target contract.** Estrarre da
  `make_bitstream_target_contract()` la policy pura e lasciare nel dispatch
  solo il wiring job/encoder. Coprire success, incompatibilità e fallback
  vietati con test di contratto.

### Wave B — packet/mux boundary

- [x] **B1 — Digest writer seam.** Separare da
  `src/media/video/mux/packet_assembler.cpp` l'hash/AVIO writer
  (`MuxAvioHashWriter`, checksum finale e metriche) dal ciclo di vita di
  `MuxSession`. Il seam è in `packet_assembler_hash_writer.{hpp,cpp}`;
  entrambi i TU passano la compilazione sintattica native-FFmpeg.
- [ ] **B2 — MuxSession seam.** Lasciare in `MuxSession` solo apertura,
  stream setup, packet placement, audio resampling, trailer e stato della
  sessione. Non duplicare l'autorità di checksum.
- [ ] **B3 — Assemble segments.** Mantenere `assemble_segments()` come
  orchestratore sottile; spostare input validation/timestamp normalization in
  helper puri e conservare i 29 casi di
  `test_packet_assembler_contract` come gate di equivalenza. La prima
  estrazione è in `packet_assembler_segments.{hpp,cpp}` per compatibilità
  codec, durata e normalizzazione timestamp; il test contract native resta
  subordinato al preset FFmpeg.

### Wave C — render node policies

- [ ] **C1 — Clear policy.** Estrarre da
  `src/render_graph/nodes/clear_node.cpp::ClearNode::execute()` la decisione
  pura tra native GPU, software e dirty-rect, inclusa la policy strict-mode.
  Il seam native/software è in `clear_policy.{hpp,cpp}` con test per tutte le
  combinazioni di capability; il seam ora espone anche `Reject` per strict
  mode. La policy dirty-rect/runtime resta nel follow-up C1b prima della
  chiusura completa del nodo.
- [ ] **C2 — Transform policy.** Spezzare
  `src/render_graph/nodes/transform_node.cpp::TransformNode::execute()` in
  normalize/clip, scelta backend e applicazione dell'operazione; aggiungere
  test per foreground scale, affine, clipping e fallback. La prima cucitura
  della scelta backend è in `transform_policy.{hpp,cpp}` con 8 assertion
  native/software/reject; restano da estrarre normalize/clip e da aggiungere
  i test geometrici/canary.
- [ ] **C3 — Temporal scene pipeline.** Ridurre
  `render_scene_via_graph_temporal()` in `src/render_graph/pipeline/scene.cpp`
  a fasi esplicite: frame parameters, graph execution, output lease e
  publication. Conservare i test pipeline esistenti come gate. La policy di
  isolamento dei temporal sample è ora in
  `scene_temporal_policy.{hpp,cpp}` con test; le fasi graph/output/lease
  restano già delegate ma il corpo principale deve ancora essere ridotto.
- [ ] **C4 — Fused batch policy.** Spezzare
  `execute_fused_batch_impl()` in
  `src/render_graph/executor/node_runner_fused_batch.cpp` e separare la policy
  di admission/error reporting dal loop di esecuzione. Proteggere con i
  canary GPU text e i test fused/unfused. La classificazione iniziale è ora in
  `fused_batch_policy.{hpp,cpp}` con test per skip/text/retired; resta da
  estrarre la policy di capability/error reporting interna al percorso text.
- [ ] **C5 — Cached GPU text.** Spezzare `draw_cached_text_run()` in
  capability/selector decision, atlas lookup, geometry emission e submit;
  nessuna modifica alla compatibilità CPU fallback senza canary equivalente.
  La decisione iniziale è ora in `gpu_text_cached_policy.{hpp,cpp}` con test
  per rejection, MTSDF e packed fallback; atlas/geometry/submit restano da
  estrarre.

### Wave D — backend boundaries

- [ ] **D1 — Vulkan surface API.** Dividere
  `vulkan_backend_surface_api.cpp` tra Vulkan-native surface/upload/composite
  e CUDA interop, mantenendo un solo `VulkanBackend` authority e gli stessi
  lock/capability gates.
- [ ] **D2 — Fused runner support.** Spostare gli helper locali di
  `node_runner_fused_batch.cpp` in un detail TU/header privato solo se il
  dependency graph resta unidirezionale e il target compilato non guadagna
  una seconda entrypoint executor.

### Wave E — text header compile-time

- [ ] **E1 — Resource header split.** Ridurre
  `text_render_resources.hpp` separando contratti leggeri, FreeType cache,
  Blend2D/Glyph atlas e scratch state in header focalizzati; mantenere
  `TextRenderResources` come facade di compatibilità durante la migrazione.
- [ ] **E2 — Consumer census.** Prima di rimuovere include transitivi,
  enumerare ogni consumer con `git grep` e aggiornare tutti i consumer nella
  stessa modifica.
- [ ] **E3 — Unicode header split.** Dividere
  `text_unicode_utils.hpp` tra UTF-8 codec, grapheme navigation e helper di
  layout; preservare namespace e simboli pubblici, aggiungendo test di
  equivalenza per cluster boundary/invalid UTF-8.
- [ ] **E4 — Include/build gate.** Verificare tempi e fan-out dopo ogni split;
  non dichiarare il lavoro chiuso se un header nuovo reintroduce la stessa
  dipendenza pesante tramite include transitivo.

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
  il test mirato passa (6 assertion) e strict mode rifiuta prima del
  percorso software quando mancano backend o registry. La parte dirty-rect
  resta aperta nella stessa action.
- `transform_policy.{hpp,cpp}` separa la decisione native/software/reject di
  `TransformNode`; il test mirato passa (2 casi, 8 assertion). La parte
  normalize/clip e i test geometrici restano aperti.
- Il test diretto del nuovo seam resta aperto perché la preset corrente non
  registra `test_packet_assembler_contract.cpp` nel target CLI attivo.
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
