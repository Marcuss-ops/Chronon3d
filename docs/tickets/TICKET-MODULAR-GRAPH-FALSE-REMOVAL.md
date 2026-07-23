# TICKET-MODULAR-GRAPH-FALSE-REMOVAL — Remove or migrate legacy `use_modular_graph = false` path

## Stato

OPEN (P2)

## Priorità

P2 (build / test rot)

## Problema

Il percorso di rendering `use_modular_graph = false` (coordinate top-left legacy) non è più mantenuto. Tutti i test helper di produzione e la maggior parte del codice CLI impostano il valore di default `true`, e le recenti modifiche al sistema di coordinate (centraggio canvas, `graph_builder_coordinates.hpp`, `graph_builder_internal.hpp`, TICKET-104) sono state progettate attorno al percorso modulare.

I test che esplicitamente disabilitano il percorso modulare falliscono perché i layer vengono renderizzati fuori viewport o i bounding box risultano vuoti.

## Evidenza

Test falliti (6 casi in `chronon3d_render_graph_tests`):

- `tests/render_graph/features/test_unified_transform_path.cpp`
  - `Unified FakeBox3D: front face matches Rect in shared composition`
    - `REQUIRE(bbox.has_value())` fallisce; `bright_bbox()` non trova pixel luminosi.

- `tests/render_graph/pipeline/test_pipeline_robustness.cpp`
  - `Coordinate Centered vs Top Left - Centered exactly on canvas`
  - `Coordinate Centered vs Top Left - Transform matrix offset`
  - `Coordinate Centered vs Top Left - Layer near border should not disappear`
  - `Coordinate Centered vs Top Left - Render graph mixed 2D and centered`
  - Tutti falliscono perché i pixel attesi `r`/`a` > 0.9 sono invece `0`.

Codice chiave:

- `include/chronon3d/backends/software/render_settings.hpp:123` — `bool use_modular_graph{true};`
- `tests/helpers/test_utils.hpp:177-197` — commento esplicito che il parametro legacy è ora no-op e il percorso modulare è l'unico invariante preservato.
- `apps/chronon3d_cli/commands/render/register_render_commands.cpp:199` — CLI `--graph / --no-graph` ancora esposto.
- `src/render_graph/pipeline/helpers.hpp:141` — `modular_coordinates = settings.use_modular_graph`.

## Impatto

- Falsi fallimenti nella suite `render_graph` che mascherano lo stato reale del modulo.
- Doppio percorso di coordinate che aumenta la complessità senza essere mantenuto.
- Rischio che utenti CLI usino `--no-graph` ottenendo risultati non deterministici o fuori viewport.

## Confine

- Non si richiede di risolvere le logiche di centraggio legacy; si richiede di decidere se il percorso `false` deve ancora esistere.
- Se si decide di rimuoverlo: aggiornare `RenderSettings`, il CLI, i test, e rimuovere il codice morto nei builder.
- Se si decide di mantenerlo: migrare i test falliti al percorso modulare e dichiarare esplicitamente che `false` è deprecato/solo legacy.

## Soluzione accettabile

Uno dei due approcci:

1. **Rimozione completa**:
   - Rimuovere il campo `use_modular_graph` da `RenderSettings` e da tutti i punti d'uso.
   - Rimuovere il flag `--no-graph` dai comandi CLI `render` e `bench`.
   - Rimuovere o aggiornare i test che impostano `use_modular_graph = false`.
   - Eliminare il ramo non-modulare in `src/render_graph/builder/` se ancora presente.

2. **Deprecazione formale + migrazione test**:
   - Aggiungere un commento/attributo di deprecazione a `use_modular_graph`.
   - Rimuovere il flag `--no-graph` dal CLI o farlo loggare un warning.
   - Migrare i 6 test falliti a `use_modular_graph = true` o marcarli come `EXPECT_FAIL` se devono continuare a dimostrare che il percorso legacy è rotto.
   - Documentare il percorso come non mantenuto in `CURRENT_STATUS.md` e in un ADR se necessario.

## Criteri di accettazione

- Nessun test della suite `render_graph` fallisce a causa del percorso `use_modular_graph = false`.
- Il campo `use_modular_graph` è rimosso oppure esplicitamente deprecato/documentato come non mantenuto.
- I comandi CLI non offrono più `--no-graph` oppure lo stampano come deprecato/warning.
- I test migrati passano con il percorso modulare.
- `docs/FOLLOWUP_TICKETS.md` e `docs/CURRENT_STATUS.md` aggiornati allo stato chiuso.

## Collegamenti

- `docs/FOLLOWUP_TICKETS.md` — indice blocker
- `docs/CURRENT_STATUS.md` — stato corrente
- `include/chronon3d/backends/software/render_settings.hpp`
- `tests/render_graph/features/test_unified_transform_path.cpp`
- `tests/render_graph/pipeline/test_pipeline_robustness.cpp`
- `apps/chronon3d_cli/commands/render/register_render_commands.cpp`
- `apps/chronon3d_cli/commands/bench/register_bench_commands.cpp`
