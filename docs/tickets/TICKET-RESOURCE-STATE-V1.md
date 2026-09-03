# TICKET-RESOURCE-STATE-V1 — explicit asset references + resource-sync primitives + parallel ResourceStateTracker

> Stato: **DONE (commit su main)** — fasi 1–3 della roadmap resource-architecture
> (asset fix → primitive → tracker in parallelo). Fase 4+ (Vulkan Sync2 adapter,
> switch definitivo, P0.2 CompiledResourceTable, RenderToMedia, RenderReceipt)
> restano forward-point separati.
> Scope: rimuovere i due path asset hardcoded dal core + introdurre le primitive
> di sincronizzazione backend-neutral additive mantenendo temporaneamente il
> vecchio `BarrierTransition` in esecuzione in parallelo nei test.

---

## Problema

1. **Asset hardcoded nel core.** Due dipendenze implicite dal checkout vivevano
   nel codice engine/library:
   - `assets/images/camera_reference.jpg` — default di
     `CameraMotionParams::reference_image`
     (`include/chronon3d/animations/camera_motion_params.hpp`).
   - `assets/fonts/Poppins-Bold.ttf` — default di `SubtitleTrackBuilder::font_path_`
     (`include/chronon3d/authoring/subtitle_track_builder.hpp`).

   Entrambi i file sono stati rimossi dal repository; il core non deve sapere
   dove gli asset vivono fisicamente (ADR-016 lineage + TICKET-ASSET-READINESS).

2. **Barrier planning troppo semplice per diventare authority di sync.** Il
   `GpuCommandPlanner` produce `BarrierTransition {pass, surface, before, after,
   hazard}` per il solo dominio color-surface/compute. Mancano: range per
   subresource (mip/layer/plane NV12-P010), buffer byte-range, usage intent
   pass-level, resolver unico intent→state, ownership transfer di queue e
   alias boundary. La roadmap prevede che il compiler diventi l'autorità unica
   (P0.1) e Vulkan consumi solo i risultati (fase 4-5).

## Soluzione — fase 1 (P0.0 asset fix, nessun altro refactor insieme)

| File | Δ |
|---|---|
| `include/chronon3d/animations/camera_motion_params.hpp` | `std::string reference_image{"assets/images/camera_reference.jpg"}` → `std::optional<assets::ImageRef> reference_image{}` (nessun default implicito; preset espliciti via `assets::ImageRef`). |
| `include/chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp` | fingerprint di `CameraMotionParamsSource`: hash presence + `path()/owner()/required()` dell'optional `ImageRef`. |
| `src/scene/camera_tilt_clips.cpp` | `build_reference_image_content`: se `reference_image` assente → solo dark-grid (nessun layer immagine implicito); se presente → `ImageParams{.source = ref, ...}` (canonical `ImageRef`). |
| `include/chronon3d/authoring/subtitle_track_builder.hpp` | `font_path_{"assets/fonts/Poppins-Bold.ttf"}` → `std::optional<assets::FontRef> font_`; nuovo overload `font(assets::FontRef, float)`; l'overload string delega al canonical `FontRef`. |
| `src/scene/subtitle_track_builder.cpp` | `spec.style.font.font_path = font_path_` → `if (font_) spec.style.font.font_path = font_->path();` (run senza font dichiarato → path vuoto, risoluzione delegata al font engine canonico). |
| `tests/support/layer_builder_inspection.hpp` | include stale `registry/animator_resolver.hpp` → `text/animation/text_animator_spec.hpp` (fix minimale richiesto dal nuovo test; file già marcato `drift-allow: stale-ref`). |
| `tests/text/test_subtitle_font_ref.cpp` (NEW) | 3 test case: default senza font implicito (font_path vuoto), `font(FontRef,size)` → path+size esatti, overload string compatibile. |
| `tests/text/CMakeLists.txt` | suite `chronon3d_subtitle_font_ref_tests` (UNIT, link `chronon3d_pipeline`). |
| `tests/renderer/camera/test_camera_motion.cpp` | test case: `CameraMotionParams` default → `reference_image` assente; set `ImageRef` → roundtrip path/owner/required. |

## Soluzione — fasi 2–3 (primitive + tracker parallelo)

| File | Δ |
|---|---|
| `include/chronon3d/runtime/resource_state.hpp` | Estensioni ADDITIVE (nessun bit/value esistente spostato): `ResourceAspect::Plane0/1/2`; `PipelineStage` VertexShader/FragmentShader/ColorOutput/VideoDecode/VideoEncode/AllGraphics; `AccessMask` ColorRead/Write + VideoDecode/Encode Read/Write (inclusi in `reads()/writes()`); `ResourceLayout` ColorAttachment/Present/External/VideoDecodeSrc/Dst/VideoEncodeSrc/Dst; `QueueClass` Compute/Decode/Encode; `SubresourceRange::overlaps()`. |
| `include/chronon3d/runtime/resource_transition.hpp` (NEW) | `BufferRange`, `WholeResource`, `ResourceRange = variant<WholeResource, SubresourceRange, BufferRange>` (+ `ranges_overlap`), `UsageIntent` (14 valori), `ResourceUse` (resource+intent+range+discard), `ResourceStateResolver` (unica authority intent→state), `ResourceTransition` (resource, range, before, after, producer/consumer pass, queue_ownership_transfer, alias_boundary), `TransitionResult`, `ResourceStateTracker` (hazard RAW/WAR/WAW, read→read accumulate, layout change, queue ownership, discard, alias boundary, non-overlap elision). |
| `tests/runtime/test_resource_state_tracker.cpp` (NEW) | 19 test case / 122 assertion: overlap semantics, resolver mapping, hazard rules, **parallel comparison vs legacy `GpuCommandPlanner`** (stesso scenario → stesso set (pass, resource, before, after)). |
| `tests/runtime/resource_state_tracker_tests.cmake` (NEW) + `tests/manifests/test_definitions.cmake` | registrazione suite UNIT. |

