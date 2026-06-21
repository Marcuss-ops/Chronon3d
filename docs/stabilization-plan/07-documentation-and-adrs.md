# 07 — Documentazione e ADR

> Piano per allineare `STATUS.md`, `ROADMAP.md`, `NEXT_STEPS.md`,
> ADR, migrazioni e changelog, e per distinguere in modo non
> ambiguo *cosa è stato fatto* da *cosa è stato verificato*.

Stato: [`../STATUS.md`](../STATUS.md) · Piano operativo:
[`../NEXT_STEPS.md`](../NEXT_STEPS.md) · Vincoli:
[`../ANTI_DUPLICATION_RULES.md`](../ANTI_DUPLICATION_RULES.md) ·
Migrazioni esistenti: [`../migrations/`](../migrations/).

---

## 1. Problema

Il repository soffre di tre problemi paralleli:

1. **Documentazione divergente.** `STATUS.md`, `ROADMAP.md`,
   `NEXT_STEPS.md` e i file `refactor-roadmap/*.md` descrivono lo
   stesso insieme di work package con granularità diverse; ogni merge
   tocca un sottoinsieme di questi file, ma non esiste una regola
   "tutti i file che descrivono la stessa area vanno aggiornati nello
   stesso merge".
2. **Decisioni architetturali sepolte nei sorgenti.** I commit
   recenti (`9f9af90e` rimozione `ExecutionPlanCache`,
   `f709d668` fix linkage, WP-8 PR 8.0/8.1 typed resolver,
   TICKET-011 FontEngine hoisting) sono descritti *nel codice* e in
   modo episodico nei messaggi di commit, ma non esiste un registro
   canonico di ADR navigabile.
3. **Stati verificati e non verificati mescolati.** Le celle delle
   tabelle `STATUS`/`ROADMAP`/`refactor-roadmap/README`
   confondono "implementato" con "testato" con "verde in CI". Il
   lettore non sa cosa fidarsi.

Conseguenza: un newcomer non sa dove leggere lo stato corrente, e un
reviewer non sa cosa chiedere come prova di chiusura di un ticket.

## 2. Stato reale (audit 2026-06-21)

| Aspetto | Stato | Evidenza |
|---|---|---|
| `STATUS.md`/`ROADMAP.md`/`NEXT_STEPS.md` esistono e referenziano ticket | ✅ | [`docs/STATUS.md`](../STATUS.md) |
| Aggiornamento *coerente* nelle merge recenti | 🟡 | `refactor-roadmap/README.md` lo impone ma non è checkato |
| `docs/migrations/` con formato "Summary / Shape / PR coverage / Risks" | ✅ | [`docs/migrations/2026-06-renderer-state.md`](../migrations/2026-06-renderer-state.md), [`2026-06-authoring-scene-composition.md`](../migrations/2026-06-authoring-scene-composition.md) |
| Directory ADR canonica (`docs/adr/`) | 🔴 | assente (verificato `ls docs/adr*`) |
| `CHANGELOG.md` aggrega chiusure verdi | 🟡 esiste ma spesso non aggiornato in merge | `docs/CHANGELOG.md` |
| `FOLLOWUP_TICKETS.md` con ticket numerati | ✅ | `docs/FOLLOWUP_TICKETS.md` |
| Distinguere implementato / compilato / testato / validato CI | 🔴 | tabelle attuali usano emoji senza semantica |

## 3. Non-goal

- Non creare un sito statico o un generatore di docs. I file
  Markdown restano la sorgente; GitHub li renderizza nativamente.
- Non duplicare `STATUS.md`. Tutti i file *derivati* referenziano
  `STATUS.md` invece di riscriverne il contenuto.
- Non spostare la cronologia fuori dal repository: `CHANGELOG.md` e
  `migrations/` restano versionati.
- Non sospendere i lavori architetturali: questo piano li documenta
  mentre procedono.

## 4. Obiettivo

Alla chiusura del piano:

1. Esiste `docs/adr/` con ADR numerati, datati, con status
   esplicito, che coprono le decisioni architetturali già prese.
2. `STATUS.md`, `ROADMAP.md`, `NEXT_STEPS.md` fanno riferimento
   incrociato e si aggiornano nello stesso merge che modifica lo
   stato descritto.
