# Chronon3D Agent Instructions

Questo file è il punto di ingresso obbligatorio per ogni agente che lavora nel repository.

## Prima di iniziare

L'agente deve lavorare su `main` aggiornato:

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git status -sb
```

Se il checkout non contiene i file elencati sotto, il clone o worktree è obsoleto. Non inventare percorsi alternativi e non ricreare copie dei documenti.

Verifica rapida (i documenti canonici devono essere presenti nel checkout):

```bash
git ls-tree -r --name-only HEAD docs/CURRENT_STATUS.md docs/ROADMAP.md docs/RELEASE_GATE.md docs/FOLLOWUP_TICKETS.md
```

## Insieme canonico della documentazione

I seguenti quattro file sono le **uniche fonti canoniche** di stato, requisiti, roadmap e problemi.
Qualsiasi stato osservabile del repository deve risiedere in uno (e solo uno) di questi file.

| Canonical                                          | Responsabilità unica                                                          |
| -------------------------------------------------- | ----------------------------------------------------------------------------- |
| [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) | stato presente (SHA, ultima baseline, blocker correnti, stato per area)        |
| [`docs/ROADMAP.md`](docs/ROADMAP.md)               | direzione futura (milestone, gate di uscita, non-goal)                        |
| [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md)     | requisiti permanenti di release (Text V1, Camera V1, SDK V1)                 |
| [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) | indice one-line dei blocker attivi (max ~10 righe)                        |

I documenti storici (vecchi `docs/STATUS.md`, `docs/NEXT_STEPS.md`, `docs/stabilization-plan/**`,    // drift-allow: archived-doc-pattern
plan agente legacy `docs/agent-tasks/`) vivono in `docs/ARCHIVE/` e **non** sono operativi.
Nessun riferimento operativo a path in `docs/ARCHIVE/` è consentito.

### Pattern di filename vietati

Sono vietati nuovi file con nome simile ai canonicals. In particolare:

* `docs/STATUS.md`, `docs/STATUS_NEXT.md`, `docs/STATUS_<variante>.md` → **VIETATO**    // drift-allow: archived-doc-pattern
* `docs/NEXT.md`, `docs/NEXT_STEPS.md`, `docs/NEXT_<variante>.md` → **VIETATO**    // drift-allow: archived-doc-pattern
* `docs/ROADMAP_<variante>.md` (es. `ROADMAP_DEV.md`) → **VIETATO**
* `docs/RELEASE_<variante>.md` (es. `RELEASE_v0.md`) → **VIETATO**
* `docs/FOLLOWUP_<variante>.md` (es. `FOLLOWUP_BACKLOG.md`) → **VIETATO**
* qualsiasi combinazione di prefissi simili (`CURRENT_STATUS_*.md`, `ROADMAP_*.md`, ...) → **VIETATO**

Per qualsiasi nuova informazione operativa: aggiungere il contenuto al **canonical esistente** appropriato
(stato → `CURRENT_STATUS`, requisiti → `RELEASE_GATE`, futuro → `ROADMAP`, blocker → `FOLLOWUP_TICKETS`).

### Regola del gate doc-sync

`tools/check_doc_sync.sh` deve accettare aggiornamenti **SOLO** ai 4 file canonical sopra elencati.
Qualsiasi check o validazione che richieda aggiornamenti a file diversi (es. `docs/STATUS.md`,    // drift-allow: archived-doc-pattern
`docs/NEXT_STEPS.md`, `docs/stabilization-plan/...`) è vietato e deve essere rimosso — // drift-allow: archived-doc-pattern
quei file sono storici, vivono in `docs/ARCHIVE/` e non possono essere modificati operativamente.

### Documenti di supporto (NON fonti canoniche di stato)

I seguenti file sono **ruoli documentali di supporto**: possono linkare i canonicals ma **non** devono duplicarne il contenuto.
Per il contratto completo di ciascun ruolo, vedi [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md).

| Ruolo                              | File                                               |
| ---------------------------------- | -------------------------------------------------- |
| Contratto documentale              | `docs/DOCUMENTATION_GOVERNANCE.md`                 |
| Audit registry                     | `docs/ARCHITECTURE_AUDIT.md`                       |
| Inventario feature                 | `docs/FEATURES.md`                                 |
| Piano testo                        | `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`      |
| Matrice camera                     | `docs/CAMERA_FEATURE_MATRIX.md`                    |
| Futuro tile-first                  | `docs/V3_BLUEPRINT.md`                             |
| Chiusure recenti                   | `docs/CHANGELOG.md`                                |
| Baseline macchina-verificata       | `docs/baselines/main-<sha>-baseline.md`             |
| Scheda ticket specifico            | `docs/tickets/TICKET-NNN.md`                       |    // drift-allow: ticket-template-pattern
| Decisione architetturale           | `docs/adr/ADR-NNN-<titolo>.md`                     |
| Materiale storico (non operativo)  | `docs/ARCHIVE/`                                     |



## ✅ Feature Freeze — REVOCATO (2026-07-06)

**Baseline verde certificata: `main@7eb5c2ba` — 11/11 PASS.**

Il feature freeze V0.1, attivo dal 2026-06-29, è stato revocato.
La baseline è documentata in [`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md).

### Regole permanenti (ereditate dal freeze)

Queste regole restano in vigore come best practices, non più come blocco:

- **No stime percentuali**: usare stato osservabile `PASS` / `FAIL` / `PARTIAL` / `NOT RUN`.
- **No espansione API non necessaria**: ogni nuovo simbolo in `include/chronon3d/` va giustificato.
- **No nuovi singleton/registry/resolver/cache** senza ADR.
- **No `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>`** senza ADR.

---

### Install Pipeline Plumbing (Cat-4 ancillary)

Asset class documentale per audit/tooling ancillari al
`tools/install_consumer_test.sh` pipeline (categoria 4 del feature
freeze: external consumer SDK).  Questi script NON sono parte dei 11/11
gate baseline — sono rot-pattern preventive FU4-lineage, invocati
durante la pipeline install/consumer o stand-alone via `bash`.

Asset category: **`INSTALL_PIPELINE_PLUMBING`**.

| Script                                          | Asset Category              | Function |
|-------------------------------------------------|-----------------------------|----------|
| `tools/audit_incomplete_type_pattern.sh`        | INSTALL_PIPELINE_PLUMBING   | std::make_shared\<T\> umbrella-header full-def probe. Emette **`BROKEN`** se l'header canonico del tipo T in `include/chronon3d/` contiene solo `class T;` (forward declaration) invece di `struct T { ... }` (full definition).  FU4 rot preventive (TICKET-GATE-10-PHASE-4-BLACK-FU4 closure lineage). |

Stand-alone usage:

```bash
bash tools/audit_incomplete_type_pattern.sh                        # default: tests/install_consumer/ + include/chronon3d/
INCOMPLETE_TYPE_SCAN_PATHS='tests/integration_test/main.cpp src/sdk_consumer/' \  <!-- drift-allow: stale-ref -->
  bash tools/audit_incomplete_type_pattern.sh                       # override scan paths
```

Exit codes (custom): 0 = clean, 1 = BROKEN rot detected, 2 = internal
error.  Status degli script INSTALL_PIPELINE_PLUMBING è tracciato in
`docs/CHANGELOG.md`; rot-pattern detect è referenziato come blocker in
`docs/FOLLOWUP_TICKETS.md` quando attiva.

---

## Priorità obbligatoria

1. Mantenere baseline verde: 11/11 gate su ogni commit su `main`.
2. Text V1 completamento (golden test, preset, kinetic typography).
3. Camera V1 completamento (runtime certification, DOF, motion blur, framing).
4. V0.1 release (SDK packaging, cross-language ABI, formato `.chronon`).
5. Certificare una sola strategia di packaging CMake per l'SDK (verifica `ar t` + `nm -C`).
6. Riallineare `docs/CURRENT_STATUS.md` e `docs/ROADMAP.md` alla baseline osservata.

## Regole di lavoro

- Cercare prima il codice e i documenti esistenti.
- Non duplicare registry, resolver, sampler, cache, service locator o checklist.
- Non segnare verde una suite che restituisce failure.
- Non cambiare un gate per nascondere un errore.
- Aggiornare il piano relativo nello stesso commit che cambia lo stato.
- Ogni nuova feature deve usare il registry, resolver o sampler canonico già esistente.
- Non introdurre GUI, browser o dipendenze GPU nel core headless CPU-first.
- Non introdurre `#include <msdfgen>`, `<libtess2>` o `<unicode[/...]>` da nessuna parte — dei pattern deny-everywhere di Gate 5 in `tools/check_architecture_boundaries.sh` (Check 11). Per deroghe serve prima un ADR.
- Fare PR piccole e mirate, senza mescolare refactor indipendenti.
- Non committare `node_modules/`, directory di build, output, artefatti o file generati.
- Eseguire almeno i test del modulo toccato prima della PR.
- Dopo ogni push verificare la cronologia recente con `git log -n 5 --oneline`.

## Workflow Git obbligatorio

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
```

> **Per-branch rebase convention (read-side):** future `git pull` (unpulled) su una branch con `branch.<name>.rebase = true` esegue rebase invece di merge non-fast-forward (history lineare). Verificare lo stato con `git config --local --get-regexp '^branch\.'` — ogni entry per-branch DEVE mostrare `rebase = true`. Impostare idempotentemente con `git config branch.<name>.rebase true` (per-repo local, **NON** `--global`).
>
> Questo è il pezzo **read-side** della triade main-sync hygiene: per-branch rebase (read, qui) + `tools/wrap_push.sh` GATE-MNT-01 (push-side wrapper) + `tools/check_main_clean.sh` (fail-on-dirty gate). Vedi § GATE-MNT-01 in fondo a questo file per il push-side + il gate canonicali (anchor-resilient: nessuna dipendenza da slug di GitHub; il riferimento è plain-text). Su `main` la verifica canonica è: `git config --local --get branch.main.rebase` ⇒ `true`.

Dopo le modifiche:

```bash
git status -sb
git diff
# test mirati
git add <solo-file-modificati>
git commit -m "<tipo(scope): descrizione chiara>"
git push origin main
git log -n 5 --oneline
```


## Quando un file sembra mancare

1. Controllare `git status -sb`.
2. Controllare `git rev-parse HEAD`.
3. Eseguire `git fetch origin`.
4. Confrontare `HEAD` con `origin/main`.
5. Aggiornare il checkout prima di concludere che il file non esiste.

Non creare un nuovo file sostitutivo con nome simile: usare sempre i percorsi canonici sopra.
## GATE-MNT-01 — main-sync fail-on-dirty gate (TICKET-048 closure)

Prima di ogni `git push` su `main` il checkout deve essere gated da `tools/check_main_clean.sh` che fallisce (exit ≠ 0) se:

1. `git fetch origin` non riesce.
2. `HEAD` e `origin/main` sono divergenti (nessuno dei due è antenato dell'altro); FF-pull, post-commit-push e uguaglianza sono tutti accettati (vedi `tools/check_main_clean.sh` per l'implementazione).
3. `git status -s` non vuoto (uncommitted o untracked).
4. `git config --local --get branch.main.rebase` ≠ `true` (per-branch rebase invariant; chiusura GATE-MNT-01-EXT, idempotent + auto-repair via `tools/wrap_push.sh` Step 2.5 + `tools/install_consumer_test.sh` Step 0).

### Wrapper canonico (portabile, tracked)
```bash
tools/wrap_push.sh origin main    # drop-in per `git push`
```
Il wrapper (`tools/wrap_push.sh`) — vedi closure lineage TICKET-048 + TICKET-067/TICKET-075 + **TICKET-076** — esegue in ordine:
  1. `git fetch $REMOTE` + parse args (default `origin` + current branch);
  2. **auto-FF unidirezionale**: se `HEAD` != `$REMOTE/$BRANCH` E `is-ancestor HEAD $REMOTE_REF` (cioè `origin/<branch>` è discendente fast-forward-puro di `HEAD`), esegue `git merge --ff-only "$REMOTE/$BRANCH"`. Se FF fallisce (vera divergenza) emette `GATE_FAIL` diagnostico + hint `git pull --rebase`;
  3. invoca `tools/check_main_clean.sh`. Se gate PASS, inoltra `git push "$@"` (drop-in trasparente, tutti gli args originali vengono forwardati). È il punto di ingresso canonico per il workflow Agent3 atomic-commit.

### Hook difensivo (per-repo, local-only)
```bash
.git/hooks/pre-push               # auto-installed local; NON tracked
```
Invocato automaticamente da qualsiasi `git push` (no global git config). Stesso gate: `tools/check_main_clean.sh`. Presente dopo il primo commit di Agent3 in un nuovo clone via questo stesso commit.

### Re-installazione post-clone / post-rebase
```bash
cp .git/hooks/pre-push.example .git/hooks/pre-push 2>/dev/null || true
# oppure usare direttamente `tools/wrap_push.sh origin main` per ogni push
```

### Smoke-test del gate
```bash
tools/check_main_clean.sh   # atteso: GATE_PASS, exit 0  (HEAD==origin/main, clean tree)
```

### Step 1 (GATE-MNT-01-EXT): per-branch rebase verify + auto-repair

**Verify (gate-side, read-enforcement):** `tools/check_main_clean.sh` Step 4 rifiuta con `GATE_FAIL: branch.main.rebase != 'true'` (single-line check: `[ ... ] = "true" || { diagnostic; exit 1; }`).

**Auto-repair (push-side, write-enforcement):** `tools/wrap_push.sh` Step 2.5 imposta `branch.${TARGET_BRANCH}.rebase=true` quando la chiave è **mancante** — idempotent + forward-only (influenza `git pull` futuri). Loggato quando accade (silent mutation = violazione di "non sorprendere l'utente"). I valori espliciti non-`true` sono preservati (esplicito > default operativo).

**Bootstrap (consumer-side):** `tools/install_consumer_test.sh` Step 0 applica la stessa auto-repair su `branch.main.rebase` per il checkout consumer — invocazioni agent post-clone si auto-riparano prima dell'inizio della pipeline install/test.

**Closure lineage:** TICKET-048 (gate wrap) → TICKET-067/075 (merge-base ancestor) → TICKET-076 (auto-FF in wrapper) → **GATE-MNT-01-EXT** (per-branch rebase verify + auto-repair).

**Smoke-test (manuale, presubmit):**
```bash
git config --local --unset branch.main.rebase
tools/check_main_clean.sh     # atteso: GATE_FAIL ... != 'true'
git config branch.main.rebase true
tools/check_main_clean.sh     # atteso: GATE_PASS
tools/wrap_push.sh origin main  # atteso: auto-repair log only on first invocation; push proceeds
```
