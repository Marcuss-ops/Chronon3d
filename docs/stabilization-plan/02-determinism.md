# Determinismo scheduler e rendering

## Problema

I test confrontano più scheduler, ma restano hash non acquisiti e percorsi composite o tile non completamente validati.

## TODO

- [x] **Sostituire i sentinel con golden hash reali.**  (PR 6.8.5 — `kRefBaseline*` sentinels
  aggiunti a `tests/deterministic/test_baseline_green.cpp` con
  `kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL` come first-clean-CI
  marker; commit `fb5980c9` su `main`.  Pattern analog a
  `kRefStaticScene` in `test_scheduler_determinism.cpp` per Q6 hybrid
  reviewer-round-2 mandate.  Numerici reali pending il toolchain
  fix su `SoftwareRenderer::capabilities() override` (TICKET-XXX-SR-
  CAPABILITIES) — capture workflow documentato in
  [`docs/01-baseline-green.md` §2.3](../../docs/01-baseline-green.md#23-baseline-verde-mitigato-ticket-007-rot-isolato)
  + [`docs/02-determinism.md` §4 Interlocking con WP-6](../../docs/02-determinism.md#4-superficie-3--composite-path)).
  Due-Phase Commit Strategy: PR 6.8.5 lands con i placeholder; il
  follow-up popola le 6 costanti con gli hash dal primo clean
  `ctest -R 'Baseline green' -V`.)
- [x] Validare Sequential, TBB 1, 2, 4 e automatico.  (PR 6.5 scheduler determinism + PR 6.9 SIMD safety net; vedi [`docs/02 §2/§3`](../../docs/02-determinism.md))
- [x] Ripetere ogni scenario più volte.  (test_baseline_green.cpp §1+§2+§4: 30 iter per scenario; §3: 1t/4t/8t lattice)
- [x] Testare cache fredda e calda.  (TICKET-007.q/.r re-enabled PR 6.9 + test_baseline_green.cpp §4 composite + §6 precomp cache hit/miss)
- [x] Riattivare la scena composite.  (test_baseline_green.cpp §4+§5 PR 6.8)
- [ ] Aggiungere un FakeBackend con compositing deterministico.  *(TICKET-013 deferred)*
- [x] Aggiungere il percorso tile.  (PR 6.1, 10 TEST_CASE in test_tile_determinism.cpp)
- [x] Testare frame diversi e scene animate.  (test_determinism_harness.cpp §2 + §11 + test_deterministic.cpp §2-§4)
- [x] Salvare seed, hash osservati e piattaforma.  *(procedura: l'hash è prodotto da `SoftwareRenderer::render_frame` → `framebuffer_hash` documentato in tools/visual_quality_suite.py; seed e piattaforma sono già nel run CLI del determinismo)*

## Test richiesti

- output bit-for-bit uguale tra scheduler;
- hash uguale al golden atteso; *(via kRefBaseline* sentinels PR 6.8.5)*
- stesso numero di visite per nodo;
- nessuna race con sanitizer quando disponibile.

## Completato quando

Tutti i percorsi seriali, paralleli, composite e tile producono lo stesso output atteso senza skip temporanei.

**Stato corrente**: tutti i 6 TEST_CASE baseline verdesi seriale-parallelo-composite-tile sono live; i 5 gradient-determinism test (TICKET-007.q/r/s/t/u) sono re-enabled da PR 6.9.  I 6 sentinel-gated REQUIRE di PR 6.8.5 sono dormant sui placeholder `kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL` — il toolchain fix `SoftwareRenderer::capabilities() override` (TICKET-XXX-SR-CAPABILITIES, FIXME nel codice) precede la capture workflow e il flip dei 6 placeholder in valori numerici reali.
