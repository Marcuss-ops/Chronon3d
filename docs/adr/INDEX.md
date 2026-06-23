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
| ADR-012/013/014/015/016/017 | _reserved slots_ (chronological; see `docs/adr/README.md`) | — | _(placeholder)_ |
| ADR-018 | Fix CMake per-target `link.txt` staleness via `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS)` on `tests/CMakeLists.txt` over the 23 per-area `.cmake` includes (TXT-00 follow-up F-B; see `docs/baselines/codex-txt-00-attempt1-blocked-375bd5b9.md`) | Accepted (F-B diagnostic merged on branch `codex/link-rot-diagnostic`) | [link-rot-text-visual](ADR-018-link-rot-text-visual.md) |
| ADR-019 | Canonical CMake link pattern for in-tree test executables: OBJECT `.o` propagation via dual `chronon3d_sdk` + `chronon3d_sdk_impl` linkage, with `-Wl,--whole-archive` escalation for static-init symbol retention (TXT-00 F-C follow-up; see `docs/baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md`) | Accepted (2026-06-23, empirical F-C history + renderer_tests reference) | [test-bin-object-propagation](ADR-019-test-bin-object-propagation.md) |

> Slot ADR-003/004/006/007 sono previsti dal piano 07 sezione D2
> ma formalmente *Proposed* finché i deliverable D2.x del piano
> 07 non producono i file. Slot successivi (Expressions V2, V3
> tile-first, nuove API pubbliche) prendono ADR-008 in poi.
>
