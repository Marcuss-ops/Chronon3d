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

## Stato assegnazioni agenti (snapshot 2026-06-23)

Snapshot dello stato corrente (vedi body per lo stato per-agente). L'assegnazione iniziale prevedeva due incarichi paralleli per ridurre conflitti e duplicazioni; la realtà attuale è sequenziale.

1. [Agente 1 — Renderer/Backend Single Identity](docs/agent-tasks/AGENT_1_RENDERER_BOUNDARY.md)
   - branch: `codex/agent1-renderer-boundary` **[DONE ✓ — Merged into main on 2026-06-23, branch retired]**
   - ownership: renderer/backend software, call site correlati, test mirati e gate renderer.
2. [Agente 2 — CMake Registry, SDK Boundary e Baseline](docs/agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md)
   - branch: `codex/agent2-cmake-sdk-baseline` **[COMPLETED — merged into main at ccabb574 on 2026-06-24]**
   - ownership: CMake, preset/toolchain, install consumer, full validation e documenti canonici.

Entrambi gli agenti sono completati. Lavoro sequenziale su `main`, un task alla volta.

## Priorità obbligatoria

1. Ripristinare una sola identità renderer/backend e rendere bloccante il relativo gate.
2. Centralizzare la registrazione dei moduli CMake.
3. Unificare toolchain/preset vcpkg.
4. Chiudere installazione ed external consumer SDK.
5. Registrare una baseline reale su checkout pulito.
6. Allineare `STATUS.md`, `NEXT_STEPS.md`, `ROADMAP.md` e stabilization plan ai risultati osservati.
7. Proseguire con ExecutionScope, Precomp e determinismo soltanto sui gap ancora dimostrati dalla nuova baseline.
8. Non iniziare V3 prima della chiusura completa dei P0.

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