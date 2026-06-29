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

## Documenti canonici (single source of truth)

- `docs/CURRENT_STATUS.md` — stato presente.
- `docs/ROADMAP.md` — milestone prodotto.
- `docs/RELEASE_GATE.md` — requisiti di release.
- `docs/FOLLOWUP_TICKETS.md` — difetti e follow-up aperti.

I documenti storici (vecchi `docs/STATUS.md`, `docs/NEXT_STEPS.md`, `docs/stabilization-plan/**`,
plan agente legacy `docs/agent-tasks/`) vivono in `docs/ARCHIVE/` e **non** sono operativi.

## Stato assegnazioni agenti (snapshot 2026-06-29)

Tutti gli agenti della tornata (Agent 1 — Renderer/Backend Single Identity, Agent 2 — CMake Registry / SDK Boundary / Baseline, Agent 4 — Visual Verification) sono confluiti su `main`. Lavoro corrente sequenziale su `main`, un task alla volta.

Stato presente e blocker attivi: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
Documenti storici di quelle tornate in `docs/ARCHIVE/` (consultazione non operativa).

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