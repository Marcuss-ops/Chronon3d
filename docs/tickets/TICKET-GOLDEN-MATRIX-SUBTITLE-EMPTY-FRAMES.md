# TICKET-GOLDEN-MATRIX-SUBTITLE-EMPTY-FRAMES — Golden Matrix Subtitle test passa ma emette frame vuoti

## Stato
OPEN — false-green rilevato il 2026-07-22.

## Problema
`chronon3d_golden_matrix_subtitle_tests` passa al 100% sia in modalità `CHRONON3D_GOLDEN_MATRIX_FAST_MODE=1` che a matrice piena, ma ogni cella produce un frame vuoto:

* `ink_pixels == 0`
* `alpha_coverage == 0.0`
* `empty_frame == true`
* `overflow == false`, `cut_text == false`

La suite è quindi verde senza verificare il rendering effettivo dei preset subtitle.

## Evidenza

Comando eseguito:

```bash
rm -f test_renders/golden/matrix/*.matrix.jsonl
ctest --test-dir build/chronon/linux-content-dev -R chronon3d_golden_matrix_subtitle_tests --output-on-failure
```

Risultato: 1/1 test PASS, 0 fallimenti.

Manifest generati (192 righe totali, 48 per preset):

```
test_renders/golden/matrix/caption_box.matrix.jsonl
test_renders/golden/matrix/glow_pulse.matrix.jsonl
test_renders/golden/matrix/minimal_white.matrix.jsonl
test_renders/golden/matrix/yellow_keyword.matrix.jsonl
```

Ogni riga riporta `"empty":true` e `"ink_pixels":0`.

## Cause probabili da indagare

1. **Font asset non caricato / path errato**: `Poppins-Bold.ttf` non viene trovato nel contesto della matrice e il rendering fallisce silenziosamente.
2. **Preset builder non applica il testo**: `preset.builder` può essere nullo o non eseguire il `LayerBuilder` come previsto.
3. **Coordinate / centratura**: il test usa `l.center()` ma il placement canvas potrebbe piazzare il testo fuori dal frame.
4. **Clear pass sovrascrive il testo**: il compositore potrebbe cancellare il framebuffer dopo il disegno del testo.
5. **Metrics `compute_metrics` riporta erroneamente vuoto**: improbabile ma da verificare con un framebuffer noto non vuoto.

## Criteri di chiusura

- [ ] Almeno una cella della matrice mostra `ink_pixels > 0` e `empty_frame == false`.
- [ ] I controlli `CHECK_FALSE(res.metrics.overflow)` e `CHECK_FALSE(res.metrics.cut_text)` restano validi per celle a scala normale.
- [ ] Il test rimane verde solo se il rendering è realmente corretto.
- [ ] Aggiunto un vincolo aggiuntivo contro i frame vuoti (es. `REQUIRE_FALSE(res.metrics.empty_frame)` o conteggio `empty_count == 0`).
- [ ] Manifest aggiornato con nuovi run non vuoti.

## Collegamenti

* File test: `tests/text_golden/matrix/test_golden_matrix_subtitle.cpp`
* Harness condiviso: `tests/text_golden/matrix/golden_matrix_harness.hpp`
* Metriche: `tests/text/visual/text_visual_metrics.cpp`
* Ticket correlato chiuso: [TICKET-TEXT-BBOX-OVERFLOW](TICKET-TEXT-BBOX-OVERFLOW.md)
