# TICKET-120 — `chronon3d_scene_tests` pre-existing runtime rot surfaced by C9

**Status:** PARTIAL
**Priority:** P1
**Area:** Camera / scene test suite
**Blocks:** certificazione Camera Production V1 end-to-end
**Introduced by:** C9 (`docs/FOLLOWUP_TICKETS.md` registration)

## Sintesi

Il certifier runtime C9 di `tests/scene/camera/golden_projection_test.cpp`
ha sbloccato la build del target `chronon3d_scene_tests` (in
precedenza bloccata da ODR TU-locali pre-esistenti in `chronon3d_text_core`,
risolte via `SKIP_UNITY_BUILD_INCLUSION` su `timed_text_document.cpp` +
`boundary_resolver/text_unit_map.cpp`). Una volta ottenuto il binario
eseguibile, l'esecuzione completa del target ha rivelato **24 fallimenti
pre-esistenti** in test case non-camera, di cui due diagnosticabili con
precisione e gli altri 22 da triage individuale.

## Failure note (estratto tail di `./build/tests/chronon3d_scene_tests`)

```
[doctest] test cases:  239 |  215 passed | 24 failed | 162 skipped
[doctest] assertions: 3533 | 3508 passed | 25 failed |
[doctest] Status: FAILURE!
```

Failure diagnosticate dal tail:

1. **`TICKET-035: anamorphic_squeeze 2.0 produces focal_x == 2 * focal_y for anamorphic_50mm`**
   — `tests/scene/camera/test_camera_projection_contract.cpp:572`
   — `CHECK( fxy_spherical.x == doctest::Approx(fxy_spherical.y).epsilon(1e-4f) ) is NOT correct!`
   — `values: CHECK( 2666.67 == Approx( 2250 ) )`
   — **Stesso bug** che il reviewer di C7 aveva flaggato sul golden test
     (fatto salvo lì con ratio 1.506 × 2.0 = 3.011, NON 2.0). L'asset
     `test_camera_projection_contract.cpp` porta ancora l'asserzione
     sbagliata `focal_x == 2 * focal_y`. **Fix 1-line:** aggiornare la
     riga 572 con lo stesso `CHECK(fxy_spherical.x == doctest::Approx(3.011f * fxy_spherical.y).epsilon(0.01f))`
     usato in C7.

2. **`TICKET-034D: CameraDescriptor is fingerprint-serializable`**
   — `tests/scene/camera/test_composition_default_camera.cpp:69`
   — **FATAL ERROR: test case CRASHED: SIGABRT - Abort (abnormal termination) signal**
   — Triage necessario: stack trace + root cause + fix. Probabilmente un
     segfault su un dereference o un assert interno.

3-24. **22 fallimenti rimanenti** — solo summary count, no detail visibile
dal tail. Triage richiesto:
- rieseguire `./build/tests/chronon3d_scene_tests --reporters=console 2>&1 | head -200`
  per estrarre il primo failure-detail di ogni test case
- oppure `--no-skip --duration` per la lista completa
- raggruppare per area (`test_camera_*`, `test_projection_*`, ecc.)

## Acceptance criteria (Definition of Done)

- `tools/wrap_push.sh origin main` + esecuzione di
  `./build/tests/chronon3d_scene_tests` produce **0 fallimenti**
  (215 passed → 239 passed, 24 failed → 0 failed)
- I test `TICKET-034D` e `TICKET-035:anamorphic_squeeze` passano
  esplicitamente (non silenziosamente, non skipped)
- Aggiornamento `docs/CURRENT_STATUS.md` riga Camera Production V1:
  rimozione del riferimento a TICKET-120; PASS se il resto della
  subsystem è verde, altrimenti descrizione del residuo
- Aggiornamento `docs/CAMERA_FEATURE_MATRIX.md` sezione §8 + Valutazione
  complessiva: nessun "🟡 residuo Certificazione runtime" residuo
- Aggiornamento `docs/FOLLOWUP_TICKETS.md`: spostare TICKET-120 in
  "Recently closed" con il commit SHA di chiusura

## Vincoli architetturali

- `tools/wrap_push.sh origin main` prima di ogni push (GATE-MNT-01)
- `tools/check_architecture_boundaries.sh` deve restare 14/14 PASS dopo
  ogni commit (gli advisory TICKET-041/042 restano fuori scope)
- `tools/check_doc_sync.sh` deve restare 0 hard failures dopo ogni
  commit (gate #7)
- Nessun nuovo singleton/registry/resolver/cache/service-locator
  (regola permanente AGENTS.md)
- Feature freeze v0.1 attivo: i 22 fallimenti non-camera rientrano
  nella categoria "Correzioni di build" (se build rot) o "Test
  deterministici" (se regressioni assertion); vedi
  `AGENTS.md` §Feature Freeze per il perimetro consentito

## Origine / commit di scoperta

C9a (certifier runtime, questo commit) — `SKIP_UNITY_BUILD_INCLUSION`
su `chronon3d_text_core` + `using chronon3d::camera_math::FocalPx;` in
`tests/scene/camera/golden_projection_test.cpp` sbloccano la build del
target `chronon3d_scene_tests`. Senza C9a questi 24 fail sarebbero
rimasti latenti (la build `Unix Makefiles` precedente lasciava `build/`
in stato parziale). Il follow-up C9b aggiorna `docs/CURRENT_STATUS.md`
+ `docs/CAMERA_FEATURE_MATRIX.md` + `docs/FOLLOWUP_TICKETS.md` con
riferimento SHA concreto a C9a.
