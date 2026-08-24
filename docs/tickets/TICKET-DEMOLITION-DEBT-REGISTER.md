# TICKET-DEMOLITION-DEBT-REGISTER

## Scopo

Registro operativo per la regola **ogni ingresso abilita un'uscita** in
`AGENTS.md`. Ogni bridge o dipendenza temporanea deve avere owner, motivo,
condizione di uscita, prova di equivalenza e rimozione delimitata.

## Regola di stato

`ACTIVE` significa che la migrazione non è terminata; `READY_TO_REMOVE`
significa che la condizione è stata dimostrata e la demolizione va eseguita in
un commit dedicato; `REMOVED` richiede anche una verifica post-rimozione.

## Registro iniziale

| Item | Owner | Reason | Exit condition / gate | Removal scope | Status |
|---|---|---|---|---|---|
| Vulkan CPU fallback bridge | Vulkan owner | Operazioni non ancora native | Scene di certificazione con tutti i `fallback_* == 0` per 30 nightly | `set_draw_node_fallback`, bridge, counter fallback, upload-back path | ACTIVE |
| Vulkan immediate submit/wait | Render-graph owner | Direct-op callers non ancora tutti su `CommandPlan` | Nessun caller production fuori da `CompiledFrameGraph → CommandPlan → batch` | Immediate submit path, conservative per-pass branch, compat telemetry | ACTIVE |
| Vulkan `legacy_*` counters | Vulkan owner | Osservabilità della migrazione precedente | Fallback/legacy path rimosso e zero riferimenti confermato dal architecture gate | Counter, schema fields, reporters e test snapshot | ACTIVE |
| SVG document boundary | Assets owner | API attuale importa il primo `<path>` soltanto | `SvgDocumentImporter` con corpus documentale, transforms/styles/shapes e parity test | Narrow file API e compat extraction assumptions | ACTIVE |
| Custom expression parser | Animation owner | AE bindings richiedono backend replacement | Backend alternativo con parity del golden corpus e fuzz regression green | `ExpressionParser`, duplicate grammar helpers, parser-specific tests | ACTIVE |
| SVG++ spike | Assets owner | Spike promosso al path importer | Production path tests and importer are canonical | Spike target, option, corpus runner and duplicate wiring | REMOVED |
| Non-core dependency graph | Build owner | Feature dependencies may be promoted to global scope | Dependency audit has a feature for every non-core package and minimal preset verifies it | Global vcpkg entries, unused CMake `find_package` calls | ACTIVE |
| SPIRV-Reflect include workaround | Shader owner | Current port target has non-canonical include layout | Upstream/port target exports the canonical include directory | Root CMake target-property workaround | ACTIVE |

## Template obbligatorio

```text
Item:
Owner:
Reason:
Exit condition:
Equivalence test/gate:
Removal scope:
Status: ACTIVE | READY_TO_REMOVE | REMOVED
```

Questo registro non sostituisce i ticket tecnici: ogni item può essere
promosso a ticket dedicato quando la demolizione entra in lavorazione.
