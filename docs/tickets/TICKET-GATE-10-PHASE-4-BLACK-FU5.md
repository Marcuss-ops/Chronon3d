# TICKET-GATE-10-PHASE-4-BLACK-FU5 — PNG content mean-RGB metric rot at install_consumer Phase 4 strict

| Field           | Value |
|-----------------|-------|
| ID              | TICKET-GATE-10-PHASE-4-BLACK-FU5 |
| Priorità        | P0 |
| Area            | install_consumer Phase 4 strict certifier (PNG readback gate) |
| Stato           | OPEN |
| Lineage         | TICKET-GATE-10-PHASE-4-BLACK parent + TICKET-GATE-10-PHASE-4-BLACK-FU4 sibling DONE |
| Discovery commit | main@0b365354 (post-FU4-pivot audit re-run) |
| Bloque baseline | 11/11 verde non raggiungibile senza fix |

## Stato (one-line)
OPEN. Discovery durante FU4-pivot audit re-run: compile PASS, link PASS, runtime consumer any-channel PASS, gate Phase 4 strict mean-RGB FAIL.

## Problema
metrics divergence all install_consumer Phase 4 strict:
- Consumer-side any-channel assertion (alpha-aware): `230400/230400 pixels >5/255` PASS.
- Gate script mean-RGB assertion (alpha-blind): `0 pixels with mean RGB > 5/255` FAIL.
- Framebuffer in-memory: all-white RGBA(1.0, 1.0, 1.0, 1.0).

Possibili rot candidate:
- (a) image_writer.cpp::save_png() scrive RGB=0 nonostante Framebuffer bianco (extension of M1.5#9 rot);
- (b) gate script Phase 4 strict check metric misalignment (any-channel vs mean-RGB);
- (c) rendering pipeline silenced error producing empty framebuffer;
- (d) PNG encoding format edge case (alpha=255 + RGB=0).

Distinct from FU1 (latent premul alpha in semi-transparent edge cases): FU5 covers FULL-transparent alpha=1 cases.

## Confine (In scope by FU5)
- write-side PNG readback diagnostic (extension of M1.5#8 tests/debug/test_save_png_roundtrip.cpp methodology).
- Apply fix-path chosen via investigation.
- Verify Phase 4 strict exit 0 + 11/11 verde.

## Soluzione accettabile
Default path: Path 1 (investigate image_writer.cpp actual RGB-rot via write-side diagnostic test). Fallback options:
- Path 2: change gate script Phase 4 strict check metric to any-channel with threshold ≥ 5/255 (alphaware); document divergence rationale.
- Path 3: rendering pipeline debug (silenced-error source).
- Path 4: PNG encoding format edge case.

## Criteri di accettazione
- bash tools/install_consumer_test.sh exit 0 (Phase 4 strict PASS).
- 11-gate audit 11/11 PASS sullo stesso commit post-fix.
- Atomic commit on main + push via tools/wrap_push.sh GATE-MNT-01.

## Cross-references
- TICKET-GATE-10-PHASE-4-BLACK (parent)
- TICKET-GATE-10-PHASE-4-BLACK-FU4 (sibling DONE @ 0b365354)
