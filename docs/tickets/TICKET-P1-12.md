# TICKET-P1-12 — CMake e packaging eccessivamente delicati

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | cmake / build |
| **Stato** | ✅ DONE (`59b2439f`) |
| **Blocca** | — |
| **Feature Freeze** | ⚠️ Parzialmente consentito (build-fix) |

## Bug (risolto)

L'SDK usava merge manuale `ar crs` tramite `cmake/sdk_archive_merge.cmake` (workaround CMake 3.25), OBJECT target, `include_private` esposti via `INTERFACE`, manifest e registry separati.

## Azioni completate

- [x] `cmake_minimum_required` già a 3.27 nel root (bump consumer test a 3.27)
- [x] Eliminato merge manuale `ar`: cancellato `cmake/sdk_archive_merge.cmake`, ridotto `Chronon3DSdkArchive.cmake` a solo canary guard
- [x] Rimosso `INTERFACE include_private` da `src/cache/CMakeLists.txt` e `src/runtime/CMakeLists.txt` (header già promossi a public)
- [x] Aggiornato `tools/sdk/check_feature_ghosts.sh`: target `sdk_archive_merge` → `chronon3d_sdk_impl`
- [x] Pulizia riferimenti obsoleti in commenti
- [ ] Public manifest generato dal registry (deferred)

## File modificati

10 file: `sdk_archive_merge.cmake` (deleted), `Chronon3DSdkArchive.cmake`, `Chronon3DSdkTargets.cmake`, `Chronon3DCanarySymbols.cmake`, `src/cache/CMakeLists.txt`, `src/runtime/CMakeLists.txt`, `src/CMakeLists.txt`, `check_feature_ghosts.sh`, consumer test CMakeLists
