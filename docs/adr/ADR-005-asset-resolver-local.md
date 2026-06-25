# ADR-005 — `AssetResolver` engine-local, niente bridge globali

- **Status:** Accepted (work-in-progress verso *Validato in CI* in WP-8 PR 8.1/8.2 e R6 piano 06)
- **Date:** 2026-06-21
- **Deciders:** Asset / runtime team
- **Tags:** architecture, asset-resolver, ownership, no-globals

## Context

Il codice raggiungeva `AssetResolver` da process-wide globals:

* `g_active_runtime`, `set_active_runtime()`, `active_runtime()` —
  il "runtime attivo" era un singleton che permetteva a qualunque
  codice profondo (font, text, preflight, precomp) di raggiungere
  *un* runtime anche in presenza di molteplici.
* `g_process_wiave_assets_root` + `process_wide_assets_root()` —
  radice asset impostata dal costruttore di runtime e letta da chi
  non riceveva un resolver esplicito.
* `typed_resolver_for_deep_code()` —
  *resolver-tipizzato-per-codice-profondo*, una variante che
  selezionava un resolver sulla base dell'ultimo runtime montato.
* `g_deep_resolver_mirror` — replica speculare che teneva il
  thread locale allineato al runtime attivo.

Conseguenze: due runtime distinti (es. due `Chronon3D::SDK` linkati
nello stesso processo) si contaminavano l'un l'altro; testing
isolato impossibile; ordine di costruzione dei runtime determinava
i risultati.

## Decision

1. `AssetResolver` è **engine-local**, posseduto dal `RenderRuntime`
   (cfr. ADR-002). Il runtime ne espone un puntatore via
   `runtime().resolver()`.
2. `AssetResolver*` è passato *esplicitamente* a tutte le
   superfici che ne hanno bisogno: font, text rasterizer, image
   loader, preflight, precomp construction, helper di content, CLI
   commands, test fixture.
3. I bridge globali `g_active_runtime`, `set_active_runtime()`,
   `active_runtime()`, `g_process_wide_assets_root`,
   `set_process_wide_assets_root()`, `process_wide_assets_root()`,
   `default_assets_root_for_deep_code()`,
   `typed_resolver_for_deep_code()`, `g_deep_resolver_mirror`,
   hook di reset test-only — sono **rimossi**.
4. `RenderRuntime::populate()` non pubblica sé stesso globalmente.
   `RenderRuntime::set_default_assets_root()` non scrive stato
   processo-wide. Distruzione del runtime è libera da pulizia
   pointer globali.
5. L'helper a due argomenti `resolver(explicit_root)` resta solo
   dove il chiamante possiede intenzionalmente la radice.

## Consequences

* **Positive.** Due runtime simultanei non si contaminano; nessuna
  dipendenza dall'ordine di costruzione; nessuna race su
  `current_identity` da mirror globali; setup CLI riproducibile;
  test multi-engine isolation (R6 del piano 06) verdi.
* **Negative.** Tutti i consumer esistenti devono ricevere un
  resolver via parametro o `ctx.services.asset_resolver` — costo
  di sostituzione noto dal libro WP-8 PR 8.0/8.1.
* **Neutral.** L'API pubblica non cambia (il consumer SDK non vede
  `AssetResolver` direttamente).

## Alternatives considered

* **Thread-local del runtime attivo.** Scartata: stessa famiglia di
  problemi (ordine di distruzione, contaminazione) con un nuovo
  singleton mascherato.
* **Lazy static resolver con doppio refcount.** Scartata: raddoppia
  la superficie di ownership senza risolvere la contaminazione.
* **Lasciare bridge come fallback deprecato.** Scartata: viola
  esplicitamente ADR-002 e rende `RenderRuntime` non più
  univocamente la fonte del resolver.

## References

* WP-8 PR 8.0 (passaggio resolver esplicito).
* WP-8 PR 8.1 (`RenderRuntime::resolver()`).
* WP-8 PR 8.5 (gate architetturale: arch-boundary script rifiuta
  reintroduzioni).
* [`../ARCHIVE/stabilization-plan/06-renderer-plan.md`](../ARCHIVE/stabilization-plan/06-renderer-plan.md) §R6.
* [`../ARCHIVE/refactor-roadmap/08-global-state-and-sdk.md`](../ARCHIVE/refactor-roadmap/08-global-state-and-sdk.md).
