# TICKET-RENDER-STACK-HARDENING — sync audit C3D, certificazione same-SHA CM3D↔C3D, chiusura bridge e hot reload dev-only

**Stato:** OPEN (2026-11-06)
**Priorità:** P0
**Data:** 2026-11-06
**Ambito:** Chronon3d (backend Vulkan) + ChrononMotion3D (confine RenderPlan) + certificazione della coppia
**Owner:** C3D Vulkan backend owner (W1, W5, W7); CM3D motion-boundary owner (W2, W3, W4, W6). Il ticket è un programma di coordinamento, non una feature.
**Tipo:** program ticket — non autorizza nuove dipendenze, nuovi renderer o nuove authority.

## Contesto e decisione

Questo ticket nasce dalla valutazione delle repository citate come riferimento per un motore Vulkan
(`SaschaWillems/Vulkan`, `VulkanMemoryAllocator`, `simple_vulkan_synchronization`, `Granite`,
`glslang`/`SPIRV-Cross`, ray tracing NVIDIA, FidelityFX) e dal controllo dello stato reale dei due
progetti.

La conclusione è che **Chronon3D non deve importare quello stack**: VMA è già integrato, Sync2 e i
timeline semaphore sono già richiesti, il render graph compilato e il transition stream esistono già,
e la pipeline shader è già build-time con `glslangValidator → spirv-val → spirv-opt` più
SPIRV-Reflect per l'ABI. Il lavoro utile non è aggiungere un secondo allocatore, una seconda authority
di sincronizzazione o un secondo render graph: è **hardening, certificazione e chiusura dei gap già
tracciati**.

ChrononMotion3D ha un confine diverso: il core `chrononmotion` è dependency-free e renderer-free per
contratto (`tests/check_motion_boundary.cmake` vieta `vulkan`, `opengl`, `glsl`, `shader`,
`swapchain`, `vkcmd`, `glfw` in `include/`, `src/`, `tests/` e nel CMake root). `render/` è un modulo
separato, **OpenGL only**, opt-in con `CHRONONMOTION_BUILD_RENDER=OFF`, e `tests/check_render_scope.cmake`
lo classifica come look-dev/proof harness, mai renderer di produzione. Quindi VMA, Tobski, Granite,
glslang/SPIRV-Cross, ray tracing e FidelityFX non hanno un consumatore nel core CM3D e non devono
entrarci. L'unica eccezione sensata è un hot reload GLSL dev-only dentro `render/`, non nel core.

### Cosa non si integra (decision record)

| Repository | Decisione | Motivo |
|---|---|---|
| `SaschaWillems/Vulkan` | Riferimento, mai vendorizzato | È una raccolta di sample Vulkan; C3D ha già compute, post-processing e pipeline proprie. PBR/IBL, deferred/subpass e swapchain non sono il core di un compositor headless. |
| `VulkanMemoryAllocator` | Già integrato, non re-integrare | `VulkanMemoryManager` usa VMA con `VMA_MEMORY_USAGE_AUTO`, classi host coerenti, allocazioni external/dedicated per CUDA e budget. La deframmentazione con allocazioni exportable/dedicated è un rischio, non un guadagno automatico. |
| `simple_vulkan_synchronization` | Checklist di audit, non dipendenza | È una single-header alpha.9 che copre pipeline barrier ed eventi, ma non semafori, fence, timeline o render pass. C3D ha bisogno di timeline semaphore e ownership transfer esterni per CUDA; una seconda authority sarebbe una regressione. |
| `Granite` | Studio architetturale, non trapianto | Il barrier compiler esiste già in `CompiledResourceTable`. Il valore residuo è nelle idee (transient aliasing, scheduling, descriptor management), non nel codice. |
| `glslang` | Già usato in build; runtime solo per hot reload dev | Gli shader release restano SPIR-V embedded. Compilare a runtime in produzione cambierebbe determinismo, startup e superficie d'attacco senza un requisito di prodotto. |
| `SPIRV-Cross` | Non necessario | SPIRV-Reflect è già l'authority di reflection e l'ABI è testata. SPIRV-Cross servirebbe solo per cross-compilare verso MSL/HLSL, che non è un requisito attuale. |
| Ray tracing NVIDIA / FidelityFX | Fuori scope | Richiedono un renderer e un requisito di prodotto (RT shadows/GI, FSR/frame generation) che un renderer offline deterministico non ha. Frame generation sarebbe incompatibile con il determinismo del frame. |

