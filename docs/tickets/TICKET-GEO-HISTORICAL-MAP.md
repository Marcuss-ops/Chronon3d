# TICKET-GEO-HISTORICAL-MAP — Cartografia storica headless

**Stato:** IN PROGRESS
**ADR:** [ADR-034](../adr/ADR-034-geo-historical-maps.md)
**Created:** 2026-10-08

## Acceptance criteria (per voce)

Stato usando `PASS` / `FAIL` / `PARTIAL` / `NOT RUN`.

| # | Voce | Stato | Note |
|---|---|---|---|
| 1 | GeoJSON importer (Polygon, MultiPolygon, proprietà) | PARTIAL | `src/geo/geojson.cpp`; test standalone PASS; CMake/ctest NOT RUN |
| 2 | Query per proprietà sulla collezione | PARTIAL | `query_by_property` (equality); test standalone PASS; niente query composte |
| 3 | HistoricalGeoTimeline a snapshot (nearest, source/confidence/date range) | PARTIAL | `src/geo/historical_timeline.cpp`; test standalone PASS |
| 4 | GeoBoolean / GeoDissolve (Union, Difference, Intersection, XOR) | PARTIAL | Clipper2 2.0.1; `src/geo/geo_boolean.cpp`; test standalone PASS; CMake/vcpkg NOT RUN |
| 5 | GeoAnchor (ScreenSpace / ShapeSpace / GeoSpace) | NOT RUN | differita |
| 6 | GeoCamera | NOT RUN | differita |
| 7 | ShapeTextureFill (fit Cover/Contain/Stretch) | PASS | CPU end-to-end pixels verify Cover/Contain/Stretch/None over a four-color source and preserve the shape alpha matte. Seven doctest cases pass (45 assertions) in current `chronon3d_scene_tests` |
| 8 | TerritoryChange (shared / gained / lost) | PARTIAL | `src/geo/territory_change.cpp`; test standalone PASS; niente render |
| 9 | GeoBorderReveal (recipe Trim + stroke + glow) | NOT RUN | differita |
| 10 | GeoLabel solver, GeoAssetPreflight | NOT RUN | differita |

## Esclusioni esplicite

- Correzioni manuali dei vertici: `GeoCorrectionPatch` deterministico è
  previsto dalla proposta ma non in questo ticket.
- Interpolazione geometrica tra snapshot: non dichiarata.
- Nessuna modifica a `CURRENT_STATUS.md` / `ROADMAP.md` (fix di feature
  non canoniche).

## Verifica

- Re-run CPU 2026-10-11: `chronon3d_scene_tests --test-case='ShapeTextureFill:*'`
  passa **7/7 casi e 45/45 asserzioni**. Il test di pixel usa una sorgente con
  quattro bande di colore per verificare crop `Cover`, letterbox `Contain`,
  deformazione `Stretch` e scala nativa centrata `None`; il caso separato
  verifica che il track matte nasconda i pixel esterni alla shape.

- Build: `CHRONON3D_ENABLE_GEO=ON` configura e compila `chronon3d_geo` (OBJECT) con vcpkg (`x64-linux`, `clipper2` via overlay port con `-Wno-error=maybe-uninitialized`, vedi nota sotto). **PASS**.
- Test: `ctest -R chronon3d_geo_core_test` (target `chronon3d_geo_core_test`, `tests/geo/geo_core_test.cpp`). **PASS**.
- Default (`CHRONON3D_ENABLE_GEO=OFF`): configure **PASS**, nessun target geo.
- Nota dipendenza: il port vcpkg `clipper2 2.0.1` fallisce con GCC per un warning `maybe-uninitialized` in `clipper.core.h:571` promosso a errore dal `-Werror` del port. Workaround locale in overlay (`/tmp/overlay-ports`, non nel repo): `-DCMAKE_CXX_FLAGS=-Wno-error=maybe-uninitialized`. Da portare nel repo come overlay o da risolvere con una versione Clipper2 successiva.
- Non verificato: voci 5-7, 9, 10 (render path) e pixel/parità visiva.
