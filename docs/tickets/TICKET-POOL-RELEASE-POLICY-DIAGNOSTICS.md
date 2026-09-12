# TICKET-POOL-RELEASE-POLICY-DIAGNOSTICS — FramebufferPool silent no-op releases

## Contesto

`PoolFbDeleter::operator()` (`src/cache/framebuffer_pool.cpp`) è l'autorità di
rilascio per `OwnedFB`. Due policy potevano restituire senza reclamare lo
storage, senza alcun segnale:

```cpp
if constexpr (std::is_same_v<T, RestoreScratchHandle>) {
    return;  // cleanup lambda handles restoration   ← la lambda non era mai invocata
} else if constexpr (std::is_same_v<T, ReturnToScratch>) {
    fb->clear(Color::transparent());
    if (p.slot) *p.slot = fb;   // slot == nullptr → storage perso
}
```

`RestoreScratchHandle` portа un callback `std::function<void()> cleanup` che non
veniva mai chiamato: un handle valido non tornava mai al suo slot. `ReturnToScratch`
con `slot == nullptr` perdeva il framebuffer. In entrambi i casi l'unica evidenza
era un leak fino a `trim_after_job` (finding N-16 dell'audit 2026-09-09, marcato
"NON BONIFICATO").

`RendererOwned` resta invece un no-op **legittimo**: il renderer possiede lo slot
fisico pianificato e lo rilascia altrove.

## Modifiche implementate

**File**: `src/cache/framebuffer_pool.cpp`,
`include/chronon3d/core/profiling/render_counter_macros.hpp`

1. `RestoreScratchHandle` invoca ora `cleanup()` quando presente; se manca,
   conta l'anomalia e libera il framebuffer (unico fallback non-leaking).
2. `ReturnToScratch` con `slot == nullptr` conta l'anomalia e libera.
3. Nuovo contatore `unexpected_pool_release_policy_count` (dominio
   `CHRONON_COUNTERS_FRAMEBUFFER`): rende osservabile una policy di rilascio
   errata invece di lasciarla silenziosa. Il contatore è allocation-free e
   `noexcept` perché il deleter gira anche durante lo stack unwinding.
4. `RendererOwned` continua a non contare: non è un errore.

## Accettazione

| Gate | Eval |
|---|---|
| `unexpected_pool_release_policy_count == 0` su un job completo | steady-state (nessun produttore usa `RestoreScratchHandle`/`ReturnToScratch` oggi) |
| Nessun leak da policy no-op | comportamento: cleanup invocato / framebuffer liberato |

## Demolition Debt

Non applicabile: gli stati no-op non sono un percorso supportato da demolire, ma
un invariante reso osservabile. Nessun `RestoreScratchHandle` / `ReturnToScratch`
producer esiste oggi; il contatore protegge il primo che verrà introdotto.