## W1 — C3D: audit di sincronizzazione Vulkan (P0)

**Obiettivo:** dimostrare la copertura degli hazard e togliere solo le barrier globali che non hanno
più un consumatore reale, senza sostituire la authority canonica.

**File di partenza:**

- `src/backends/vulkan/vulkan_backend_sync_private.cpp`
- `src/backends/vulkan/vulkan_backend_submission_private.cpp`
- `src/backends/vulkan/vulkan_surface_upload_private.cpp`
- `src/backends/vulkan/vulkan_backend_impl.hpp`
- `src/backends/vulkan/vulkan_backend_lifecycle.cpp`
- `docs/architecture/VULKAN-AUTHORITY-MAP.md`

**Attività:**

- [ ] Aggiungere una lane SyncVal dedicata, con layer e versione pinnati, sul corpus:
      una sequenza 1080p con testo/immagine, una run CUDA/NVENC e la coppia camera già usata dai
      golden. Registrare host, GPU, driver, layer, comando e output.
- [ ] Verificare `VulkanBackend::synchronize_native_dependencies()`: il memory barrier
      `ALL_COMMANDS`/`MEMORY_WRITE` è ancora necessario per dipendenze native (CUDA external
      semaphore, asset fuori dal piano)? Se sì, restringerlo e documentare il residuo; se no,
      rimuoverlo con una prova SyncVal prima/dopo.
- [ ] Verificare `emit_unplanned_compute_sync()`: le transizioni `General` read/write per le immagini
      non pianificate possono diventare una dipendenza upload→compute precisa? Ogni immagine che
      resta fuori dal piano deve avere un motivo e un owner.
- [ ] Verificare layout transition, subresource range e queue ownership transfer per le surface
      esterne/CUDA; nessun uso improprio di `VK_QUEUE_FAMILY_EXTERNAL`.
- [ ] Aggiungere un gate ripetibile (script o test target) che fallisca su nuovi errori SyncVal sul
      corpus, senza skippare casi e senza sopprimere messaggi.
- [ ] Misurare l'effetto sulle performance con tre run, stesso preset e stesso SHA. Una differenza
      dentro il rumore run-to-run non è una regressione; una differenza misurata va spiegata.

**Accettazione:**

- [ ] Zero errori SyncVal sul corpus definito, con log allegato.
- [ ] Ogni barrier globale residua ha owner, motivo, exit condition e test di equivalenza
      (Demolition Debt se temporanea). Nessuna soppressione del layer.
- [ ] `chronon3d_backend_registry_tests`, `chronon3d_compositor_tests`,
      `chronon3d_render_job_contract_tests` e `chronon3d_render_graph_tests` verdi.
- [ ] Evidenza in `docs/evidence/` con comandi, output e confronto prima/dopo.

## W2 — CM3D↔C3D: certificazione same-SHA (P0)

**Obiettivo:** trasformare le prove locali in una coppia CM3D/C3D pulita, stesso SHA, suite completa
e gate camera eseguiti da CI.

**Stato di partenza da rispettare:**

- `ChrononMotion3D/docs/tickets/TICKET-CM3D-CI-ADAPTER-20261106.md` è **CLOSED come codice**: la
  corsia adapter esegue la suite completa, ma l'esecuzione GitHub e la certificazione same-SHA
  restano il gate.
- `ChrononMotion3D/docs/tickets/TICKET-CM3D-REMAINING-WORK-20261106.md` righe 5, 11 e 16 elencano
  CI adapter, gate camera ADR 2–6 e verifiche di chiusura come ancora aperti.
