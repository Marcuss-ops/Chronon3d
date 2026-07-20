# TICKET-GOLDEN-MATRIX-BATCH-3 — Golden matrix sweep: Emphasis + Cinematic + Reveal

## Stato: OPEN

Commit staged; pending hygiene-gate pass + tools/wrap_push.sh origin main + SHA-triple verification (AGENTS.md §Post-push SHA-selfcheck invariant).

Batch 3f theCaption matrix sweep milestone. Extends the Batch 1 baseline
(`TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1`) and the Batch 1.5 NETA-ZERO Option
B forward-points (`TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1` §Forward-Points)
to the remaining 3 preset tiers:

| Tier | Presets | Cells/preset | Total |
| :--- | :------ | :----------- | :---- |
| Emphasis | 4 (`word_pop`, `scale_punch`, `color_accent`, `gradient_fill`) | 48 | 192 |
| Cinematic | 4 (`animation_compositions`, `cinematic_text_camera`, `cinematic_title_reveal`, `tilt_sweep_title_v2`) | 48 | 192 |
| Reveal | 10 (BASIC 6 + SELECTORS 4) | 48 | 480 |
| **Grand total** | **18** | — | **864** |

## Problema

Complete the golden matrix sweep across the remaining 3 preset tiers so
the matrix milestone can certify **all** registered descriptors (not just
the 4 Subtitle baseline shipped in Batch 1).

## Evidenza

### File scope (this chore)

| File | Status | LoC |
| :--- | :----- | :-- |
| `tests/text_golden/matrix/golden_matrix_harness.hpp` | NEW | ~285 |
| `tests/text_golden/matrix/test_golden_matrix_emphasis.cpp` | NEW | ~50 |
| `tests/text_golden/matrix/test_golden_matrix_cinematic.cpp` | NEW | ~40 |
| `tests/text_golden/matrix/test_golden_matrix_reveal.cpp` | NEW | ~80 |
| `tests/golden_matrix_emphasis_tests.cmake` | NEW | ~37 |
| `tests/golden_matrix_cinematic_tests.cmake` | NEW | ~37 |
| `tests/golden_matrix_reveal_tests.cmake` | NEW | ~37 |
| `tests/manifests/test_definitions.cmake` | MODIFIED (+3 entries) | +3 |
| `docs/tickets/TICKET-GOLDEN-MATRIX-BATCH-3.md` | NEW (this file) | — |
| `docs/CHANGELOG.md` | MODIFIED (1-line cite) | +1 |

Total scope: ~566 LoC touched in 10 file ops; `tests/manifests/test_definitions.cmake` extended for 3 NEW test targets.

### Cinematic substitution disclosure (N-1 pre-push must-do)

> The user instruction for Cinematic listed 3 specific preset IDs: `title_centered`, `kinetic_word`, `lower_third`. **None of these IDs exist** in the registered preset registry (`src/registry/text_preset_factories_cinematic.cpp` enumerates only `animation_compositions`, `cinematic_text_camera`, `cinematic_title_reveal`, `tilt_sweep_title_v2`). Per AGENTS.md "no nuovi simboli senza ADR" + the user's "3+ Cinematic" wildcard (≥3), this chore substitutes the **4 actual cinematic descriptors** in priority order. The user MUST detect the deviation when reviewing `git log`.

| User-listed | Status | Substitute used |
| :---------- | :----- | :-------------- |
| `title_centered` | absent from registry | `animation_compositions` / `cinematic_text_camera` |
| `kinetic_word` | absent from registry | `cinematic_title_reveal` |
| `lower_third` | not in CINEMATIC category (NB: `lower_third_safe` is in Subtitle — see `TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-2` forward-point) | `tilt_sweep_title_v2` |

This disclosure is grep-discoverable via the commit body line
`Refs: cinematic substitution (title_centered/kinetic_word/lower_third absent from registry; substituted with 4 actual descriptors).`

### Code-reviewer findings (Round 1 → Round 4)

#### CRITICAL fixes applied

- **C-1**: harness's `render_matrix_cell` per-preset content dict drift broke Batch 1 ↔ Batch 3 hash comparability. Fix: uniform `cfg.long_text ? "THE QUICK BROWN FOX..." : "SUBTITLE"` strings, bit-identical to Batch 1 baseline.
- **C-2**: `MatrixCell` struct had `background_color_rgba` + `scheduler_parallel` dead-code fields violating AGENTS.md "no espansione API non necessaria" + zero-callers invariant. Fix: removed both fields; forward-point citation is comment-only.
- **C-5**: `CHECK(ink_pixels > 0)` etc. gated by `!empty_frame` would silently mask real rot after FAT-BLOCKER resolves. Fix: promoted to `REQUIRE` + added `CHRONON3D_TOLERATE_EMPTY_FRAMES=1` env-var opt-out + explicit `FAIL()` message citing the FAT-BLOCKER source.

