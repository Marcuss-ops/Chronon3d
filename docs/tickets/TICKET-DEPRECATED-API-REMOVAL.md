# TICKET-DEPRECATED-API-REMOVAL — Remaining deprecated public APIs

## Stato: OPEN (P1)

Il ticket storico è stato ristretto il 2026-08-01. Le migrazioni già
atterrate non sono più blocker: `CompositionRegistry::add(name, factory)`,
la mappa `factories_`, gli alias temporali di `LayerBuilder`, gli alias delle
transizioni camera, il percorso `CompositionSpec::assets_root` e il
costruttore deprecato di `ShapedGlyphLine` non esistono più sul `main` attuale.
Anche la soppressione globale `-Wno-error=deprecated-declarations` è assente.

Restano soltanto deprecazioni verificabili nel codice, da gestire per area:

- `SoftwareRenderer(Config)` come percorso di costruzione legacy;
- `GraphExecutor::execute(CompiledFrameGraph&)` senza `ExecutionScope`;
- i preset testuali con dimensione canvas implicita;
- `text::resolve_fallback_fonts(...)` come free-function adapter;
- `authoring::{Scene,Layer}::context()` e `Layer::configure_core(...)`;
- `SceneBuilder`/`Layer::local_frame(...)` e gli altri adapter esplicitamente
  marcati nei rispettivi header.

## Criteri residui

- audit dei chiamanti per ciascun simbolo ancora presente;
- migrazione o rimozione per area, senza riaprire le API già eliminate;
- promozione a `-Werror=deprecated-declarations` per i target SDK quando
  l'area è a zero chiamanti produttivi;
- baseline completa solo dopo che i target modificati sono stati ricostruiti.

## Evidenza di chiusura parziale

L'assenza dei simboli legacy di composition, timing, camera e glyph shaping è
verificabile con `rg` sui path `include/`, `src/`, `content/`, `apps/` e
`tests/`. I dettagli storici dei singoli refactor restano nelle schede
specifiche e non fanno più parte dell'inventario operativo di questo ticket.
