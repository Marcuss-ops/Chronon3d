# TICKET-GEO-HISTORICAL-MAP — Cartografia storica headless

**Stato:** MIGRATO FUORI DAL CORE — scope funzionale residuo ancora parziale
**ADR:** [ADR-034](../adr/ADR-034-geo-historical-maps.md)
**Created:** 2026-10-08

**Migrazione 2026-10-25:** i componenti headless sono ora posseduti da
`ChrononPlugin/plugins/maps/`; non esiste più il target `chronon3d_geo` né
l'opzione `CHRONON3D_ENABLE_GEO`. `maps_geo_core_test` compila ed esegue i test
del parser, query, timeline, booleani e cambi di territorio. L'API di camera,
anchor e reveal resta fuori dal perimetro implementato.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] **Voci differite 5/6/9/10** (`GeoAnchor`, `GeoCamera`, `GeoBorderReveal`, `GeoLabel` solver/`GeoAssetPreflight`) — logica: decidere se implementarle o dichiararle fuori perimetro in modo esplicito; oggi sono `NOT RUN` senza decisione. Dove: implementazione ora in `ChrononPlugin/plugins/maps/` (fuori dal core Chronon3D). Gate: per ogni voce, PASS/FAIL documentato oppure esclusione motivata nella tabella di accettazione.
- [ ] **`ShapeTextureFill` pixel per fit mode** — logica: il lowering è validato (4 doctest `chronon3d_scene_tests`) ma il pixel output per Cover/Contain/Stretch non è mai stato renderizzato. Dove: `shape_texture_fill` nei scene builders + test pixel in `tests/scene/`. Gate: golden pixel per ciascun fit mode.
- [ ] **Query composte + render `TerritoryChange`** — logica: il modulo mappe copre query semplici; mancano le query composte e il render dello stato shared/gained/lost. Dove: `ChrononPlugin/plugins/maps/` (parser/query/timeline) + pipeline di render dedicata. Gate: test query composte + golden render TerritoryChange.
- [ ] **Workaround Clipper2 portabile** — logica: il workaround `-Wno-error=maybe-uninitialized` vive in overlay fuori repo; va portato in repo (overlay versionato) o risolto con Clipper2 più recente. Dove: overlay port vcpkg nel repo. Gate: build `CHRONON3D_ENABLE_GEO` equivalente senza file esterni.

## Acceptance criteria (per voce)

Stato usando `PASS` / `FAIL` / `PARTIAL` / `NOT RUN`.

| # | Voce | Stato | Note |
|---|---|---|---|
| 1 | GeoJSON importer (Polygon, MultiPolygon, proprietà) | PARTIAL | Trasferito a `ChrononPlugin/plugins/maps`; test Plugin conservati |
| 2 | Query per proprietà sulla collezione | PARTIAL | Implementazione in `ChrononPlugin/plugins/maps`; niente query composte |
| 3 | HistoricalGeoTimeline a snapshot (nearest, source/confidence/date range) | PARTIAL | Implementazione in `ChrononPlugin/plugins/maps` |
| 4 | GeoBoolean / GeoDissolve (Union, Difference, Intersection, XOR) | PARTIAL | Implementazione in `ChrononPlugin/plugins/maps`, basata su Clipper2 |
| 5 | GeoAnchor (ScreenSpace / ShapeSpace / GeoSpace) | NOT RUN | differita |
| 6 | GeoCamera | NOT RUN | differita |
| 7 | ShapeTextureFill (fit Cover/Contain/Stretch) | PARTIAL | `shape_texture_fill` (scene builders): image node + alpha track matte to shape layer, validated; 4 doctest cases PASS in `chronon3d_scene_tests`. Pixel output NOT RUN; fit mode is passed through `FitMode`, not tested per mode |
| 8 | TerritoryChange (shared / gained / lost) | PARTIAL | Implementazione in `ChrononPlugin/plugins/maps`; niente render |
| 9 | GeoBorderReveal (recipe Trim + stroke + glow) | NOT RUN | differita |
| 10 | GeoLabel solver, GeoAssetPreflight | NOT RUN | differita |

## Esclusioni esplicite

- Correzioni manuali dei vertici: `GeoCorrectionPatch` deterministico è
  previsto dalla proposta ma non in questo ticket.
- Interpolazione geometrica tra snapshot: non dichiarata.
- Nessuna modifica a `CURRENT_STATUS.md` / `ROADMAP.md` (fix di feature
  non canoniche).

## Estrazione dal core (2026-10-26)

Rimosso da Chronon3D il target opzionale `chronon3d_geo`, i suoi header e i
relativi test. ChrononPlugin contiene l'implementazione mantenuta. Il
trasferimento non chiude le voci funzionali ancora `NOT RUN` e non certifica
la parità fra le due implementazioni precedenti.

## Verifica storica (prima dell'estrazione; non descrive il build attuale)

- Build: `CHRONON3D_ENABLE_GEO=ON` configura e compila `chronon3d_geo` (OBJECT) con vcpkg (`x64-linux`, `clipper2` via overlay port con `-Wno-error=maybe-uninitialized`, vedi nota sotto). **PASS**.
- Test: `ctest -R chronon3d_geo_core_test` (target `chronon3d_geo_core_test`, `tests/geo/geo_core_test.cpp`). **PASS**.
- Default (`CHRONON3D_ENABLE_GEO=OFF`): configure **PASS**, nessun target geo.
- Nota dipendenza: il port vcpkg `clipper2 2.0.1` fallisce con GCC per un warning `maybe-uninitialized` in `clipper.core.h:571` promosso a errore dal `-Werror` del port. Workaround locale in overlay (`/tmp/overlay-ports`, non nel repo): `-DCMAKE_CXX_FLAGS=-Wno-error=maybe-uninitialized`. Da portare nel repo come overlay o da risolvere con una versione Clipper2 successiva.
- Non verificato: voci 5-7, 9, 10 (render path) e pixel/parità visiva.