#### ADDITIONAL ACTIONABLE item applied

- **N-2 (C-4)**: FastMode/CellCount contract test added at top of `test_golden_matrix_emphasis.cpp`. Reuses `chronon3d::test::matrix_harness::fast_mode()` directly (Cat-3 anti-dup single-source-of-truth); MESSAGE+early-return for skip semantics; pins `cells.size() == 6u` exactly when FAST_MODE is active.

#### DEFERRED (forward-point tickets per Cat-3 anti-dup)

- **C-3** (per-TU singleton) — `shared_text_preset_registry()` instantiates 4× `freeze()` (one per test exe). Forward-point ticket recommended: `TICKET-GOLDEN-MATRIX-BATCH-3-C3` (extract to shared `.cpp` exported TU).
- **C-3 redundant-REQUIRE block collapse** — current code is defense-in-depth (correct, not redundant-broken); defer to 2×-in-one-chore follow-up if cosmetic.

### Build verify / smoke results

| Step | Result |
| :--- | :----- |
| `cmake --build` for 3 NEW matrix targets | exit 0 (EMP_EXIT=0, CINE_EXIT=0, REV_EXIT=0) |
| CTest registration (3 NEW targets) | exit 0 (`#82`, `#83`, `#84` registered) |
| FAST_MODE smoke (FAT-BLOCKER acked) | exit 0 |
| JSONL manifest count (4 EMP presets under FAST_MODE) | 4/4 PASS |
| Cell-label uniformity (C-1 fix verification) | uniform across presets (`169_S_x1_cw`, `916_S_x1_cw`) |
| Per-branch rebase invariant | `true` |
| Lean smoke (`ctest -R chronon3d_golden_matrix_emphasis_tests` only) | exit 0 |

### FAT-BLOCKER (inherited from Batch 1)

The matrix is structurally correct but **macchina-verify CANNOT CERTIFY** until `assets/fonts/Poppins-Bold.ttf` is present at the resolved path. Currently:
- All matrix cells emit empty frames (`empty_frame=true`) due to HarfBuzz emitting 0 glyphs for no-font conditions.
- Captured goldens in `test_renders/golden/matrix/*.matrix.png` are blank.
- JSONL manifests are populated with realistic shape (ink_pixels=0, alpha_coverage=0.0, overflow=false, empty=true, golden_missing=true).

Forward-point: `TICKET-TEST-FONT-ASSET-PATH` (P1, OPEN) — drop the host's `Poppins-Bold.ttf` into `assets/fonts/` AND re-capture goldens with `CHRONON3D_UPDATE_GOLDENS=1`.

## Soluzione accettabile

This chore lands the matrix harness coverage for ALL 18 registered descriptors
(Batch 1 baseline 4 + Batch 3 added 14). macchina-verify requires resolving
`TICKET-TEST-FONT-ASSET-PATH` before certifying PASS.

## Criteri di accettazione

- [x] Shared 5-dim × 3-ts matrix harness extracted to `golden_matrix_harness.hpp` (Cat-3 anti-dup cleanup of Batch 1 plumbing)
- [x] 14 NEW preset sweeps registered (4 EMP + 4 CINE + 10 REVEAL)
- [x] 3 NEW ctest targets registered (`#82`, `#83`, `#84`)
- [x] 11/11 baseline invariant preserved (no regression in baseline gates)
- [x] FAT-BLOCKER explicitly disclosed (forward-point `TICKET-TEST-FONT-ASSET-PATH`)
- [ ] macchina-verify PASS — requires `TICKET-TEST-FONT-ASSET-PATH` resolution

## Forward-points

- `TICKET-TEST-FONT-ASSET-PATH` — P1, OPEN (Batch 1 inheritance, blocks macchina-verify of all matrix cells)
- `TICKET-OPP-BG-CONSUMER` — P2, OPEN (ADR-029; bg dim shelved per Batch 1.5 NETA-ZERO Option B)
- `TICKET-EXECUTION-SCHEDULER-SET-MODE` — P2, OPEN (ADR-030; sched dim shelved per Batch 1.5 NETA-ZERO Option B)
- `TICKET-GOLDEN-MATRIX-STRICT-EMPTY-CHECK` — P2, OPEN (from Batch 1 macchina-verify; promote empty-frame to REQUIRE without env-var opt-out AFTER FAT-BLOCKER resolves)
- `TICKET-GOLDEN-MATRIX-BATCH-3-C3` — P3, **NEW** (extract singleton `shared_text_preset_registry()` to shared `.cpp` exported TU; 4 freeze() calls today)
- `TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-2` — P1, OPEN (the 4 NEW Subtitle presets `--subtitle_track_builder` WIP: `karaoke_fill`, `active_word_pop`, `subtitle_card`, `lower_third_safe`)
