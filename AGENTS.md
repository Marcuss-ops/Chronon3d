# Chronon3D Agent Instructions

Punto di ingresso obbligatorio per ogni agente che lavora nel repository.

## Workflow Git obbligatorio

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git checkout -b codex/<task>
```

Durante il lavoro:

```bash
git rebase origin/main   # frequente
git status -sb           # prima di ogni commit
git diff                 # verifica le modifiche
# test mirati sul modulo toccato
git add <solo-file-modificati>
git commit -m "<tipo(scope): descrizione>"
git push origin codex/<task>
git log -n 5 --oneline   # dopo il push
```

- **Mai pushare direttamente su `main`.**
- Una PR = un solo problema.
- PR piccola e reviewable.
- Rebase su `origin/main` prima del merge.
- Dopo il merge, il branch va rimosso.

## Regole di lavoro

- Cercare prima il codice e i documenti esistenti.
- Non duplicare registry, resolver, sampler, cache, service locator o checklist.
- Non segnare verde una suite che restituisce failure.
- Non cambiare un gate per nascondere un errore.
- Aggiornare i documenti nello stesso commit che cambia lo stato.
- Ogni nuova feature deve usare il registry, resolver o sampler canonico già esistente.
- Non introdurre GUI, browser o dipendenze GPU nel core headless CPU-first.
- Non introdurre `#include <msdfgen>`, `<libtess2>` o `<unicode[/...]>` da nessuna parte.
- Non committare `node_modules/`, directory di build, output, artefatti o file generati.
- Eseguire almeno i test del modulo toccato prima della PR.

## Documenti canonici

- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) — stato presente.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone future.
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti per una release valida.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — ticket e difetti tracciati.

## Quando un file sembra mancare

1. `git fetch origin`
2. `git status -sb`
3. `git rev-parse HEAD`
4. Confrontare `HEAD` con `origin/main`.
5. Aggiornare il checkout prima di concludere che il file non esiste.

Non creare un file sostitutivo con nome simile: usare sempre i percorsi canonici.
