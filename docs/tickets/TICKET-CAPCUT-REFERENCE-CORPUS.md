# TICKET-CAPCUT-REFERENCE-CORPUS — CapCut reference corpus + parity test

## Stato: OPEN (2026-07-21, commit pending)

## Problema

Per chiudere il milestone M3 "CapCut-grade Parity" del verdict
[`docs/ROADMAP.md`](../ROADMAP.md) (line ~511 — `TICKET-CAPCUT-REFERENCE-CORPUS`)
serve un corpus di riferimento **blessed** da CapCut (gold standard per
parità visiva) + un test di parità che confronta l'output Chronon3D con
questo corpus.

I golden test interni (`tests/golden/`, `tests/text_golden/`) usano output
Chronon3D come gold standard. Per la parity con CapCut serve un riferimento
**esterno** (NON auto-generato da Chronon3D).

## §Criteri di accettazione

- ✅ Directory skeleton `tests/reference/capcut/{static,subtitles,effects}/` con `.gitignore` locale per subdir
- ✅ README policy doc (blessed-only + no auto-substitution + naming convention)
- ✅ Test skeleton `test_capcut_parity.cpp` con metric helpers (SSIM reuse + inline baseline/bbox/missing_glyphs/cut_text)
- ✅ CMakeLists.txt standalone con `chronon3d_add_test_suite(TIER INTEGRATION)` + LINK_TARGETS override per `chronon3d_visual_test_support`
- ✅ Wire-in in `tests/manifests/test_definitions.cmake` (ADR-018 pattern)
- ⏳ ≥1 blessed reference PNG per area (DEFERRED-PR-review, see §Forward-points)
- ⏳ Metric extraction to `tests/visual/support/capcut_metrics.hpp` IF metric helpers reused elsewhere (DEFERRED post-Fase 9)
- ⏳ WBH macchina-verifica: `ctest -R chronon3d_capcut_parity_tests --output-on-failure` (DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent)

## §Soluzione adottata (Cat-3 minimal-surface)

### Struttura
```
tests/reference/capcut/
├── README.md                   (policy + how-to-seed + naming convention)
├── CMakeLists.txt              (chronon3d_add_test_suite)
├── test_capcut_parity.cpp      (skeleton + 4 metric helper unit tests)
├── static/
│   ├── .gitignore              (current/ ignored, reference/ tracked-post-PR)
│   ├── reference/.gitkeep      (placeholder; blessed PNGs land here post-PR)
│   └── current/.gitkeep
├── subtitles/ (same structure)
└── effects/    (same structure)
```

### Policy
- `reference/` blessed-only (commit via PR review esplicito che dimostri blessed-ness)
- `current/` regenerable (gitignored)
- NO auto-substitution (anche con `CHRONON3D_UPDATE_GOLDENS=1`)
- Naming: `<area>_<font>_<test-id>.png`

### Metriche (inline helpers in test file, no `image_diff.hpp` extension)
- `compute_ssim(actual, expected)` — riuso `chronon3d::test::compute_ssim` (image_diff.hpp)
- `compute_baseline_error(actual, expected)` — ink_vertical_extent diff (pixel_scan_helpers.hpp)
- `compute_bbox_error(actual, expected)` — alpha_bbox per-side diff
- `count_missing_glyphs(actual, expected)` — pixel-by-pixel expected-has-ink-but-actual-not
- `detect_cut_text(actual)` — alpha_bbox touches framebuffer edge

### Soglie (verdict §Fase 9)
| Metrica | Soglia |
|---|---|
| baseline err | ≤ 1 px |
| bbox err (per lato) | ≤ 2 px |
| SSIM-on-ROI | ≥ 0.95 |
| changed_pixel_ratio (ROI) | ≤ 5% |
| missing_glyphs | 0 |
| cut_text | false |

### Graceful-skip pattern
Skeleton phase = corpus vuoto. Tutti i test skip con:
```cpp
if (refs.empty()) {
    MESSAGE("§blessed-reference corpus not yet populated for <area>/ — see TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points");
    CHECK(true);  // graceful-skip
    return;
}
```

## §Forward-points

- **(a) Seed ≥1 blessed static PNG** (P1, OPEN, DEFERRED-PR-review): serve designer/riferimento CapCut reale (Inter, Noto Sans, Liberation) + export manuale PNG lossless + commit con subject che cita tool + font + risoluzione + PR review che dimostri blessed-ness (NON auto-generato).
- **(b) Seed ≥1 blessed subtitle PNG** (P2, OPEN, DEFERRED-PR-review): karaoke subtitle con word-timing da CapCut, formato `<area>_<font>_<test-id>.png`.
- **(c) Seed ≥1 blessed effect PNG** (P2, OPEN, DEFERRED-PR-review): glow/anim effect frame da CapCut.
- **(d) Metric extraction to `tests/visual/support/capcut_metrics.hpp`** (P3, DEFERRED-post-Fase 9): refactor inline helpers se metric vengono riusate da altri test (es. `tests/text_golden/text_completeness/text_alignment.cpp` per edge_distance). Cat-3 anti-dup discipline.
- **(e) WBH macchina-verifica** (P1, DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent): `ctest -R chronon3d_capcut_parity_tests --output-on-failure` su working build host.

## §Cronologia

- 2026-07-21: skeleton committato (this commit).
- 2026-07-21: TICKET aperto (cronaca-estesa home).
- Forward-point cluster: (a) seed blessed static; (b) seed blessed subtitle; (c) seed blessed effect; (d) refactor metric IF reused; (e) WBH macchina-verifica.

## References

- [`docs/ROADMAP.md`](../ROADMAP.md) — M3 "CapCut-grade Parity" milestone (line ~511)
- [`docs/tickets/TICKET-GRAPHICS-SHAPE-STYLE-ROT.md`](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) — sibling build rot context
- [`tests/visual/support/image_diff.hpp`](../../tests/visual/support/image_diff.hpp) — `compute_ssim` reuse
- [`tests/text_golden/text_completeness/pixel_scan_helpers.hpp`](../../tests/text_golden/text_completeness/pixel_scan_helpers.hpp) — `alpha_bbox` + `ink_vertical_extent` reuse
- [`tests/bench_corpus/CMakeLists.txt`](../../tests/bench_corpus/CMakeLists.txt) — precedent for per-area standalone CMakeLists
- AGENTS.md §Feature freeze — Cat-3 zero-surface preserved (skeleton in tests/, no include/chronon3d/ touched)
- AGENTS.md §Disciplina di aggiornamento dei canonici — cronaca-estesa in this TICKET, canonicals sintetici
- AGENTS.md §Docs canonical update discipline rule — `docs/CHANGELOG.md` + `docs/FOLLOWUP_TICKETS.md` aggiornati in this commit
