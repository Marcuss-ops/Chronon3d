# ADR-034 — Headless geo / historical map subsystem

**Stato:** MIGRATO — il dominio geo appartiene a `ChrononPlugin/plugins/maps` (2026-10-26)
**Data:** 2026-10-08

## Contesto

La proposta di prodotto richiede cartografia storica: import GeoJSON, query
sulle feature, timeline storica a snapshot, unione/differenza di poligoni,
fill texture su forme, ancoraggio georeferenziato e animazione dei confini.
Il dominio è mantenuto in ChrononPlugin; `docs/ROADMAP.md` congela nuovi
effetti/sistemi fino alla certificazione combinata. L'estrazione dal core è
registrata nel ticket associato.

## Decisione

1. Il dominio geo vive in `ChrononPlugin/plugins/maps`, fuori dal runtime
   Chronon3D. Le feature passano per un solo `GeoFeatureCollection` immutabile
   costruito dal parser.
2. Il parser GeoJSON usa `nlohmann-json`, già dipendenza del progetto
   (`vcpkg.json`, `src/text/timed_text_adapter_json.cpp`). Nessuna nuova
   dipendenza di terze parti è introdotta da questo ADR.
3. Le operazioni booleane di poligono (Union/Difference/Intersection/XOR,
   Dissolve) usano **Clipper2** (Angus Johnson, Boost License), versione
   fissata `2.0.1`, come dipendenza del modulo cartografico ChrononPlugin. Scelta dell'utente (2026-10-08) dopo confronto con
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
- **Equivalence test/gate:** `ChrononPlugin/plugins/maps/tests/` (vedi ticket).
- **Removal scope:** implementazione provvisoria di clipping, se sostituita.
- **Status:** `REMOVED` dal core Chronon3D; le capacità geografiche rimanenti
  sono mantenute dal plugin mappe.

## Conseguenze

- Nessun cambio al render path né ai gate di certificazione esistenti.
- Chronon3D non espone più un target o header geo; vedere il ticket per la
  ricognizione del trasferimento.
