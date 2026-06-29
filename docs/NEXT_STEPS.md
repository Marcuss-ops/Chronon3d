# Chronon3D — Next Steps Reali

> **Snapshot funzionale analizzato:** `main@e853e728`, 2026-06-29.
>
> Questo documento descrive l'ordine operativo immediato delle priorità per
> sbloccare la costruzione della baseline. Ogni azione è atomica (un commit
> per chiusura) e viene promossa via atomic-commit + push diretto su `main`
> (vedi `AGENTS.md`: nessun branch di feature intermedi).

## Coda delle priorità

### P0 — Blocker immediati

- **P0.1 — Investigare la regressione FRESH-end-to-end `sdk_archive_merge`**
  *Descrizione*: il build pulito in CI (FRESH `rm -rf /tmp/dod && cmake
  --preset linux-ci --fresh && cmake --build … --target sdk_archive_merge`)
  fallisce con `rc=1` nonostante la build locale (warm cache) produca un
  archivio valido con 289 oggetti. Root-cause non ancora isolata.
  *Sintomo*: il run finale DoD end-to-end sul commit `e853e728` ha
  Step 2 FAIL + Step 5 cascade FAIL + tutti i successivi SKIP.
  *Unblocks*: artefatto `libchronon3d_sdk_impl.a` consumabile da
  `cmake --install` (Step 5) e dall'intero consumer pipeline (Steps 6–8).
  *Azione minima*: leggere `/tmp/dod_c1v2_step2.log` (tail 50) per estrarre
  l'errore reale, identificarne la singola causa radice, applicare la fix
  più piccola (≤1 commit atomico).

### P1 — Follow-up packaging e validazione

- **P1.1 — TICKET-039 follow-up — content manifest-filter**
  *Descrizione*: il filtro `find("${_obj}" "/${_reg_obj}.dir/" ...)` in
  `cmake/Chronon3DSdkArchive.cmake` non matcha
  `_content.dir/` (con underscore-prefix local-name del target dir).
  Il simbolo `chronon3d::register_content_modules` esiste in
  `content/CMakeFiles/_content.dir/register_content_modules.cpp.o` ma
  non viene mergiato in `libchronon3d_sdk_impl.a`. Conseguenza: il
  catalogo canary resta 8/9 PRESENT + 1 BEST-EFFORT documentato al
  commit `81cdc738`.
  *Unblocks*: 9/9 canary PRESENT su Fase 5; rimozione del caveat
  BEST-EFFORT nel comment header di `cmake/Chronon3DCanarySymbols.cmake`.
  *Azione minima*: allargare la regex del filtro per includere sia
  `/<target>.dir/` *che* `/_<target>.dir/` (o normalizzare local-name via
  `get_target_property(... SOURCE_DIR ...)`), validare con single-`nm -C`.

- **P1.2 — Re-run DoD 9-step end-to-end post-fix P0 + P1**
  *Descrizione*: dopo la chiusura di P0.1 e P1.1, rilanciare
  `/tmp/dod_c1_verify.sh` su `main@HEAD` per produrre una matrice 9/9 PASS
  (target).
  *Unblocks*: certificazione machine-verificata del commit post-fix.
  *Azione minima*: aggiornare `docs/baselines/<hash>-dod-9-of-9.md` con
  la nuova matrice (commit id, ambiente, log per-step incluso) + bump
  di `docs/CURRENT_STATUS.md`.

### P2 — Snapshot e documentazione

- **P2.1 — Aggiornare `docs/FOLLOWUP_TICKETS.md` con i nuovi ticket residui**
  *Descrizione*: tracciare formalmente i 2 gap come ticket con severità
  + commit di chiusura pianificati. Permette di non perderli nella
  coda operativa generale.
  *Azione minima*: aggiungere `TICKET-059` (sdk_archive_merge regressione)
  + tracking di `TICKET-039` follow-up (content manifest-filter, già
  pre-tracciato come P1.1).