3. Esiste un vocabolario di *stato verificato* usato in modo
   consistente (`Implementato`, `Compilato`, `Testato`, `Validato in
   CI`), con table-leggenda obbligatoria.
4. I sorgenti contengono solo invarianti ancora valide; la storia
   pregressa è in `migrations/` o `CHANGELOG.md`, non nei commenti
   C++.
5. `tools/check_architecture_boundaries.sh` (estensibile) verifica
   la presenza di una "doc sync" entry nel merge che cambia
   comportamento.

## 5. Lavoro proposto

### D1 — Convenzione ADR

- [ ] Creare `docs/adr/README.md` con:
  - numerazione `ADR-NNN-slug-kebab.md`;
  - stati: **Proposed · Accepted · Deprecated · Superseded by ADR-MMM**;
  - struttura fissa (sezione "Status" prima di tutto, poi
    *Context · Decision · Consequences · Alternatives considered ·
    References*);
  - regola di chiusura: un ADR si firma **Accepted** solo quando il
    comportamento relativo è *validato in CI* (cfr. sezione 6).
- [ ] Migrare i due record equivalenti esistenti in `docs/migrations/`
      (`2026-06-renderer-state.md`,
      `2026-06-authoring-scene-composition.md`) come ADR, lasciando
      in `migrations/` il changelog dettagliato ma spostando lo
      *status* e la *decisione* nell'ADR.
- [ ] Aggiungere `docs/adr/INDEX.md` con titolo, stato, link.

### D2 — ADR obbligatori

> Almeno questi sette ADR devono esistere entro la chiusura del
> piano. Ogni ADR cita il commit o il PR che lo materializza.

- [ ] **ADR-001 — Frame Graph Compiler come unica superficie di
  compilazione** (motivazione: rimozione overload executor su raw
  graph e rimozione `ExecutionPlanCache` — commit
  `9f9af90e`). Stato attuale: *Accepted* (WP-1 in fase di chiusura).
- [ ] **ADR-002 — `RenderRuntime` possiede i servizi engine-lifetime**
  (asset resolver, scheduler, executor, catalogs, backend). Il
  renderer prende un puntatore al runtime. Motivazione: TICKET-011,
  PR-9 (`SoftwareBackend` estratto). Stato: *Accepted* ma da
  promuovere in PR-7.4.
- [ ] **ADR-003 — Confine SDK pubblico è solo `Chronon3D::SDK`**
  (facciata unica; PIMPL per i dettagli; nessun tipo di
  implementazione visibile). Cross-ref: regole 4, 16, 17, 19 di
  [`ANTI_DUPLICATION_RULES.md`](../ANTI_DUPLICATION_RULES.md).
  Stato: *Proposed* (WP-7 PR 7.4 + WP-8 PR 8.5 ancora pending).
- [ ] **ADR-004 — `ExecutionScope` per root/tile/precomp** (parent,
  arena, identità; child non resetta arena parent). Cross-ref:
  WP-6 / `03-execution-scope-and-precomp.md`. Stato: *Approved*
  (lavoro in `06/07..` ancora attivo).
- [ ] **ADR-005 — `AssetResolver` engine-local, niente bridge
  globali** (rimozione `g_active_runtime`,
  `g_process_wide_assets_root`, `typed_resolver_for_deep_code()`,
  `g_deep_resolver_mirror`). Cross-ref: WP-8 PR 8.1–8.2. Stato:
  *Proposed* (R6 del piano 06 in attesa).
- [ ] **ADR-006 — Registrazione via `ExtensionContext`** (no
  macro globali, no `static std::vector<Factory>`, no
  `CompositionPluginRegistry` parallelo). Cross-ref: regole 1, 2,
  3, 15 di [`ANTI_DUPLICATION_RULES.md`](../ANTI_DUPLICATION_RULES.md).
  Stato: *Accepted* (commit storici lo confermano, ma
  `builtin_composition_entries()` è stato rimosso PR 2).
- [ ] **ADR-007 — `SoftwareRenderSession` come unica sede per
  stato per-renderer** (frame history, dirty telemetry, buffer
  ring, scratch buffer, scene hasher, program store). Cross-ref:
  WP-3 PR 3.4, `docs/migrations/2026-06-renderer-state.md`.
  Stato: *Accepted* (lavoro phase 5 ancora attivo).