- La suite CM3D con adapter è registrata a 67/67; non dichiarare PASS da un albero dirty o da un
  pair non congelato.

**Attività:**

- [ ] Congelare due SHA puliti, C3D e CM3D, e registrare il pair nel ticket.
- [ ] Eseguire la suite CM3D completa con adapter: `ctest --preset dev --output-on-failure`,
      `chrononmotion_boundary`, `chrononmotion_render_scope`, camera bake/parity.
- [ ] Eseguire la lane adapter C3D (`chronon3d_motion_plan_adapter_tests`), camera parity, C ABI e
      renderer core sullo stesso pair.
- [ ] Eseguire T13/T15 nella lane strict-native e conservare evidenza di native surface, zero
      readback/staging e 150/150 frame.
- [ ] Far girare la CI GitHub sullo stesso pair e registrare run ID, job e conclusioni.

**Accettazione:**

- [ ] Coppia CM3D/C3D stesso SHA, alberi puliti, suite completa verde.
- [ ] Gate camera ADR 2–6 chiusi o formalmente deferiti con owner, motivo ed exit condition.
- [ ] T13/T15 con evidenza strict-native valida; nessun `PARTIAL` senza owner e prossimo passo.
- [ ] Nessun test skippato o rimosso per far passare la certificazione.

## W3 — RenderPlan / highlight bridge: chiusura o deferimento formale (P1)

**Fonte:** `ChrononMotion3D/docs/tickets/TICKET-CM3D-REMAINING-WORK-20261106.md` §4.

Chiudere, o deferire formalmente con owner ed exit condition, i punti ancora aperti:

- [ ] CI same-SHA del bridge.
- [ ] Alpha edge e occlusione + DoF su scena animata completa.
- [ ] Fault-injection nativa end-to-end su errori GPU/allocazione/sync.
- [ ] Depth per-pixel della mesh inline verso il compositing C3D.
- [ ] Copertura PBR/produzione oltre la fixture.
- [ ] Review umana del video completo e dei bordi alpha.
- [ ] CTest completa sul pair corrente.
- [ ] Rotta stills `-o file.png` con mesh inline (difetto C3D tracciato).
- [ ] `MATERIAL_PBR_V1`: sampling software V3 lato C3D.

**Regola:** un campo serializzato non chiude una feature. La chiusura richiede un consumatore che
cambi i pixel e una prova di parità o un deferimento esplicito.

## W4 — Shadow blur/opacity e motion blur per-layer (P1)

**Fonte:** `ChrononMotion3D/docs/tickets/TICKET-CM3D-REMAINING-WORK-20261106.md` §2/§3 e
`ChrononMotion3D/docs/tickets/TICKET-CM3D-PER-LAYER-BLUR-20261106.md`.

**Shadow blur/opacity:**

- [ ] Contratto: unità, range, default, interazione con lo shadow di scena.
- [ ] Capability dichiarata dal consumer; il lowering emette solo con capability presente.
- [ ] Kernel C3D per-layer che cambia i pixel, con test di bordo e sovrapposizione.
- [ ] Test CM3D di emissione, rifiuto senza capability e round-trip scena.
- [ ] Parità software/Vulkan e review visiva.

**Per-layer motion blur:**

- [ ] Contratto del campo V3: nome, unità (angolo/gradi), campioni, default, fail-closed.
- [ ] `Layer` authoring + serializzazione, senza duplicare `MotionScene::setShutter`.
- [ ] Lowering solo con capability; nessun default che armi un kernel assente.
- [ ] Kernel C3D e parità software/Vulkan misurata.
- [ ] Test CM3D di emissione e rifiuto fuori range.

**Divieto:** non aggiungere `shadow_blur`, `shadow_opacity` o un flag per-layer al wire senza il
kernel C3D e senza la capability. Non mappare l'opacità sull'alpha del layer o il blur sul blur di
scena.

