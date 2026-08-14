# ADR Index

> **NOTE — ADR-006 deviation / chronological slot policy.**  Lo slot ADR-006 è riservato
> a *Registrazione via `ExtensionContext`* (Accepted, ANTI_DUPLICATION_RULES
> #1..3) e NON è riutilizzabile: la numerazione è monotona
> (`docs/adr/README.md`) e ogni slot ha un solo argomento.  Le
> richieste future di ADRs per `engine.renderer()` ref→ptr API break,
> nuove API pubbliche o work package successivi devono prendedere
> ADR-008 o successivi.  ADR-008 documente la ref→ptr break
> introdotta dalla 06 R3b boundary refactor.

# ADR Index

| # | Titolo | Status | File |
|---|---|---|---|
| ADR-001 | Frame Graph Compiler unica superficie di compilazione | Accepted (WIP WP-1) | [frame-graph-compiler](ADR-001-frame-graph-compiler.md) |
| ADR-002 | `RenderRuntime` possiede i servizi engine-lifetime | Accepted (WIP R3 del piano 06) | [render-runtime-ownership](ADR-002-render-runtime-ownership.md) |
| ADR-003 | Confine SDK pubblico = `Chronon3D::SDK` (PIMPL) | Proposed | _(placeholder)_ |
| ADR-004 | `ExecutionScope` per root / tile / precomp | Proposed (WP-6 attivo) | _(placeholder)_ |
| ADR-005 | `AssetResolver` engine-local, niente bridge globali | Accepted (WIP PR 8.1/8.2) | [asset-resolver-local](ADR-005-asset-resolver-local.md) |
| ADR-006 | Registrazione via `ExtensionContext` | Accepted | _(placeholder, vedi ANTI_DUPLICATION_RULES #1..3)_ |
| ADR-007 | `SoftwareRenderSession`: stato per-renderer unificato | Accepted (WIP WP-3 phase 5) | _(placeholder)_ |
| ADR-008 | `RenderEngine::renderer()` returns pointer (ref→ptr API break) | Accepted (06 R3b boundary refactor) | [render-engine-renderer-ptr-return](ADR-008-render-engine-renderer-ptr-return.md) |
| ADR-009 | Opt-in third-party text deps (`icu`, `libtess2`, `msdfgen`) for Phases 9 / 11 / 12 | Accepted (WIP — features added to `vcpkg.json`, Gate-5 scoped exem. live) | [optional-text-deps](ADR-009-optional-text-deps.md) |
| ADR-010 | Boundary gate semantic extension (F3.1 extension of `tools/check_architecture_boundaries.sh` with 3 regression-resistant semantic gates: `m_runtime`/`m_registry=nullptr` canonical copy pattern, `m_renderer->settings()` zero-match post-F0.2, `RenderPipeline::m_renderer` scope guard) | Accepted (F3.1 commit, validated by selftest cases 8/9/10) | [boundary-gate-semantic-extension](ADR-010-boundary-gate-semantic-extension.md) |
| ADR-011 | Delete legacy camera surface (AnimatedCamera2_5D, both CameraRig namespaces, CameraShotProfile, camera_descriptor_adapters); keep `camera_v1::CameraDescriptor` canonical; remove `projection_mode` from `Camera2_5D` | 📋 Documented (F2.3 design recorded; deletion pending dedicated F2.3.X workstream) | [camera-legacy-deletion](ADR-011-camera-legacy-deletion.md) |
| ADR-012 | Chronon3D V0.1 SDK Manifest Boundary (deferred action acknowledged; `composition.hpp` transitive leak documented) | Accepted (deferred action) | [chronon3d-sdk-manifest-boundary](ADR-012-chronon3d-sdk-manifest-boundary.md) |
| ADR-013 | camera_v1 semantic contracts: compiler late-rebuild, KeepLastValidCamera policy, cache-lease commit-on-success, explicit FrameRate propagation, DampedFollow-forces-RequiresHistory, LookAtLayer-missing-transforms canonical diagnostic channel (+ ADR-013-EXT: diagnostic channel canonical emit + compiler byte-determinism + post-compile immutability) | ✅ Documented + accepted (post-A3 cluster) | [camera-v1-semantics](ADR-013-camera-v1-semantics.md) |
| ADR-014 | Text golden coverage extension: 12 user-spec tests + 5 forward-only tickets, AGENTS-compliant (no Python, no public C++) | ✅ Documented + accepted (companion to 12 user-spec skeleton tests + 5 PLANNED forward-only rows) | [text-golden-coverage](ADR-014-text-golden-coverage.md) |
| ADR-015 | `ProjectedLayer2_5D::transform` screen-space TRS invariant: position=centroid, scale=(bbox_w,bbox_h,1), rotation=identity, anchor=origin | ✅ Documented + accepted (companion to corpus matrix-fix commit `c03ce2a2`; post-fix rendering path uses `proj.transform.to_mat4()` without defensive normalization, locks AE_CAM_03/05/06/09 frame0≠frame60; residual AE_CAM_02+04 collision carried as `TICKET-AE-CAM-MULTI-NODE-SWEEP`) | [screen-space-trs-invariant](ADR-015-screen-space-trs-invariant.md) |
| ADR-016 | Sequence + Asset canonical contract: TimelineResolver decide cosa esiste al frame, AssetPreflightResolver decide se gli asset sono pronti; RenderGraphBuilder riceve scena già risolta; Renderer non inventa timeline né asset. 9 simboli canonici in `chronon3d::timeline::v2` (4) + `chronon3d::assets::v2` (5) namespace. Step 1/4 landed entrambi i workstream (commit `33798b0a` Asset + `0f47d591`/`fab2046e` Sequence); Step 2/3/4 forward-point. Grep-audit backlog pre-Elimination: 254 Sequence + 85 Asset = 339 hits total, target post-Step-4 = 0. AGENTS v0.1 §Anti-duplication Rules compliance: ZERO nuovi singleton/registry/cache/service-locator. | 📋 Documented (Step 1/4 done entrambi; promotion to `DONE` richiederà Step 4 macchina-verifica: post `main@7eb5c2ba` baseline-verde + 11/11 PASS + grep-audit backlog = 0 su tutti i 10 legacy items combinati) | [sequence-asset-canonical-contract](ADR-016-sequence-asset-canonical-contract.md) |

> Slot ADR-003/004/006/007 sono previsti dal piano 07 sezione D2
> ma formalmente *Proposed* finché i deliverable D2.x del piano
> 07 non producono i file. Slot successivi (Expressions V2, V3
> tile-first, nuove API pubbliche) prendono ADR-008 in poi.
>
| ADR-017 | commit_layer(): manifest completeness for inactive layers | Accepted (main@0ff8b100) | [commit-layer-manifest-preservation](ADR-017-commit-layer-manifest-preservation.md) |
| ADR-018 | Auto-fit text: shrink-only binary search on compile_text_layout() with 12-iteration bound; min/max font size clamps; cache-key integration with resolved font size | ✅ Accepted | [auto-fit-text](ADR-018-auto-fit-text.md) |
| ADR-019 | Text Coordinate Model: 4-level Canvas/Layer/Box/Glyph; bbox naming convention (local/world/predicted/alpha); predicted_bbox containment invariant; TextPlacement resolves Box level; fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX | ✅ Accepted | [text-coordinate-model](ADR-019-text-coordinate-model.md) |
| ADR-027 | CompositionRegistry::add() deprecation discipline + ABI-stability plan: Phase 2 re-add [[deprecated]] marker on legacy add(name, factory) overload per TICKET-COMPOSITIONDESCRIPTOR-MIGRATION Phase 2; Phase 3 REMOVE post V0.1 (Cat-2 freeze source-removal gate, this ADR is the formal gate); 4 forward-point tickets bundled per AGENTS.md section ### 2x-in-one-chore deprecation-reversal rule (PHASE-2-DONE + PHASE-2.5-BUILD-FLAG + PHASE-3-REMOVAL + PHASE-4-AUDIT). | ✅ Accepted (2026-07-14) | [compositiondescriptor-migration](ADR-027-compositiondescriptor-migration.md) |
