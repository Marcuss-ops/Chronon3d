# Agent Workflow — Chronon3d

Questo documento definisce come lavorano gli agenti sulla repo senza annullarsi a vicenda.

Obiettivo: evitare modifiche concorrenti e sovrapposte su graph, specscene, scene, layer, executor ed export.

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
src/render_graph/executor/internal.hpp
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
internal.cpp/internal.hpp: target svuotamento progressivo
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

## 6. Comando Per Trovare File Caldi

```bash
git log --format= --name-only --all \
  | sed '/^$/d' \
  | sort \
  | uniq -c \
  | sort -nr \
  | sed -n '1,40p'
```

Quando un file core resta tra i piu' modificati, non aggiungere altra logica li'. Estrarre un extension point o un helper dedicato.
