# TICKET-122 — Fix render-order: grid_background eseguito DOPO le card (non-deterministico)

## Stato

**PLANNED** — diagnostica completata in TICKET-121 FASE 7. Il root cause è confermato: il `GraphExecutor` visita i nodi in ordine topologico non-deterministico, causando l'esecuzione del `grid_background` DOPO le card e coprendole con il suo sfondo opaco.

## Priorità

P1 — blocca la risoluzione definitiva delle collisioni hash AE_CAM_02 e AE_CAM_04.

## Problema

Il `RenderGraph` memorizza i nodi in ordine di creazione layer (`std::vector`), con il grid_background PRIMA delle card. Tuttavia, il `GraphExecutor` visita i nodi in **ordine topologico** dal nodo di output, non in ordine di inserimento. I nodi foglia (SourceNode senza dipendenze tra loro) possono essere eseguiti in qualsiasi ordine.

### Evidenza empirica (TICKET-121 FASE 7)

Log `spdlog::warn` in `SourceNode::execute()` prima di `draw_node()` per AE_CAM_02:

```
frame=0:  card(1) → grid(9) → dot(3) → card → card → card → card
frame=30: grid(9) → card → card → card → dot(3) → card
frame=60: grid(9) → card → card → card → card → dot(3)
```

**L'ordine è non-deterministico.** A frame 0, il grid (shape=9) viene disegnato SECONDO, dopo una card, coprendola con `bg_color` opaco (alpha=1.0). L'hash risultante è quello del "solo grid background" (`cc86d2b5...`), condiviso da CAM_02, CAM_04, CAM_07, CAM_09.

### Impatto

- **AE_CAM_02** (zoom 500→1500): `hash(frame0) == hash(frame60)` — grid copre le card
- **AE_CAM_04** (parent null Z-dolly): `hash(frame0) == hash(frame60)` — stesso bug
- **AE_CAM_07** (gatefit): hash statico `cc86d2b5...` — grid da solo
- **AE_CAM_09** (motion blur): `hash(frame0) == hash(frame30)` — grid copre tutto

### Root cause tecnico

1. **`RenderGraph::m_nodes`** è un `std::vector<std::unique_ptr<RenderGraphNode>>` — ordine corretto (layer creation order)
2. **`GraphExecutor`** visita i nodi in ordine topologico partendo dal nodo di output
3. I nodi SourceNode sono tutti foglie (nessuna dipendenza tra loro) → ordine di visita non garantito
4. Il nodo di output (compositor/layer_pipeline) riceve input da tutti i SourceNode e li compone in ordine non deterministico

## Soluzione

Tre opzioni di fix, dalla più semplice alla più robusta:

### Opzione A — Stable sort nel GraphExecutor (preferita)

Nel `GraphExecutor`, quando si iterano i nodi foglia (senza dipendenze), usarne l'ordine di inserimento nel grafo invece dell'ordine di visita topologica. I nodi sono già in ordine di layer nel `m_nodes` vector.

**File da modificare:** `src/render_graph/executor/graph_executor.cpp` (o equivalente)

**Cambiamento:** ordinare i nodi ready-to-execute per `node_id` crescente (che corrisponde all'ordine di `add_node()` = ordine di layer).

### Opzione B — Edge espliciti tra layer

Nel `graph_builder`, aggiungere dipendenze tra SourceNode consecutivi dello stesso tipo di layer:
```
grid → card_subject → card_tl → card_tr → ... → output
```

**File da modificare:** `src/render_graph/builder/graph_builder_layer_pipeline.cpp`

**Cambiamento:** dopo aver creato tutti i SourceNode per i layer Normal/Shape, connetterli in ordine sequenziale.

### Opzione C — Ordinamento nel compositor

Nel layer pipeline pass (che compone i FB dei vari layer), ordinare gli input per `layer_index` o `node_id` prima di comporli.

**File da modificare:** `src/render_graph/builder/passes/graph_builder_layer_pipeline_pass.cpp`

**Cambiamento:** aggiungere uno step di sort prima della composizione.

## Azioni da eseguire

1. Studiare il `GraphExecutor` per capire come vengono schedulati i nodi foglia.
2. Implementare l'Opzione A (stable sort per node_id) se il GraphExecutor lo permette.
3. In alternativa, implementare l'Opzione B (edge espliciti).
4. Ricompilare `chronon3d_ae_parity_tests`.
5. Rigenerare golden PNGs: `CHRONON3D_UPDATE_GOLDENS=1`.
6. Verificare: `sha256sum(frame0) != sha256sum(frame60)` per CAM_02 e CAM_04.
7. Verificare: 35/35 AE_CAM PASS, 0 FAIL.
8. Rimuovere i MESSAGE workaround in `ae_parity_tests.cpp` per CAM_02 e CAM_04.
9. Chiudere TICKET-122 come DONE.

## Check finale DONE

- `AE_CAM_02 zoom 500→1500` produce `hash(frame0) != hash(frame60)` ✅
- `AE_CAM_04 parent null` produce `hash(frame0) != hash(frame60)` ✅
- `AE_CAM_07 gatefit` produce hash diverso da `cc86d2b5...` ✅
- `AE_CAM_09 motion blur` produce `hash(frame0) != hash(frame30)` ✅
- `./build-fast.sh scene-test '*AE_CAM*'` → 35/35 PASS, 0 FAIL
- MESSAGE workaround rimossi da `ae_parity_tests.cpp` per CAM_02/04
- Gate check 3/3 PASS

## File rilevanti

| File | Ruolo |
|---|---|
| `src/render_graph/executor/graph_executor.cpp` | Esecuzione nodi in ordine topologico |
| `src/render_graph/builder/graph_builder_layer_pipeline.cpp` | Costruzione pipeline layer |
| `src/render_graph/builder/passes/graph_builder_source_pass.cpp` | Creazione SourceNode per layer |
| `include/chronon3d/render_graph/render_graph.hpp` | `RenderGraph::m_nodes` (ordine corretto) |
| `src/render_graph/nodes/source_node.cpp` | `SourceNode::execute()` — draw_node chiamata |
| `tests/visual/ae_parity/ae_parity_scenes.cpp` | Scene AE_CAM_02/04 |
| `tests/visual/ae_parity/ae_parity_tests.cpp` | MESSAGE workaround da rimuovere |

## Collegamenti

- Ticket padre: [`TICKET-121`](./TICKET-121.md) — diagnostica completa (CLOSED)
- Ticket correlato: [`TICKET-ae-cam-hash-collision`](./TICKET-ae-cam-hash-collision.md) — cache layer investigation
- Commit diagnostica: `d748b38c` (FASE 7 render-order confirmation)
