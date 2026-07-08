# Agent Workflow — Chronon3d

Questo documento definisce come lavorano gli agenti sulla repo senza annullarsi a vicenda.

Obiettivo: evitare modifiche concorrenti e sovrapposte su graph, specscene, scene, layer, executor ed export.

Riferimento operativo: [docs/DEFINITION_OF_DONE.md](./DEFINITION_OF_DONE.md) definisce i gate minimi prima di considerare una PR chiusa.

---

## 1. Regola Base

Un task deve avere un solo dominio primario.

Dominio valido:

```text
graph
executor
scene
specscene
layer-builder
export-video
extension
content
docs
tests
build
```

Non modificare `graph`, `specscene` e `layer` nello stesso task, salvo task architetturale esplicito.

---

## 2. File Orchestratori

Questi file devono restare piccoli e non devono ricevere nuova logica di dominio:

```text
src/render_graph/builder/graph_builder_pipeline.cpp
src/render_graph/builder/graph_build_pipeline.cpp
apps/chronon3d_cli/commands/video/video_export_pipe.cpp
apps/chronon3d_cli/commands/video/video_export.cpp
src/render_graph/executor/executor.cpp
src/render_graph/executor/internal.cpp
```

Se serve aggiungere comportamento, creare un file dedicato:

```text
src/render_graph/builder/graph_builder_<domain>.cpp
src/render_graph/builder/passes/<domain>_pass.cpp
src/render_graph/executor/<domain>.cpp
apps/chronon3d_cli/commands/video/<domain>_helpers.cpp
```

---

## 3. Prima Di Modificare Core

Scrivere nel commit, nella PR o nella nota di lavoro:

```text
Dominio:
File protetti toccati:
Motivo:
Perche' non basta un file nuovo:
Test/build eseguiti:
Rischio di conflitto con altri agenti:
```

Se questa scheda non ha risposta chiara, il task va ridotto.

---

## 4. Regole Anti-Conflitto

- Un agente non deve fare refactor core e feature nello stesso commit.
- Un agente non deve spostare API pubbliche mentre un altro sta lavorando su feature sopra quelle API.
- Nuova logica va in file nuovi quando possibile.
- File gia' ridotti non devono ricrescere sopra soglia senza decisione esplicita.
- I commit devono essere piccoli e leggibili: un dominio, un motivo.
- Dopo un pull con nuovi commit remoti, fare build mirata prima del push.

Soglie operative:

```text
graph_builder_pipeline.cpp: massimo 80 righe
video_export_pipe.cpp: massimo 500 righe fino a split exporter completo
executor.cpp: target sotto 300 righe
internal.cpp: target svuotamento progressivo
```

---

## 5. Flusso Consigliato

```text
1. git fetch origin main
2. git status --short --branch
3. scegliere un solo dominio
4. controllare docs/CORE_OWNERSHIP.md
5. modificare o creare file locali al dominio
6. eseguire build/test mirati
7. aggiornare docs se cambia ownership o architettura
8. commit con messaggio specifico
9. rebase/pull se origin/main e' cambiato
10. build mirata
11. push
```

---

## 6. Pre-push hygiene gates (Atomic Commits - TICKET-110)

Prima di inoltrare `git push`, lo script `tools/wrap_push.sh` esegue una catena di gate bloccanti
in ordine (Exit code: 0 = pass, 1 = fail). La parity in CI e garantita eseguendo i medesimi
script nei workflow GitHub Actions corrispondenti.

**Sequenza gate (locale via `tools/wrap_push.sh`):**

1. **`tools/check_main_clean.sh`** (GATE-MNT-01) — verifica rebase-clean, nessuna divergenza da
   `origin/main`, nessun dirty tree, `branch.main.rebase=true`. Chiusura TICKET-076.
2. **`tools/check_test_hygiene.sh`** (gate #10b doctest) — blocca `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`
   duplicato in `tests/**` (escluso `tests/test_main.cpp`). Fix: includere `<doctest/doctest.h>`
   invece di ridefinire l'implementazione nei file di test aggiuntivi. Chiusura del
   10-point friction audit item #10.
3. **`tools/check_test_suite_registration.sh`** (gate #10c, promosso bloccante in commit
   `7e7c8498`) — blocca `add_executable(chronon3d_*test_*...)` grezzo; ogni test target deve
   passare per `chronon3d_add_test_suite(NAME TIER SOURCES [LINK_TARGETS] [LABELS])`.
   Audit finale: **41 SUITE / 0 RAW**.

**Parity in CI (workflow GitHub Actions):**

| Gate | Workflow | Step name | Trigger |
|------|----------|-----------|---------|
| `check_test_hygiene.sh` | `.github/workflows/ci.yml` | "Gate / Test Hygiene" | ogni PR + push (pre-build; **Linux only via `if: runner.os == 'Linux'`** perché la matrice include Windows Release) |
| `check_test_suite_registration.sh` | `.github/workflows/gates-full-validation.yml` | "Gate / Test Suite Registration" | PR-with-sensitive-changes + push (pre-build, paths-scoped a `tests/*` + `CMakeLists.txt`; **Ubuntu-pinned via `runs-on: ubuntu-latest`**) |
| `check_main_clean.sh` | non CI (GATE-MNT-01 = local-only per design) | — | invocato localmente da `tools/wrap_push.sh` + `.git/hooks/pre-push` |

**Nessun `--skip-gates` escape hatch.** Motivi: (a) un duplicate `DOCTEST_MAIN` rompe il link;
(b) un raw `add_executable` bypassa il source-registry contract §11.1; (c) deferire il fail a CI
inquina la cronologia `main` con commit rotti. Hardblock sempre.

Workflow locale canonico per atomic-commit (sequenza post-modifica):

```bash
git fetch origin && git checkout main && git pull --ff-only origin main
# ...edit/test/commit...
bash tools/wrap_push.sh origin main   # drop-in per `git push`, applica la catena gate intera
```

## 7. Comando Per Trovare File Caldi

```bash
git log --format= --name-only --all         | sed '/^$/d'  | sort       | uniq -c | sort -nr    | sed -n '1,40p'
```

Quando un file core resta tra i piu' modificati, non aggiungere altra logica li'. Estrarre un extension point o un helper dedicato.