Tutti gli ADR futuri (Expressions V2 promozione, V3 tile-first,
nuove API) seguono lo stesso template.

### D3 — Regola di sincronizzazione dei documenti operativi

> "Regola di chiusura" comune a [`README.md`](README.md) e
> [`refactor-roadmap/README.md`](../refactor-roadmap/README.md),
> resa *eseguibile*.

- [ ] Definire la matrice di co-update obbligatoria:

| Cambio in | File da aggiornare nello stesso merge |
|---|---|
| `software_renderer.hpp` / `software_backend.hpp` | `STATUS.md`, `06-renderer-plan.md`, `refactor-roadmap/07-software-backend-completion.md`, eventuale ADR-002/-003, eventuale `TICKET-NNN` |
| `cmake/Chronon3DModules.cmake` | `STATUS.md`, `04-cmake-module-registry.md`, eventuale ticket |
| `vcpkg.json` o `CMakePresets.json` | `STATUS.md`, `08-dependency-profiles.md`, eventuale ticket |
| `RenderRuntime` / `RenderSession` / `RenderEngine` | `STATUS.md`, `NEXT_STEPS.md` (sezione P0), `refactor-roadmap/08-global-state-and-sdk.md`, eventuale ADR |
| `PrecompNode` / `SceneProgramStore` | `STATUS.md`, `refactor-roadmap/05-scene-program-store.md`, ADR-004 |
| Qualsiasi cosa in `experimental/` | ADR nuova + sezione *Esperimentale* in `STATUS.md` |

- [ ] Aggiungere `tools/check_doc_sync.sh` (o come regola dentro
  `check_architecture_boundaries.sh`) che per ogni commit rilevante
  -- via `--files-changed` -- verifica la presenza di una voce
  "doc sync" nel body del messaggio di commit quando i file
  toccati sono nelle righe della matrice.
- [ ] Aggiornare la sezione *Ruolo dei documenti* di
  [`STATUS.md`](../STATUS.md) per includere `docs/adr/` come
  registro canonico delle decisioni.

### D4 — Vocabolario di stato verificato

> Sostituisce la semantica implicita delle emoji usate in
> `STATUS.md` e in `refactor-roadmap/README.md`.

| Stato | Definizione operativa | Dove si applica |
|---|---|---|
| **Implementato** | Il codice è nel tree principale e compila in locale nella build di riferimento del maintainer | singola PR |
| **Compilato** | La CI ha confermato il build pulito (almeno preset `linux-ci`) | per merge commit |
| **Testato** | Le suite rilevanti passano (almeno `chronon3d_tests_fast`) sul commit | per merge commit |
| **Validato in CI** | La CI full-validation (`linux-full-validation` o equivalente `win-release`) passa senza flake su commit di chiusura, registrato in `CHANGELOG.md` | per chiusura ticket |

- [ ] Convertire le tabelle di `STATUS.md` e di
  `refactor-roadmap/README.md` all'uso di queste quattro etichette
  esplicite, mantenendo la *tabella leggenda* all'inizio di ogni
  file.
- [ ] Aggiornare i cherry-pick templates / PR template (se esistenti)
      perché includano il campo *stato di verifica* (almeno uno dei
      quattro sopra).

### D5 — Storia fuori dai sorgenti

- [ ] Regola: un commento C++ documenta solo un *invariante ancora
      valido*. Le decisioni storiche vanno in `docs/adr/` o in
      `docs/migrations/<data>-<slug>.md`.
- [ ] Audit dei file che ancora contengono riferimenti storici a
      `ExecutionPlanCache`, `default_assets_root`, `g_active_runtime`,
      `M-3`, `PR-X rewire` (vedi TICKET-007, TICKET-008, TICKET-011):
      tracciare in `docs/FOLLOWUP_TICKETS.md` i commenti da strippare
      una volta che la puntuale modifica è verde in CI.
- [ ] Promemoria nei file di test storici (`notes`, "Rationale" ecc.)
      che vivono solo in `docs/postmortems/` o in test migration
      docs.

### D6 — E2E: doc-ticket-code sync CI gate (interlock con WP-0)