`BarrierTransition` / `BarrierPlan` / `GpuCommandPlanner` restano INVARIATI:
il vecchio sistema continua a produrre il suo plan; il nuovo tracker è
additivo e i test confrontano le due rappresentazioni.

## Demolition Debt sheet (AGENTS.md §Demolition Debt — obbligatoria)

1. **Owner** — mantenitore del bridge parallelo: chi estende `GpuCommandPlanner`
   o il backend Vulkan.
2. **Reason** — migrazione in corso: il compiler deve diventare l'autorità unica
   di sincronizzazione (P0.1) e Vulkan deve consumare solo `ResourceTransition`
   via Sync2. Fino all'adapter Sync2 (fase 4-5) il `BarrierPlan` legacy resta il
   path produttivo.
3. **Exit condition** — condizione osservabile: Vulkan (e tutti gli altri
   backend) traducono `ResourceTransition[]` del `ResourceStateTracker` invece
   di `BarrierTransition[]`; i test di parità (parallel comparison in
   `test_resource_state_tracker.cpp`) passano sul nuovo path con il vecchio
   disabilitato; zero `VkImageMemoryBarrier`/`vkCmdPipelineBarrier` nel normal
   path.
4. **Equivalence test/gate** — `tests/runtime/test_resource_state_tracker.cpp`
   (parity del set (pass, resource, before, after) tra i due sistemi) +
   regression legacy `tests/runtime/test_gpu_command_plan.cpp`.
5. **Removal scope** — da cancellare a migrazione completata: `BarrierTransition`,
   `BarrierPlan`, `build_barrier_plan` in `gpu_command_plan.hpp`, i fallback
   blanket `ALL_COMMANDS`/`MEMORY_READ_WRITE` nel backend Vulkan, lo state map
   `current_image_layout_` se duplica il tracker.
6. **Status** — `ACTIVE`.

## Verifica

- Syntax-only (`g++ -std=c++20 -fsyntax-only`) su tutti gli header/source/test
  toccati: **PASS**.
- Esecuzione standalone (header-only, nessuna lib di progetto): 
  `test_resource_state_tracker.cpp` **19/19 PASS (122 assertion)** incl. la
  parallel comparison vs legacy; regression `test_gpu_command_plan.cpp`
  **6/6 PASS (38 assertion)** — le estensioni additive di `resource_state.hpp`
  non alterano il comportamento legacy.
- **Blocker pre-esistente (non introdotto da questo ticket)**: la configure
  standard del repository è rotta su `main` (`src/CMakeLists.txt:4`
  `find_package(nlohmann_json_schema_validator CONFIG REQUIRED)` ma il port
  non esiste nella vcpkg baseline pinnata `cb2981c4`; `cmake/Chronon3dVcpkgToolchain.cmake`
  è stato rimosso dalla tree). Stato coerente con `docs/CURRENT_STATUS.md`
  "CI infrastructure FAIL". Il full build CTest del progetto NON è eseguibile
  finché questo non viene risolto (forward-point `TICKET-VCPKG-SCHEMA-VALIDATOR-MISSING`).

## Forward-points

| Ticket | Oggetto |
|---|---|
| fase 4 | Adapter Vulkan Sync2 (`VkImageMemoryBarrier2`, `VkBufferMemoryBarrier2`, `VkDependencyInfo`, `vkCmdPipelineBarrier2`) che traduce `ResourceTransition`; nessun uso produttivo del tracker prima dell'adapter. |
| fase 5 | Switch definitivo Vulkan a Sync2 + rimozione `BarrierTransition`/`VkImageMemoryBarrier`/`vkCmdPipelineBarrier`/fallback blanket (Demolition Debt exit). |
| fase 6-7 (P0.2) | `ResourceDesc → PhysicalRequirements → CompiledResourceTable`; `SurfaceDesc` solo authoring sugar. |
| fase 8 | RenderToMedia su NV12/P010 come vere graph resources (Plane0/Plane1 subresources). |
| TICKET-VCPKG-SCHEMA-VALIDATOR-MISSING (NEW, pre-esistente nel codice) | ripristinare la configure standard: aggiungere il port (o bump baseline) + ripristinare il toolchain wrapper; sblocca full build + CTest di tutte le suite. |