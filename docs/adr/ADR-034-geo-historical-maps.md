# ADR-034 — Headless geo / historical map subsystem

**Stato:** PROPOSED (implementazione parziale in `TICKET-GEO-HISTORICAL-MAP`)
**Data:** 2026-10-08

## Contesto

La proposta di prodotto richiede cartografia storica: import GeoJSON, query
sulle feature, timeline storica a snapshot, unione/differenza di poligoni,
fill texture su forme, ancoraggio georeferenziato e animazione dei confini.
Nel repository non esiste alcun codice geo. `docs/ROADMAP.md` congela nuovi
effetti/sistemi fino alla certificazione combinata; l'utente ha scelto
esplicitamente di procedere comunque con l'intero sottosistema.

## Decisione

1. Il dominio geo vive in `include/chronon3d/geo/` e `src/geo/`, headless,
   senza GUI/GPU. Non introduce registry paralleli: le feature passano per
   un solo `GeoFeatureCollection` immutabile costruito dal parser.
2. Il parser GeoJSON usa `nlohmann-json`, già dipendenza del progetto
   (`vcpkg.json`, `src/text/timed_text_adapter_json.cpp`). Nessuna nuova
   dipendenza di terze parti è introdotta da questo ADR.
3. Le operazioni booleane di poligono (Union/Difference/Intersection/XOR,
   Dissolve) usano **Clipper2** (Angus Johnson, Boost License), versione
   fissata `2.0.1`, come dipendenza scoped alla feature vcpkg `geo`
   (`vcpkg.json`). Scelta dell'utente (2026-10-08) dopo confronto con
   implementazione interna. Coordinate quantizzate a 1e-6 gradi prima del
   clipping: risultati deterministici, non esatti al floating-point.
4. La timeline storica seleziona lo snapshot più vicino all'anno richiesto
   e conserva `source`, `confidence` e intervallo di date per ogni snapshot.
   Nessuna interpolazione geometrica è dichiarata in questo ADR.
5. Animazione del confine, texture su forma e ancoraggio georeferenziato
   sono recipe sopra le primitive esistenti (Trim, stroke, glow, track
   matte) e sono fuori dallo scope di questo primo passo.

## Demolition Debt

- **Owner:** repo owner + agente della sessione geo.
- **Reason:** il clipping poligonale interno è provvisorio finché non esiste
  una scelta definitiva sulla dipendenza.
- **Exit condition:** ADR successivo sceglie l'algoritmo di clipping e
  supera un test di parità su poligoni con buchi e MultiPolygon.
- **Equivalence test/gate:** `tests/geo/` (vedi ticket).
- **Removal scope:** implementazione provvisoria di clipping, se sostituita.
- **Status:** `ACTIVE`

## Conseguenze

- Nessun cambio al render path né ai gate di certificazione esistenti.
- Il sottosistema è opt-in: non modifica `ROADMAP.md` né `CURRENT_STATUS.md`.
