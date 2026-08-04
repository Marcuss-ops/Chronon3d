# TICKET-SEQUENTIAL-CACHE-DIVERGENCE — Sequential graph-cache parity divergence

## Stato: OPEN (2026-08-04)

## Problema

Rendering di frame consecutivi con un unico runtime condiviso non è
byte-identico al rendering dello stesso frame con un runtime nuovo (freddo)
quando una sorgente dinamica entra nello schermo. Con la configurazione di
default (diagnostics **OFF**) il verifier
`tests/deterministic/test_sequential_graph_cache.cpp` fallisce: 108/2700
assertion, primo mismatch al **frame 24** dell'ordine lineare — il runtime
condiviso non disegna il rettangolo in movimento che entra dal bordo
sinistro, mentre il runtime indipendente lo disegna correttamente.

## Evidenza

- Verifier: `tests/deterministic/test_sequential_graph_cache.cpp` (aggiunto in
  `35c062a9 test(cache): add sequential graph cache verifier`), ordini
  linear/random/reverse/repeated, frame 0–59, hash XXH64 via
  `test::framebuffer_hash`.
- Con `settings.diagnostics.enabled = true` (pattern canonico del repo, vedi
  `tests/content/test_light_transition_sequential_cache.cpp`) il verifier
  passa 2700/2700. La flag cambia la semantica del bbox in
  `SourceNode::predicted_bbox` / `MultiSourceNode::predicted_bbox`:
  `bbox.clip_to(frame)` viene applicato **solo** quando la diagnostics è
  disabilitata.
- Con diagnostics OFF la sorgente `moving_source` esegue (cache node miss,
  `frame_dep=0`) e disegna gli stessi pixel in entrambi i runtime; la
  differenza emerge a valle nel composito, e solo dal frame in cui l'oggetto
  entra nello schermo (per frame 0–23 l'output è background-only in entrambi).

## Ipotesi di causa (non ancora verificata)

1. La sorgente animata è autored con valori per-frame in C++ grezzo
   (`.pos = {x, y}` calcolato da `ctx.frame()`) senza oggetti animator:
   l'analisi statica la classifica `static`, quindi la cache nodo condivisa
   riusa il risultato di un frame precedente (vuoto/off-screen) invece di
   rieseguire — mentre un runtime freddo esegue sempre.
2. La semantica `bbox.clip_to` gated su `diagnostics_enabled` è una
   dipendenza fragile: una flag di logging non dovrebbe cambiare il pixel
   output.

## Prossimi passi (tranche fix cache)

- Riprodurre con diagnostics OFF come test rosso canonico (commit 1 del piano).
- Correggere la classificazione static/dynamic (o l'authoring della fixture
  con animator) e la semantica bbox diagnostics-gated (commit 2).
- Rimuovere ogni workaround; `opacity = 0.001` incluso se ancora presente.
- Criterio di accettazione: verifier verde con diagnostics OFF su tutti e
  quattro gli ordini, frame 0–59.
