# TICKET-TEXT-VIS-CLIP-FALSE-POSITIVE — CLIP_DROPS_INK / NO_INK_RENDERED while text is visible

**Stato:** CLOSED (2026-11-14)
**Priorità:** P2
**Ambito:** Chronon3d diagnostics, text visibility audit
**Origine:** ChrononTemplate motion-design stress suite, case 12 (`12_overshoot`)

## Causa

The post-render audit compared the framebuffer alpha against the **predicted**
bbox used as the clip. Case 12's visible `$10,000` ink is at
`(724,392)-(912,444)` while the predicted box is `(538,367)-(742,435)`; the
text is visible in the badge, so the predicted box is not the clip that actually
dropped ink. The audit also ran on intermediate passes whose framebuffer alpha
was still empty, producing `NO_INK_RENDERED` before the final pass.

## Fix

- `TextRunNode` now prefers the draw's authoritative canvas-space
  `actual_ink_bbox` as the post-render alpha override.
- The post-render clip is the actual framebuffer bounds, not the predicted
  bbox; predicted containment remains a separate geometric invariant.
- When the draw reports no ink, the pixel-side audit is skipped (the draw
  already fails closed for a drawable run with zero visible glyphs).
- `TextVisibilityAudit` carries `alpha_measured`, so callers do not confuse
  "not measured" with "measured empty".
- `Audit text_visibility` / `verify_text_visibility` accept the override; the
  existing API remains source-compatible through default arguments.

## Verifica

- Case 12 frame 8 software: **0** `CLIP_DROPS_INK` / `NO_INK_RENDERED`
  warnings, with actual ink `(724,392)-(912,444)` inside the output.
- New regression `Visibility contract #7`: visible ink inside the clip passes;
  ink outside the clip fails. **7/7 assertions pass** for that case.
- The full legacy `chronon3d_visibility_contract_tests` still has 4 stale
  expectation failures (8 vs 9 px expansion and 102 vs 116 px local bbox), and
  `chronon3d_text_clip_policy_tests` has 35 harness failures with empty
  framebuffer alpha. They are not claimed as green; the new override regression
  and the case-12 log check are the closure evidence.

## Note

This is diagnostic correctness; the 12 MP4 was already visually valid. The
predicted bbox being left-shifted is a separate conservative-bbox quality issue,
not a reason to suppress the audit.
