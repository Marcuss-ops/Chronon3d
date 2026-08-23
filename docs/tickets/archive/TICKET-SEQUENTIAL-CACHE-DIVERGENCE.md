# TICKET-SEQUENTIAL-CACHE-DIVERGENCE — Sequential graph-cache parity divergence

## Stato: DONE per il percorso diagnostics-OFF verificato (2026-08-08, `main@377995d4`)

## Problema

Il ticket tracciava una divergenza tra rendering sequenziale con runtime
condiviso e rendering dello stesso frame con runtime freddo quando una
sorgente dinamica entra nello schermo. La regressione è ora chiusa nel percorso verificato: sul build
`linux-fast-dev` con `CHRONON3D_BUILD_DIAGNOSTICS=OFF` e con
`settings.diagnostics.enabled = false`, il verifier passa senza mismatch.
Il build mantiene separata l'opzione `CHRONON3D_ENABLE_DIAGNOSTICS=ON`; la
chiusura qui documentata riguarda esclusivamente il flag di build
`CHRONON3D_BUILD_DIAGNOSTICS` e l'impostazione runtime esplicita.

## Evidenza

- Verifier: `tests/deterministic/test_sequential_graph_cache.cpp` (aggiunto in
  `35c062a9 test(cache): add sequential graph cache verifier`), ordini
  linear/random/reverse/repeated, frame 0–59, hash XXH64 via
  `test::framebuffer_hash`.
- Evidenza storica: il percorso diagnostics ON passava mentre il percorso OFF
  falliva al frame 24; questa era la condizione che il ticket doveva risolvere.
- Evidenza corrente su `main@377995d4`: `bash tools/verify_sequential_graph_cache_linux.sh`
  ha ricostruito `chronon3d_deterministic_tests` e `chronon3d_scene_tests`,
  verificato la freschezza rispetto a test, CMake e sorgenti runtime, quindi
  ha eseguito il verifier con diagnostics runtime OFF.
- Risultato: `chronon3d_deterministic_tests` — 3/3 test case PASS,
  3011 assertions PASS, 0 failure; `chronon3d_scene_tests` — 14/14 test case
  PASS, 117 assertions PASS, 0 failure; marker
  `CHRONON_SEQUENTIAL_GRAPH_CACHE_PASS`, exit code 0.

## Ipotesi di causa (storica, superata dalla verifica)

1. La sorgente animata era autored con valori per-frame in C++ grezzo
   (`.pos = {x, y}` calcolato da `ctx.frame()`) senza oggetti animator:
   l’analisi statica la classificava `static`, quindi la cache nodo condivisa
   riusava il risultato di un frame precedente (vuoto/off-screen) invece di
   rieseguire — mentre un runtime freddo eseguiva sempre.
2. La semantica `bbox.clip_to` gated su `diagnostics_enabled` era una
   dipendenza fragile: una flag di logging non dovrebbe cambiare il pixel
   output.

## Criterio di chiusura

Chiuso per il percorso verificato: il verifier passa con
`CHRONON3D_BUILD_DIAGNOSTICS=OFF` e `settings.diagnostics.enabled = false`
su tutti gli ordini (lineare, random, reverse e repeated), sui frame 0–59,
includendo il contratto di rendering cold/warm e il controllo
transactional/topology della suite `chronon3d_scene_tests`. Questa evidenza
non estende la conclusione ad altre configurazioni diagnostiche.

## Aggiornamenti

- 2026-08-04: rimosso il workaround `opacity = 0.001` dalle composizioni
  light transition (`content/launches/light_transition_sound_smoke.cpp`),
  ripristinato `opacity = 0.0` reale; topologia dei layer invariata.
- 2026-08-08: verifica finale diagnostics-OFF su `main@377995d4` completata
  con `CHRONON_SEQUENTIAL_GRAPH_CACHE_PASS`; ticket chiuso.
