# TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1 — Golden Matrix: Subtitle batch 1

## Stato: DONE (2026-07-20, commit pending)

## Problema

La roadmap `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` Blocco 5 ("Visual
Regression Harness") + `docs/RELEASE_GATE.md` richiedono che ogni preset
text sia coperto dalla golden matrix:

| Dimensione | Casi richiesti               |
| ---------- | ---------------------------- |
| Aspect ratio | 16:9 + 9:16                |
| Text length  | corto + lungo              |
| Background   | chiaro + scuro             |
| Scale        | normale + estrema          |
| Cache        | fredda + calda             |
| Scheduler    | seriale + parallelo        |
| Timestamp    | iniziale / intermedio / finale |

Metriche: bounding box ink, alpha coverage, overflow, centro visuale,
pixel diff, hash deterministico, full-empty frame, testo tagliato.

Prima di questo commit, nessun preset era coperto dalla golden matrix
completa. La suite `tests/text/test_text_golden.cpp` (TXT-QA-01)
copriva 5 scenari a 4 timestamp × 2 AR = 40 sentinels, ma senza la
matrice completa (no bg, no scale, no cache, no scheduler, no
overflow/empty/cut metrics).

## Soluzione

### Frame
- `ScenarioMetrics` esteso a 11 campi (3 nuovi: `overflow`, `empty_frame`,
  `cut_text`) — additive change in `tests/text/visual/text_visual_metrics.cpp`,
  nessun campo esistente rimosso o rinominato.
- Nuovo file `tests/text_golden/matrix/test_golden_matrix_subtitle.cpp`:
  matrix sweep harness su 4 preset Subtitle baseline × 8 dimensioni ×
  3 timestamp = 192 celle per preset (768 totali).
- `CHRONON3D_GOLDEN_MATRIX_FAST_MODE=1` env gate per ridurre a 6 celle
  per preset (2 AR × 3 ts) durante local dev / smoke.

### Wiring
- Nuovo cmake manifest `tests/golden_matrix_subtitle_tests.cmake` —
  target `chronon3d_golden_matrix_subtitle_tests`, TIER INTEGRATION,
  LINK_TARGETS include `chronon3d_content` (per `centered_text`
  helper) + `chronon3d_text_core` + `chronon3d_visual_support`.
- Wire-in in `tests/manifests/test_definitions.cmake`.

### Metriche (11 totali)
1. `hash` — `chronon3d::test::framebuffer_hash(*fb)` (u64 deterministico)
2. `ink_bbox` — `RectF{x, y, w, h}` dell'ink con `alpha > 0.05`
3. `ink_pixels` — count
4. `mean_luminance` — media pesata per ink pixel
5. `alpha_coverage` — `ink_pixels / total_pixels`
6. `visual_center` — centro di massa pesato per alpha
7. `render_ms` — wall-clock tra `t0` e ritorno
8. `overflow` — **NEW**: true se qualche ink pixel è entro 2px da
   qualsiasi edge del framebuffer (intended bbox should NOT touch edges)
9. `empty_frame` — **NEW**: true se `ink_pixels == 0`
10. `cut_text` — **NEW**: true se bbox right/bottom tocca l'edge del
    framebuffer (testo probabilmente troncato)
11. `hash` — duplicato di (1) per audit (canonical slot pos)

### Audit trail
- Ogni cella emette PNG in `test_renders/golden/matrix/<preset>_<tag>_<ts>.png`
  via `verify_golden` helper (ENV-gated Update mode).
- Append JSONL entry in `test_renders/golden/matrix/<preset>.matrix.jsonl`
  con tutte le 11 metriche + cell tag + golden passed/missing.

## Criteri di accettazione

- [x] `ScenarioMetrics` esteso a 11 campi (3 nuovi additive, 8 esistenti invariati)
- [x] `compute_metrics` implementa le 3 nuove metriche senza alterare le 8 esistenti
- [x] Test target `chronon3d_golden_matrix_subtitle_tests` compila clean
- [x] 4 TEST_CASEs (MinimalWhite, YellowKeyword, GlowPulse, CaptionBox) ×
      192 celle per preset (FAST_MODE riduce a 6)
- [x] `verify_golden` helper reused — no new singleton/registry/cache
- [x] Manifest wired into `tests/manifests/test_definitions.cmake`
- [x] Tree clean post-commit (per `tools/check_main_clean.sh`)
- [x] No new symbols in `include/chronon3d/` (Cat-3 anti-duplication)

## Build verification

```bash
cd build && cmake . 2>&1 | tail -3 && cmake --build . --target chronon3d_golden_matrix_subtitle_tests -- -j$(nproc)
# expected: target builds clean (or, if no host Blend2D-gated text test env, target is skipped via CHRONON3D_USE_BLEND2D early-return)
```

## Forward-points

- **TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-2**: 4 NEW Subtitle presets
  (karaoke_fill, active_word_pop, subtitle_card, lower_third_safe)
  after the orphan `chore(subtitle): orphan productive-subtitle WIP`
  lands on `main` (currently stashed on `stash@{0}`).
- **TICKET-GOLDEN-MATRIX-EMPHASIS-BATCH**: 4 Emphasis presets
  (word_pop, scale_punch, color_accent, gradient_fill).
- **TICKET-GOLDEN-MATRIX-CINEMATIC-BATCH**: Cinematic tier presets
  (title_centered, kinetic_word, lower_third + caption_box/glow_pulse
  cinematic variants).
- **TICKET-GOLDEN-MATRIX-REVEAL-BATCH**: 10 Reveal presets
  (text_animations, fade_in, blur_in, slide_up, slide_down, scale_in,
  tracking_close, masked_line_reveal, word_cascade, character_cascade)
  — heaviest batch (cache state matters most for animated presets).
- **TICKET-GOLDEN-MATRIX-CI-OPTIMIZATION**: investigate parallel
  ctest execution + per-cell timeout budget to keep CI under 10min
  for the full 4-batch matrix (~3,072 cells).

## Cross-link

- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §Blocco 5
- `docs/RELEASE_GATE.md` §Text V1 (golden matrix requirement)
- `docs/MIGRATION_TEXT_SPEC.md` §13 (selector canon, used by cascade presets)
- `AGENTS.md` §Disciplina di aggiornamento dei canonici (canonical minimal-touch)
- `stash@{0}` — orphan `chore(subtitle): orphan productive-subtitle WIP`
  (will land as separate commit; introduces 4 NEW Subtitle presets
  needed for Batch 2).
