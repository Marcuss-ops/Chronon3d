# TICKET-PERSISTENT-CACHE-ADR-GAP — Retired executor bridge

## Stato: DONE / ARCHIVED (2026-08-01)

Il ticket è superato dal codice corrente. Il bridge executor-side descritto
qui non esiste più: non sono presenti
`persistent_framebuffer_cache_enabled_for_current_run()`,
`policy.persistent()`, `FrameInvariantPersistent` o
`static_persistent_cache()`.

`CacheMode` espone ora soltanto `Disabled`, `FrameVariant` e
`FrameInvariantMemory`. Questo ticket non richiede quindi un ADR aggiuntivo né
una rimozione ulteriore.

`PersistentFramebufferStore` resta invece codice vivo, posseduto per istanza
da `RenderRuntime` e coperto dai test cache/benchmark. La sua eventuale
estrazione dal runtime V1 è un tema architetturale distinto e non deve essere
trattata come chiusura di questo ticket.