- **P2.2 — Refresh `docs/CHANGELOG.md`**
  *Descrizione*: aggiungere una sezione per il batch di 4 commit atomici
  SDK chain (`56ab0625` → `81cdc738`) sotto l'intestazione del mese + la
  Fix-5 ROT-2 (`e853e728`) sotto la stessa sezione.
  *Azione minima*: una commit atomica `docs(CHANGELOG): post-doD-Fase-1…`.

- **P2.3 — Refresh `docs/baselines/` con snapshot post-fix**
  *Descrizione*: dopo P0 + P1 chiusi, creare
  `docs/baselines/main-<<post-fix>>-dod-9-of-9.md` come baseline di
  riferimento per le milestone M1–M4. Include: 9-step PASS matrix,
  atomic commit refs, ambiente (preset, OS, compiler).

## Coda post-baseline (residuo M0 → M1)

Items che erano blocker baseline-critical ma sono stati chiusi su `main`
durante il wind-down DoD; rimangono come reference e per retrospettiva
del prossimo ciclo M1 (nessuna azione richiesta, ma documentati per
tracciabilità storica):

- **CODA-PB.1 — ROT-2 in `software_renderer.hpp` chain (alias TICKET-058)**
  ✅ chiuso
  *Descrizione storica*: nuovo incomplete-type ROT su `RenderRuntime`
  siblings del ROT-1 (Fix 2 / commit `81cdc738`). Root cause: sul
  consumer-side (Step 9 ghost_sweep path) il path canonico
  `sw_renderer->runtime().executor()` incontrava forward-declaration-only
  di `RenderRuntime` su `include/chronon3d/backends/software/software_renderer.hpp`,
  producendo `invalid use of incomplete type 'class chronon3d::runtime::RenderRuntime'`.
  *Stato attuale*: ✅ ROT-2 **chiuso** su commit `e853e728` via Fix 5
  (`include/chronon3d/backends/software/software_renderer.hpp` include +
  audit-comment block TICKET-038/TXT-00). NON richiede commit dedicato.
  Tracking: `TICKET-058` in `docs/FOLLOWUP_TICKETS.md` (Recently closed).
  *Lesson learned*: il pattern rot-2 era su un header pubblico
  (forward-decl rimossa) mentre il pattern rot-1 era su TUs `.cpp` private.
  Predicare audit-comment block TICKET-038/TXT-00 a entrambi i casi per
  tracciabilità.

## Vincoli operativi (richiamati da `AGENTS.md`)

- Nessun branch intermedio: lavorare direttamente su `main`.
- Commit atomici, 1 fix per commit, push frequenti.
- Commenti inline esplicativi per ogni cambio non-meccanico (audience:
  code-reviewer + futuro lettore).
- Touch solo file canonici elencati in `AGENTS.md` (root + `src/` +
  `cmake/` + `tests/` + `tools/` + `docs/`) e, per i file Nuovi,
  motivazione in-line esplicita.
- Niente `<src/...>` o `<msdfgen>` o `<libtess2>` o `<unicode[/...]>` da
  nessuna parte (deny-everywhere Gate 5). Per deroghe serve prima un ADR.
- Mai segnare verde una suite che restituisce failure; mai cambiare un
  gate per nascondere un errore.

## Sequenza raccomandata

1. **P0.1**: leggere il log di errore dal FRESH build → diagnostic →
   fix-atomico → push.
2. **P1.1**: content manifest-filter (single regex o normalize) →
   fix-atomico su `cmake/Chronon3DSdkArchive.cmake` → push.
3. **P1.2**: re-run DoD end-to-end. Se matrice 9/9 PASS → baseline snapshot
   (P2.3) + FOLLOWUP_TICKETS update (P2.1) + CHANGELOG bump (P2.2).
4. Se la matrice non è 9/9 → riaprire la coda con un nuovo gap classificato.

L'obiettivo finale è una DoD machine-verificata 9/9 PASS su un singolo commit
di `main`, certificato da un baseline snapshot — vero "fine di M0". A
`e853e728` la coda è ridotta a 2 gap (P0.1 + P1.1) dopo la chiusura del
ROT-2.
