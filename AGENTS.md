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

Verifica rapida:

```bash
git ls-tree -r --name-only HEAD docs/stabilization-plan
git ls-tree -r --name-only HEAD docs/agent-tasks
```

## Piano operativo canonico

Indice principale:

- `docs/stabilization-plan/README.md`

Work package:

- `docs/stabilization-plan/01-baseline-green.md`
- `docs/stabilization-plan/02-determinism.md`
- `docs/stabilization-plan/03-execution-scope-and-precomp.md`
- `docs/stabilization-plan/04-cmake-module-registry.md`
- `docs/stabilization-plan/05-sdk-plan.md`
- `docs/stabilization-plan/06-renderer-plan.md`
- `docs/stabilization-plan/07-documentation-and-adrs.md`
- `docs/stabilization-plan/08-dependency-profiles.md`
- `docs/stabilization-plan/09-document-canonicalization.md`

Documenti generali da leggere insieme:

- `docs/STATUS.md`
- `docs/NEXT_STEPS.md`
- `docs/ROADMAP.md`
- `docs/FOLLOWUP_TICKETS.md`
- `docs/ANTI_DUPLICATION_RULES.md`

## Stato assegnazioni agenti (snapshot 2026-06-24)

Snapshot dello stato corrente (vedi body per lo stato per-agente). L'assegnazione iniziale prevedeva due incarichi paralleli per ridurre conflitti e duplicazioni; la realtà attuale è sequenziale.

1. [Agente 1 — Renderer/Backend Single Identity](docs/agent-tasks/AGENT_1_RENDERER_BOUNDARY.md)
   - branch: `codex/agent1-renderer-boundary` **[DONE ✓ — Merged into main on 2026-06-23, branch retired]**
   - ownership: renderer/backend software, call site correlati, test mirati e gate renderer.
2. [Agente 2 — CMake Registry, SDK Boundary e Baseline](docs/agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md)
   - branch: `codex/agent2-cmake-sdk-baseline` **[COMPLETED]**
   - ownership: CMake, preset/toolchain, install consumer, full validation e documenti canonici.
   - closed-state baseline: [`docs/baselines/main-345e5f2e-txt-00-closed.md`](docs/baselines/main-345e5f2e-txt-00-closed.md) (main@`345e5f2e`, audit-pinned at commit `b8114705`, 2026-06-24).
3. [Agente 4 — Verifica visuale e integrazione](docs/agent-tasks/AGENT_4_VERIFY_VISUAL_INTEGRATION.md)
   - branch: `main` **[DONE ✓ — landed via atomic commit `db81d51d` on 2026-06-24; pre-pickup post-Agente 1+2]**
   - ownership: verifica end-to-end (test-only + doc-only); nessun `include/`, `src/`, `content/` modificato.

Tutti gli agenti di questa tornata sono completati. Lavoro sequenziale su `main`, un task alla volta.

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
5. **Allineamento documentazione** — `STATUS.md`, `ROADMAP.md`, `RELEASE_GATE.md`.

### Cosa è VIETATO

- Percentuali manuali di completamento ("Camera 70-75%", "Text 60-65%").
  Sostituire con `STATUS.generated.md` prodotto dalla CI.
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
5. Riallineare `STATUS.md`, `NEXT_STEPS.md`, `ROADMAP.md` alla baseline osservata.
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