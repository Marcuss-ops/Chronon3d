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

## Priorità obbligatoria

1. Baseline e dichiarazioni di stato corrette.
2. Tre test falliti.
3. Due violazioni architetturali.
4. Preset `linux-lean-dev`.
5. Consumer SDK e toolchain/prefix vcpkg.
6. Golden hash reali, senza sentinel.
7. Sincronizzazione ExecutionScope.
8. Migrazione completa Precomp e call site legacy.
9. Canonicalizzazione documenti.
10. Registry centrale CMake.

## Regole di lavoro

- Cercare prima il codice e i documenti esistenti.
- Non duplicare registry, resolver, sampler, cache o checklist.
- Non segnare verde una suite che restituisce failure.
- Non cambiare un gate per nascondere un errore.
- Aggiornare il piano relativo nello stesso commit che cambia lo stato.
- Ogni nuova feature deve usare il registry, resolver o sampler canonico già esistente.
- Non introdurre GUI, browser o dipendenze GPU nel core headless CPU-first.

## Quando un file sembra mancare

1. Controllare `git status -sb`.
2. Controllare `git rev-parse HEAD`.
3. Eseguire `git fetch origin`.
4. Confrontare `HEAD` con `origin/main`.
5. Aggiornare il checkout prima di concludere che il file non esiste.

Non creare un nuovo file sostitutivo con nome simile: usare sempre i percorsi canonici sopra.