## W5 — C3D: shader hot reload dev-only (P1)

**Obiettivo:** velocizzare lo sviluppo dei 27 shader compute senza cambiare il percorso release.

**Design preferito:**

- [ ] Opzione `CHRONON3D_ENABLE_SHADER_HOT_RELOAD`, default `OFF`, abilitata solo nei preset dev.
- [ ] Compilazione tramite subprocess `glslangValidator` + `spirv-val` + `spirv-opt` (stesso
      toolchain della build), per non linkare glslang nel runtime. Se si sceglie libglslang, serve
      un ADR e una motivazione.
- [ ] Watch di `src/backends/vulkan/shaders/*.comp` con `efsw` (già disponibile per
      `CHRONON3D_ENABLE_DEV_WATCH`) e debounce.
- [ ] Validazione ABI con SPIRV-Reflect contro i binding attesi; ricreazione della sola
      `VkPipeline` con la `VkPipelineCache` esistente. Se il layout descriptor cambia, fail-closed
      finché non esiste un percorso esplicito di ricreazione layout.
- [ ] Release: SPIR-V embedded invariato, nessun compilatore runtime, nessuna nuova dipendenza
      pubblica.

**Accettazione:**

- [ ] Modifica di uno shader → il frame successivo cambia senza riavviare il processo.
- [ ] Shader invalido o ABI incompatibile → errore tipizzato, vecchia pipeline mantenuta, nessun
      crash.
- [ ] Build release senza hot reload e senza glslang linkato; gate/CI che lo verifica.
- [ ] Evidenza con diff dello shader, hash/pixel delta e tempi di reload.

## W6 — CM3D: GLSL hot reload nel solo `render/` (P2, opzionale)

**Obiettivo:** se e solo se il look-dev è misurato come collo di bottiglia, velocizzare il harness
OpenGL senza toccare il core.

- [ ] Implementazione confinata a `ChrononMotion3D/render/`.
- [ ] Nessuna nuova dipendenza: polling `std::filesystem` oppure `efsw` con ADR.
- [ ] Ricarica dei sorgenti `.glsl` e ricreazione dei programmi GL; nessun percorso runtime nel core.
- [ ] `chrononmotion_boundary` e `chrononmotion_render_scope` restano verdi.
- [ ] Build release di CM3D invariata.

**Nota:** il core `chrononmotion` non deve acquisire `glsl`, `shader`, `opengl`, `glfw` o `vulkan`.

## W7 — C3D: valutazione VRAM aliasing / async compute (P2)

**Obiettivo:** decidere con una misura, non con un'impressione.

- [ ] Misurare il picco VRAM su una composizione 4K rappresentativa, con e senza asset/text
      pesanti, usando la telemetria VMA esistente.
- [ ] Valutare l'aliasing fisico dei transient (slot di `CompiledResourceTable` + VMA) solo se il
      picco mostra pressione reale. Non deframmentare allocazioni external/dedicated CUDA.
- [ ] Valutare una compute queue o una transfer queue separata solo con un bisogno misurato:
      oggi il backend ha una queue family, una submission authority serializzata e un ring di 6
      slot.
- [ ] Registrare il verdetto go/no-go con numeri; nessun codice prima del go.

**Contesto misurato:** il benchmark B2 corrente è encoder-bound (`avcodec_send_frame` ~3.76 ms/frame
contro ~0.30 ms del render thread). La VRAM e l'async compute non sono la priorità di throughput
finché quel dato resta vero.

## Sequenza consigliata

| Fase | Lavoro | Dipendenza | Output |
|---|---|---|---|
| 1 | W1 + W2 in parallelo | nessuna | SyncVal pulito + coppia same-SHA |
| 2 | W3 + W4 | W2 | Bridge chiuso o deferito; kernel shadow/blur |
| 3 | W5 | W1 | Hot reload dev-only con release invariata |
| 4 | W6 | solo se misurato | Harness GL più veloce, core invariato |
| 5 | W7 | W2/W3 verdi + profilo | Decisione VRAM/queue con numeri |

