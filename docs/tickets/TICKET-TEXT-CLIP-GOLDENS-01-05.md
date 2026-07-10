# TICKET-TEXT-CLIP-GOLDENS-01-05 — text clip bounds goldens (Clip 01–05)

## Stato
OPEN

## Priorità
P1

## Problema
I 5 `TEST_CASE` di `tests/text_golden/text_clip/text_clip_bounds.cpp` (Clip 01–05: `AscentNotCut`, `RightEdgeNotCut`, `Scale130NotCut`, e 2 successivi) hanno assertions numeriche su `predicted_bbox` (e.g. `bbox.height() > 90`, `bbox.width() > 500`, `bbox.x1 < width-10`) ma **non hanno golden PNG seedati** sotto il nuovo code path post-`TICKET-TEXT-CLIP-ASCENT` (chiuso 2026-07-08). Senza golden di riferimento, `verify_golden()` (safety net loose-thresholds) non può rilevare drift; inoltre il mancato seed impedisce la certificazione del cluster `text_golden/text_clip/` per il rilascio V0.1.

## Evidenza
- `tests/text_golden/text_clip/text_clip_bounds.cpp` lines 1–400 (5 `TEST_CASE` + helper `alpha_bbox`).
- `tests/text_golden_tests.cmake` line 136 (comment "Five TEST_CASEs in text_clip/text_clip_bounds.cpp").
- Nessun file `tests/text_golden/text_clip/goldens/clip_*_expected.png` (o equivalente) presente nel checkout corrente (verificato via `find tests/text_golden/text_clip/ -name '*.png'`).
- `docs/CHANGELOG.md` line 22 conferma che Clip 01/02/03 sono stati introdotti nel commit `TICKET-TEXT-CLIP-ASCENT` "as 3-test numerical bbox regression lock" — solo assertions numeriche, no golden.

## Impatto
- Impedisce la certificazione golden del cluster `text_golden/text_clip/` per V0.1 release.
- Nasconde regressioni visive non coperte dalle sole assertions numeriche (es. colore, anti-aliasing, hinting).
- Accoppiato a `TICKET-TEXT-CLIP-PREDICTED-BBOX`: il seed dei goldens va fatto DOPO il fix del predicted_bbox, altrimenti i goldens catturano il bug attuale.

## Confine
- Solo seed di 5 golden PNG + 1 golden per Clip 06 (opzionale, diagnostico).
- Escluso: assertions numeriche (già presenti e PASS con fix TICKET-TEXT-CLIP-ASCENT).
- Escluso: nuovi test (Clip 01–05 sono già nel file).

## Soluzione accettabile
1. Attendere la chiusura di `TICKET-TEXT-CLIP-PREDICTED-BBOX` (goldens devono catturare il bbox corretto, non quello buggato).
2. Eseguire `bash tools/regen_camera_golden.sh` (o analogo) sui 5 `TEST_CASE` Clip 01–05, generando `tests/text_golden/text_clip/goldens/clip_0{1..5}_expected.png`.
3. Verificare `chronon3d_text_golden_tests --test-suite=text_clip` → 5/5 PASS contro i nuovi goldens.
4. Aggiungere i 5 file PNG al tracking git + `tests/text_golden_tests.cmake::text_clip_golden_TEST_SRCS` (se previsto dal pattern esistente).

## Criteri di accettazione
- 5/5 `TEST_CASE` Clip 01–05 PASS contro goldens seedati.
- `verify_golden()` safety net abilitato e PASS (no fallback loose-thresholds-only).
- `tests/text_golden_tests.cmake` aggiornato con i 5 path attesi.
- Cross-link a `TICKET-TEXT-CLIP-PREDICTED-BBOX` nella sezione "Lineage".

## Linkage
- Test file: `tests/text_golden/text_clip/text_clip_bounds.cpp`.
- CMake: `tests/text_golden_tests.cmake` line 136.
- Tool di seed: `tools/regen_camera_golden.sh` (o equivalente per text_golden).
- Prerequisite: `TICKET-TEXT-CLIP-PREDICTED-BBOX` (DONE) — goldens devono catturare bbox corretto.
- Stato corrente: [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) §Open Blockers.
