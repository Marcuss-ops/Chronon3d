# TICKET-TRN-03 — Layer transition cleanup and certification

## Stato

DONE (2026-07-24)

## Obiettivi

- Supportare `transition_in` e `transition_out` contemporaneamente sullo stesso layer.
- Completare la cache key di `TransitionNode` con durata, delay, easing, direzione e tutti i parametri tipizzati.
- Rendere fail-loud gli ID di transizione sconosciuti.
- Parametrizzare i valori precedentemente hard-coded (feather, centro, colore, seed, speed, direction, angle).

## Cosa è stato verificato

Il codice sorgente in `main` già soddisfa i requisiti funzionali:

- `graph_builder_layer_pipeline.cpp` aggiunge entrambi i nodi `TransitionNode` (in e out) quando presenti, senza preferire l'uno all'altro.
- `hash_layer_transition_spec` include `transition_id`, `direction`, `duration`, `delay`, `easing` e `parameters`.
- `LayerTransitionCatalog::resolve` lancia `std::runtime_error` per ID sconosciuti.
- I parametri tipizzati (`SlideParams`, `SmoothWipeParams`, `CircleIrisParams`, `FlashParams`, `ProceduralRemotionParams`, `RemotionParams`) sono già definiti e usati dal catalogo.

Questo ticket certifica i requisiti aggiungendo copertura test in `tests/render_graph/features/test_transition.cpp`.

## Copertura test aggiunta

- In e out coexist on the same layer (già esistente, confermato).
- Cache key includes duration, delay, easing and direction.
- Cache key includes typed parameters per tutte le transizioni parametriche.
- TransitionNode in/out split cache key (verifica che `m_is_out` influenchi la chiave).
- Identical transition specs produce identical cache keys.
- Unknown transition id fails loudly (render + catalog).
- LayerTransitionCatalog rejects unknown ids.
- Typed parameters affect transition output (smooth_wipe).

## Commit di chiusura

Vedi commit che include questo file per il riferimento esatto.

## Forward points

- Estendere "Typed parameters affect transition output" anche a `circle_iris`, `flash`, `procedural_remotion`, `remotion` oltre a `smooth_wipe` (nice-to-have, golden-level).
- Integrare il gate `tools/check_transition_id_dispatch.sh` nella pipeline di pre-push se non già fatto.
