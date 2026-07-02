# TICKET-P1-09 — RenderRuntime service locator

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | runtime |
| **Stato** | PLANNED |
| **Blocca** | post-baseline |
| **Feature Freeze** | ❌ Bloccato — richiede baseline verde |

## Bug

`RenderRuntime` espone le risorse attraverso percorsi multipli: `RenderServices` (bundle di puntatori), accessor diretti (`node_cache()`, `executor()`, `scheduler()`), accessi via `SoftwareRenderer` e `RenderSession`. Costruzione fragile in 5 step (`construct runtime → renderer → backend → attach → pipeline`). I due costruttori di `RenderEngine::Impl` duplicano la costruzione.

## Criteri di accettazione

- [ ] Introdurre `RenderRuntime::create(RuntimeConfig)` come unico costruttore pubblico (`static Result<RenderRuntime, RuntimeBuildError>`)
- [ ] Unificare i due costruttori di `RenderEngine::Impl`
- [ ] Rimuovere `attach_backend()` pubblico (diventa interno alla factory)
- [ ] Ridurre la superficie di `RenderServices` — esporre solo interfacce tipizzate
- [ ] Test: `RenderRuntime::create()` produce oggetto sempre valido (backend_attached + servizi pronti)
- [ ] Test: due runtime indipendenti non condividono stato

## File interessati

- `include/chronon3d/runtime/` (RenderRuntime, RenderServices)
- `src/runtime/`
- `RenderEngine` implementation
- CLI commands
