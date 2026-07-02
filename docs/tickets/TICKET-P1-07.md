# TICKET-P1-07 — Sistema asset globalmente mutabile

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | assets / runtime |
| **Stato** | PLANNED |
| **Blocca** | post-baseline |
| **Feature Freeze** | ❌ Bloccato — richiede baseline verde |

## Bug

`RenderRuntime` possiede correttamente un `AssetResolver` locale, ma continua a esportare API globali:

```cpp
set_process_wide_assets_root();
process_wide_assets_root();
process_wide_resolver();
```

L'implementazione usa: stringa globale, mutex globale, resolver statico lazy, first-mount semantics.

`RenderEngine::assets_root()` restituisce il valore globale, non quello posseduto dal proprio runtime.

Con due engine (`Engine A → /assets/project-a`, `Engine B → /assets/project-b`), l'ultimo writer modifica il valore osservato dall'altro engine. Il resolver statico impedisce di applicare root successivi dopo il primo mount.

## Criteri di accettazione

- [ ] Rimuovere progressivamente il fallback globale: `RenderRequest → RenderSession → RenderGraphContext → AssetResolver&`
- [ ] Deep code senza resolver deve fallire con errore di dependency injection, non leggere stato di processo
- [ ] Deprecare `set_process_wide_assets_root()`, `process_wide_assets_root()`, `process_wide_resolver()`
- [ ] `RenderEngine::assets_root()` restituisce il valore posseduto dal proprio runtime
- [ ] Test: due engine con root diversi non interferiscono

## File interessati

- `src/assets/asset_resolver.cpp`
- `src/core/` (API globali)
- `include/chronon3d/` (header pubblici)
- `RenderEngine` implementation
