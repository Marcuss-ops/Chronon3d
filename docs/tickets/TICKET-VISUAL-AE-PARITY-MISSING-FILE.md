# TICKET-VISUAL-AE-PARITY-MISSING-FILE — Recreate missing AE parity test sources

## Stato: CLOSED (2026-07-13, rot-cascade resolved upstream at commit `4791e98b` — `tests/visual_tests.cmake` at upstream KEEPS the `chronon3d_ae_parity_tests` suite wiring intact with `visual/ae_parity/ae_parity_tests.cpp` + `ae_cam_scenes.cpp` + `ae_glow_position_drift.cpp`; local remotion-rot repair replaced by upstream preservation)

## Problema

L'intera directory `visual/ae_parity/` risulta assente dall'albero dei sorgenti
(post content-dedup).  I tre file referenziati da
`tests/visual_tests.cmake::chronon3d_ae_parity_tests::SOURCES` sono tutti
mancanti:

- `visual/ae_parity/ae_parity_tests.cpp` — MISSING
- `visual/ae_parity/ae_parity_scenes.cpp` — MISSING
- `visual/ae_parity/ae_glow_position_drift.cpp` — MISSING

Ciò blocca ogni `cmake -S . -B <build-dir>` che include
`tests/visual_tests.cmake`, indipendentemente dal preset (la verifica
del blocco è a livello di CMake configure-time).

## Decisione di riparazione (Azione 18)

Per sbloccare la build pipeline, l'intero blocco
`chronon3d_ae_parity_tests` + il ctest alias `Ae08GlowPositionDrift`
sono stati rimossi da `tests/visual_tests.cmake`.  Il rot è documentato
nel comment block sostitutivo (vedi `tests/visual_tests.cmake` intorno
alla riga ~123) e in questo ticket.

## Criteri di accettazione per chiusura

1. Individuare in git history il commit in cui `visual/ae_parity/` è
   stato rimosso (git log --diff-filter=D) per valutare se i file
   sono recuperabili o se vanno riscritti.
2. Ricreare i 3 file sorgenti o ripristinarli dal commit di rimozione.
3. Ripristinare il blocco `chronon3d_add_test_suite(NAME
   chronon3d_ae_parity_tests ...)` in `tests/visual_tests.cmake` con
   i 3 file nella lista SOURCES.
4. Ripristinare il `add_test(NAME Ae08GlowPositionDrift ...)` ctest
   alias.
5. Verificare che `cmake --preset linux-fast-dev -B
   build/manual-test -DCHRONON3D_BUILD_TESTS=ON -DCHRONON3D_BUILD_CONTENT=ON`
   completi la configure senza errori.
6. Verificare che `ctest -R "Ae08"`
   ritorni i 6 TEST_CASEs della Fase 3 SCALA come in origine.

## Forward-points

- `tests/visual_tests.cmake` dovrebbe guadagnare un defensive
  comment che cita questo ticket per chiunque modifichi
  `tests/visual_tests.cmake` dopo la ricreazione.
- Se la ricreazione è irrecuperabile (commit perso), il suite va
  riscritto da zero seguendo la specifica Fase 3 SCALA.

## Origine

Ticket aperto durante Azione 18 (regression lock per il silent
failure di `content/animation_compositions.cpp::anim_typewriter`).
L'analisi iniziale attribuiva la colpa a un singolo file mancante,
ma un'ispezione `find visual/ae_parity -type f` del basher ha
rivelato che l'intera directory è assente — escalation del rot.
