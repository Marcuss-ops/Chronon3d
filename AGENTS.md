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

I documenti storici (vecchi `docs/STATUS.md`, `docs/NEXT_STEPS.md`, `docs/stabilization-plan/**`,
plan agente legacy `docs/agent-tasks/`) vivono in `docs/ARCHIVE/` e **non** sono operativi.
Nessun riferimento operativo a path in `docs/ARCHIVE/` è consentito.

### Pattern di filename vietati

Sono vietati nuovi file con nome simile ai canonicals. In particolare:

* `docs/STATUS.md`, `docs/STATUS_NEXT.md`, `docs/STATUS_<variante>.md` → **VIETATO**
* `docs/NEXT.md`, `docs/NEXT_STEPS.md`, `docs/NEXT_<variante>.md` → **VIETATO**
* `docs/ROADMAP_<variante>.md` (es. `ROADMAP_DEV.md`) → **VIETATO**
* `docs/RELEASE_<variante>.md` (es. `RELEASE_v0.md`) → **VIETATO**
* `docs/FOLLOWUP_<variante>.md` (es. `FOLLOWUP_BACKLOG.md`) → **VIETATO**
* qualsiasi combinazione di prefissi simili (`CURRENT_STATUS_*.md`, `ROADMAP_*.md`, ...) → **VIETATO**

Per qualsiasi nuova informazione operativa: aggiungere il contenuto al **canonical esistente** appropriato
(stato → `CURRENT_STATUS`, requisiti → `RELEASE_GATE`, futuro → `ROADMAP`, blocker → `FOLLOWUP_TICKETS`).

### Regola del gate doc-sync

`tools/check_doc_sync.sh` deve accettare aggiornamenti **SOLO** ai 4 file canonical sopra elencati.
Qualsiasi check o validazione che richieda aggiornamenti a file diversi (es. `docs/STATUS.md`,
`docs/NEXT_STEPS.md`, `docs/stabilization-plan/...`) è vietato e deve essere rimosso —
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
| Scheda ticket specifico            | `docs/tickets/TICKET-NNN.md`                       |
| Decisione architetturale           | `docs/adr/ADR-NNN-<titolo>.md`                     |
| Materiale storico (non operativo)  | `docs/ARCHIVE/`                                     |



## 🔴 Feature Freeze — V0.1 (attivo dal 2026-06-29)

**Nessuna nuova feature viene accettata fino a baseline verde certificata**
(11 gate architetturali sullo stesso commit).

### Cosa è BLOCCATO

- Nuove feature camera, testo, rendering, shape, animazioni.
- Nuovi nodi render graph, effect processor, backend.
- Nuovi preset, registry, risolutori, sampler.
- Estensioni V3, tile-first, expressions V2.
- Nuovi formati di input/output.

### Cosa è CONSENTITO (solo queste categorie)

1. **Correzioni di build** — CMake, link, include mancanti, dipendenze.
2. **Test deterministici** — golden test, sentinel hash, regression gate.
3. **Rimozione percorsi legacy** — dead code, API deprecate, artifact obsoleti.
4. **Consumer SDK esterno** — certificazione install + link + `render_frame()`.
5. **Allineamento documentazione** — `docs/CURRENT_STATUS.md`, `ROADMAP.md`, `RELEASE_GATE.md`.

### Cosa è VIETATO

- Stime manuali di completamento (es. "X–Y%") non sono ammissibili.
  Sostituire con stato osservabile: `PASS` / `FAIL` / `PARTIAL` / `NOT RUN`
  (fonte canonica: `STATUS.generated.md` prodotto dalla CI quando disponibile).
- Nuovi `#include`, nuove classi pubbliche, nuovi `target_link_libraries`
  non strettamente necessari alle 5 categorie sopra.
- Qualsiasi modifica a `include/chronon3d/` che espanda la superficie API.

### Definizione di "baseline verde"

Tutti gli 11 check architetturali devono restituire PASS sullo **stesso commit**:

```bash
commit=$(git rev-parse HEAD)
bash tools/check_architecture_boundaries.sh          # 14 gate
bash tools/check_architecture_boundaries_selftest.sh # selftest
bash tools/check_software_renderer_boundary.sh       # sw boundary
bash tools/check_gitignored_dirs.sh                  # gitignore hygiene
bash tools/audit_software_renderer.sh                # sw audit
bash tools/check_camera_architecture.sh              # camera arch
bash tools/check_doc_sync.sh                         # doc sync
bash tools/check_filename_drift.sh                   # filename drift
bash tools/test_architectural.sh                     # TU-level rot
bash tools/install_consumer_test.sh                  # consumer SDK
bash tools/check_backend_sanitization.py             # backend sanitization
# 11/11 PASS → feature freeze revocato
```

### Revoca

Il feature freeze viene rimosso SOLO quando un commit su `main` registra
11/11 PASS e la baseline viene documentata in `docs/baselines/` con SHA
canonico. La revoca richiede un commit esplicito che rimuove questa sezione.

---

## Priorità obbligatoria

1. Ottenere baseline verde: 11/11 gate sullo stesso commit (feature freeze attivo).
2. Certificare una sola strategia di packaging CMake per l'SDK (verifica `ar t` + `nm -C`).
3. Unificare toolchain/preset vcpkg.
4. Chiudere installazione ed external consumer SDK.
5. Riallineare `docs/CURRENT_STATUS.md` e `docs/ROADMAP.md` alla baseline osservata (i file `STATUS.md` / `NEXT_STEPS.md` sono ora storici in `docs/ARCHIVE/`).
6. Solo dopo baseline verde e SDK certificato: Text V1, Camera V1, V0.1 release.

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
