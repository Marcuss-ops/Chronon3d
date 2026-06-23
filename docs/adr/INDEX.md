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

> Slot ADR-003/004/006/007 sono previsti dal piano 07 sezione D2
> ma formalmente *Proposed* finché i deliverable D2.x del piano
> 07 non producono i file. Slot successivi (Expressions V2, V3
> tile-first, nuove API pubbliche) prendono ADR-008 in poi.
>