## Criteri di accettazione del ticket

- [ ] W1 e W2 sono PASS con evidenza same-SHA.
- [ ] W3 e W4 sono chiuse oppure formalmente deferite con owner, motivo ed exit condition.
- [ ] W5 è accettata dev-only e non cambia il percorso release.
- [ ] W6 è no-op oppure confinata a `render/`.
- [ ] W7 produce un verdetto misurato, non un refactor anticipato.
- [ ] Nessuna nuova dipendenza o authority: architecture gate e `chrononmotion_boundary`/
      `chrononmotion_render_scope` verdi.
- [ ] `FOLLOWUP_TICKETS.md` aggiornato con una sola riga; i canonici non contengono cronaca.

## Non-goals e guardrail

- **No** VMA, Tobski, Granite, glslang runtime, SPIRV-Cross, ray tracing o FidelityFX nel core
  ChrononMotion3D.
- **No** secondo allocatore, seconda authority di sincronizzazione o secondo render graph in C3D.
- **No** renderer Vulkan in ChrononMotion3D: il renderer di produzione è C3D e va consumato via
  RenderPlan.
- **No** nuove terze parti in `ChrononMotion3D/render/` senza ADR (allowlist in
  `tests/check_render_scope.cmake`).
- **No** campi wire senza consumatore C3D che cambi i pixel.
- **No** soppressione di errori SyncVal, test skippati o asserzioni indebolite per far passare un
  gate.
- **No** refactor del render graph mentre la CI same-SHA è rossa o sconosciuta.

## Rischi e gestione

| Rischio | Mitigazione |
|---|---|
| SyncVal produce falsi positivi o rumore driver | Layer/versione pinnati, corpus fisso, host registrato; ogni warning classificato, non soppresso |
| Rimozione di una barrier globale scopre un hazard reale | Prova prima/dopo sul corpus; fix puntuale; la barrier resta con Demolition Debt finché la copertura non è dimostrata |
| Hot reload ricrea pipeline con layout incompatibile | Validazione ABI con SPIRV-Reflect; fail-closed; ricreazione layout solo con supporto esplicito |
| Il pair same-SHA è bloccato da runner GitHub | Registrare il blocco come esterno; non dichiarare PASS da run locali dirty |
| `render/` assorbe lavoro di produzione | `check_render_scope.cmake` resta il gate; ogni estensione richiede ADR |

## Evidenza richiesta

- Host, GPU, driver, layer e versione degli strumenti.
- Coppia SHA C3D/CM3D e stato pulito degli alberi.
- Comandi esatti, exit code, log completi e output SyncVal.
- Per W5: diff dello shader, tempi di reload, hash/pixel delta e verifica che la release non
  contenga il percorso runtime.
- Per W7: numeri di picco VRAM e throughput, non stime.

## Riferimenti

- `src/backends/vulkan/vulkan_backend_sync_private.cpp`
- `src/backends/vulkan/vulkan_submission_authority.hpp`
- `src/backends/vulkan/memory/vulkan_memory_manager.cpp`
- `docs/architecture/VULKAN-AUTHORITY-MAP.md`
- `docs/adr/ADR-001-frame-graph-compiler.md`
- `docs/adr/ADR-029-authority-boundary-doctrine.md`
- `ChrononMotion3D/tests/check_motion_boundary.cmake`
- `ChrononMotion3D/tests/check_render_scope.cmake`
- `ChrononMotion3D/docs/BOUNDARY_C3D.md`
- `ChrononMotion3D/docs/tickets/TICKET-CM3D-REMAINING-WORK-20261106.md`
- `ChrononMotion3D/docs/tickets/TICKET-CM3D-PER-LAYER-BLUR-20261106.md`
- `ChrononMotion3D/docs/THREEPP_CERTIFICATION.md`

Le repository esterne restano riferimenti di studio: nessuna è una dipendenza di build o runtime di
Chronon3D o ChrononMotion3D.
