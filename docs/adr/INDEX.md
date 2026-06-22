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

> Slot ADR-003/004/006/007 sono previsti dal piano 07 sezione D2
> ma formalmente *Proposed* finché i deliverable D2.x del piano
> 07 non producono i file. Slot successivi (Expressions V2, V3
> tile-first, nuove API pubbliche) prendono ADR-008 in poi.
>
> **ADR-008 deviazione dalla richiesta originaria.**  La richiesta
> iniziale era di nominare il documento `ADR-006`; quello slot è
> riservato a *Registrazione via ExtensionContext* per la politica
> monotona di numerazione (`docs/adr/README.md`) e quindi non
> riutilizzabile.  ADR-008 è il primo slot libero post-007.