> Ultima trincea: la regola D3 è eseguibile solo se WP-0 (gate
> architetturale affidabile) è in piedi.

- [ ] Allineamento con WP-0 PR 0.x: la regola "exit code non-zero
      su ogni violazione" copre anche la mancata doc-sync.
- [ ] Test di accettazione: cinque scenari
  (`tools/check_doc_sync_test.sh`):
  - merge senza `STATUS.md` toccato quando cambia un file
    `src/runtime/**` → fail;
  - merge senza ADR quando si introduce un nuovo public header in
    `include/chronon3d/**` → fail (via `tools/check_public_headers.py`);
  - merge su precomp senza aggiornare `refactor-roadmap/05-scene-program-store.md`
    → fail;
  - merge che chiude un TICKET-NNN senza aggiornare `CHANGELOG.md` →
    fail;
  - merge che cambia `vcpkg.json` senza aggiornare `08-dependency-profiles.md`
    → fail (cross-link con piano 08).

## 6. Test plan

Per ogni PR: test mirato prima della chiusura. Configurazione minima:
`linux-lean-dev` (test veloci) + `linux-ci` (install consumer).

| PR | Test gate |
|---|---|
| D1 | `docs/adr/README.md` esiste, ADR-001..007 numerati senza buchi |
| D2 | `git grep` di `g_active_runtime`/`ExecutionPlanCache` in `docs/adr/` non trova "dovrebbe essere rimosso" senza ADR-005/ADR-001 |
| D3 | `tools/check_doc_sync.sh` esce 1 su commit di prova senza sync |
| D4 | `tools/audit_status_vocab.py` (o equivalente) parsa tabelle e rifiuta emoji non mappate |
| D5 | `git grep -nE "R6|PR-[0-9] rewire|M-3"` su `include/`, `src/` decrementa rispetto a baseline |
| D6 | cinque scenari D6 in `tools/check_doc_sync_test.sh` verdi in `linux-ci` |

## 7. Criteri di chiusura

- [ ] D1..D6 merged nell'ordine descritto.
- [ ] `docs/adr/` con almeno sette ADR; ognuno ha *Status*,
      *Context*, *Decision*, *Consequences*, *References*;
      ogni ticket `TICKET-NNN` cita almeno un ADR o è un ADR.
- [ ] `STATUS.md`, `ROADMAP.md`, `NEXT_STEPS.md` aggiornati
      coerentemente con `CHANGELOG.md` (gate D3 verde).
- [ ] Tutte le tabelle di stato (incluso `refactor-roadmap/README.md`)
      usano esclusivamente i quattro stati di sezione D4.
- [ ] Audit D5: zero commenti "M-3/PR-X rewire/Phase A" non legati a
      ticket in `src/`, `include/`.
- [ ] `tools/check_doc_sync.sh` e `tools/check_doc_sync_test.sh`
      installati nel path `tools/`, eseguibili in `linux-ci`,
      citati come invarianti in `check_architecture_boundaries.sh`.
- [ ] Aggiornare `STATUS.md` ("Stato e roadmap sincronizzati"),
      `ROADMAP.md`, `NEXT_STEPS.md`, `CHANGELOG.md`.

## 8. Rischi e mitigazioni

| Rischio | Mitigazione |
|---|---|
| ADR diventano backlog di prosa | D1 fissa struttura + status; INDEX.md consultabile |
| Gate D3 troppo rumoroso in fase attiva | Whitelist esplicita per commit "wip" via `tools/check_doc_sync.sh --wip` (no fail) — solo `--strict` fallisce |
| Stato "Validato in CI" non ottenibile per esperimenti | `experimental/**` escluso per default; gating solo su `src/**` |
| Refactor-roadmap 00..08 e `refactor-roadmap/README.md` non migrazione completa al vocabolario D4 | D4 PR include un bot-style audit `tools/audit_status_vocab.py` con diff mitigation |
| D6 si intreccia con WP-0 PR 0.x | D6 consegnato dopo chiusura di WP-0; cita `tools/check_architecture_boundaries.sh` come dipendenza |

## 9. Segnale di completamento

Quando tutte le PR D1–D6 sono chiuse e i criteri di sezione 7 sono
verdi, marcare `Stato e roadmap sincronizzati` nel TODO generale di
[`README.md`](README.md).
