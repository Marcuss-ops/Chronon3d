# TICKET-AUDIT-2026-09-09-REMEDIATION — Log di bonifica post-audit

> **Stato:** CLOSED (G-01..G-11 landed o defer motivato).
> **Origine:** cronaca migrata da `FIXING.md` (rimosso 2026-09-09, findings soppiantati da `docs/AUDIT-2026-09-09.md` + `docs/GOVERNANCE.md`).
> **Audit di riferimento:** `docs/AUDIT-2026-09-09.md`.

## Stati rettificati dall'audit

| Item | Stato precedente | Stato verificato 2026-09-09 | Nota |
|---|---|---|---|
| D-03 | P2 "mai letta" | **FALSO POSITIVO** | `ResourcePlanTelemetry` è consumata da `apps/chronon3d_cli/utils/job/render_job_finalize.cpp:112-122` (exporter telemetry) e `peak_live_bytes` alimenta `CompiledResourceTable` (`frame_graph_builder_allocation_detail.hpp:132`). Nessuna bonifica necessaria. |
| D-05 | `lowered_into_batch` dead | **FALSO POSITIVO** | È il flag del fusion pass con validazione di coerenza in `frame_graph_builder_program_detail.hpp:254-256`. |
| H-01, H-02, H-03 | P0 | **LAND** (f6eb170a6 e precedenti) | radial_blur su scratch + LUT taps; `glyph_key_string`/sha256 eliminati; telemetry dispatch su stack array. |
| C-03, C-04, C-05, E-01, E-02, E-04 (parz.), E-07, S-04 | P0/P1 | **LAND** | Verificati nel codice 2026-09-09. |
| S-01 (parziale) | P0 | **LAND (esteso oggi, G-04)** | shared_lock fast-path + upload fuori dal lock (vedi G-04). |
| C-01 | P1 | **LAND (G-12)** | `ResourcePreparation::load_font` valida la face via `FontEngine::can_load`; rigetta font invalide/corrotte al barrier (`CorruptedAsset`). |
| C-02 | P1 | **LAND (G-10)** | `AssetKind::Layout` aggiunto ad `AssetKind` + `add_layout`; Phase 5 migrata a `manifest.filter(AssetKind::Layout)`. Test di tassonomia e autoritá del manifest. |
| S-05 | P1 | **DEFER** (decisione prodotto) | Compat path; demolizione già tracciata (`patches/README.md:59`). |
| S-02 (struttura lock) | P0 | **PARZIALE (G-03)** | Best-fit scan sotto lock eliminato via indice ordinato; sharding del mutex resta dietro gate `m_lock_wait_us` (piano Es.1 dell'audit). |
| S-03 | P1 | **PARZIALE** | Hot-path rework esistente (1 lock, zero string) + Es.2 dell'audit (TLS batch) resta il passo successivo. |

## Task bonificati (G-01..G-12)

| Task | Finding | Modifica | Test |
|---|---|---|---|
| G-01 | N-03/N-05 | `src/runtime/resource_preparation.cpp`: predicato unico `should_fail_loud()` (MissingAsset ∪ UnresolvableAssetPath ∪ **CorruptedAsset** su ref required); le 5 copie della policy (3 helper + 2 inline) passano tutte dal predicato. La fase mesh non dichiara più successo con mesh required corrotta sotto WarnAndSkip. | `test_resource_preparation.cpp` (WarnAndSkip optional→warning, required→fail-loud) + `test_resource_preparation_mesh.cpp` (required corrupted→CorruptedAsset fail-loud; optional corrupted→warning) |
| G-02 | N-04 | `src/render_graph/pipeline/helpers.hpp` + `composition.cpp`: SSAA `downsample_fb_pooled()` prende il buffer da `sw_sidecar->framebuffer_pool()` (zero heap churn per frame); fallback allocante solo per caller senza pool. | Gate `framebuffer_allocations==0` con SSAA attivo (bench) |
| G-03 | N-01/S-02 | `src/cache/framebuffer_pool.cpp`: completata la migrazione OPT-POOL-BESTFIT — best-fit via indice ordinato `(area, dims)` al posto dello scan lineare O(buckets) sotto lock; index mantenuto in acquire/release/clear (prima era half-wired e l'invariante era violato). | Counter `framebuffer_pool_best_fit_reuse` / `exact_hit` invariati; `m_lock_hold_wall_us` ↓ |
| G-04 | N-02/S-01 | `src/runtime/gpu_glyph_atlas_core_detail.hpp`: upload driver per-glyph FUORI dal `unique_lock` (reserve-then-upload: regione riservata sotto lock, upload sbloccato, publish con re-check). Miss burst non serializza più su `upload_surface_region`. | Subtitle burst: hit-rate e `failures` invariati; contesa ↓ |
| G-05 | E-03 | `gpu_asset_cache.*`: contatore dedicato `unsupported_capability_releases` (atomico, fold in `stats()`) — il rilascio logico-only su backend CPU è osservabile e non si mescola più a `release_failures`. | `stats().unsupported_capability_releases > 0` su CPU backend |
| G-06 | U-01 | `include/chronon3d/runtime/gpu_glyph_atlas.hpp`: `glyph_key_hash64` unificato su `core::hash::HashBuilder` (ultimo hash-combine manuale dell'header eliminato; digest cambia una volta, nessun artifact persistente keyato su questo hash). | Test golden digest in-memory (nessuno persistito) |
| G-07 | grid kernel | `src/backends/software/kernels/grid_background_kernel.cpp`: `cols`/`active_x` → `thread_local` riusati (zero heap churn per tile; TBB workers non condividono il TLS). | Gate alloc per-frame (bench grid) |
| G-08 | N-17 | `software_session_resources.hpp` + radial/directional blur, stroke, offset: `warn_once_effect_scratch_missing()` — il fallback `scratch==nullptr` (pool-less) ora avvisa una volta; `apply_offset` riceve anche lo scratch (era l'unico effect senza wiring, allocava w×h per frame). | Log WARN assente sui path renderer-driven |
| G-09 | D-02 | `job_session.hpp`: `JobSession::arena()` deprecata con `[[deprecated]]` + commento contrattuale (authority = `FrameExecution::arena()`; alias slot-0 = use-after-recycle per caller in prod). Caller esistenti (test/diagnostica) invariati. | Build verde (no `-Werror`); nessun nuovo caller |
| G-10 | C-02 | `include/chronon3d/assets/asset_manifest.hpp` + `src/runtime/resource_preparation.cpp`: `AssetKind::Layout` introdotto nel manifest; Phase 5 migrata da `filter(Image)` a `manifest.filter(AssetKind::Layout)`. Invariante: immagini/video non innescano più layout preparation accidentale. | `test_resource_preparation.cpp` suite manifest authority (text-only, image-only, video+subtitles, image+text, no-text, layout explicit) |
| G-11 | docs | `docs/GOVERNANCE.md` H-5/H-6/H-7 aggiornati; `docs/AUDIT-2026-09-09.md` stato finale. | — |
| G-12 | C-01 | `src/runtime/resource_preparation.cpp`: `load_font` verifica validità FreeType face via `FontEngine::can_load` al barrier al posto del solo path resolver check. | `test_resource_preparation.cpp` corrupt font rejection |

## Test aggiornati/aggiunti (G-01)

- `tests/runtime/test_resource_preparation.cpp`: il case WarnAndSkip ora usa un ref **opzionale** (intento originale del test; il required-missing è coperto dal nuovo case fail-loud E-04).
