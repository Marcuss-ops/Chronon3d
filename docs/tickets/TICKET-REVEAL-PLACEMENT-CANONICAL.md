# TICKET-REVEAL-PLACEMENT-CANONICAL — TextRevealDescriptor placement canonicalization

## Stato

IN PROGRESS (2026-07-13, Phase A — ticket + golden baseline snapshot)

## Priorità

P1 (Text Production V1 hardening — `pin_to_center` + `base_pos` is a legacy
side-channel that bypasses the canonical `TextPlacement` resolver)

## Problema

`chronon3d::content::text_reveal::TextRevealDescriptor` exposes TWO parallel
positioning mechanisms that should be one canonical path:

1. **Legacy path (current)**: `bool pin_to_center{false};` + `Vec3 base_pos{0,0,0};`
   → At runtime in `build_text_reveal_line()` body, when
   `d.pin_to_center == true`, the surrounding closure executes
   `l.pin_to(Anchor::Center);` THEN `l.position({cx, d.base_pos.y, d.base_pos.z});`.
   This is a side-channel mutation of `LayerBuilder`'s anchor that
   **bypasses** the canonical text placement resolver
   `TextPlacementKind::CanvasCenter` (`chronon3d::scene.builders` SDK
   header), which is the canonical pre-resolved placement in the rest of
   the codebase (cert_* / content/common/animation_helpers / content/certification / content/text_placement /
   content/certification/cert_* — 49 confirmed call sites already use
   `TextPlacement{TextPlacementKind::CanvasCenter, {}}`).

2. **Canonical replacement target**:
   `TextPlacement placement{TextPlacementKind::CanvasCenter, {}}; Vec3 offset{};`
   where `placement.kind` selects the anchor (CanvasCenter is the most
   semantically equivalent replacement for `pin_to_center=true`), and
   `offset` replaces `base_pos.{y,z}` (Y line position + cinematic Z) via
   the resolver's standard offset pipeline. `offset.x` is reserved for
   future safe-area placement hooks.

This dual system creates:
- **API drift**: new contributors don't know which path is canonical.
- **Resolver asymmetry**: `tests/tools/check_no_dual_text_api.sh` (per
  AGENTS.md Cat-3 anti-duplication precedent) flags any new TextSpec
  variant — same concern should apply to TextRevealDescriptor placement.
- **Maintenance cost**: the `if (d.pin_to_center) ...; l.position({... base_pos ...});`
  pattern in `text_reveal.cpp:45-48` is a hand-rolled bypass that
  re-implements text resolver internals, fragile to GridZ-axis contributions
  (whip_pan_hero_reveal uses `base_pos = {0.0f, 0.0f, 0.0f}` for the title
  but `L.position()` directly for the subtitle — inconsistent styles).

## Evidenza

### Current state (code-search verbatim)

**`content/common/text/text_reveal.hpp:44`**: `Vec3 base_pos{0.0f, 0.0f, 0.0f};  // y-pos of the text line`
**`content/common/text/text_reveal.hpp:58`**: `bool  pin_to_center{false};  // call l.pin_to(Anchor::Center) before positioning`
**`content/common/text/text_reveal.cpp:45-48`**:
```cpp
if (d.pin_to_center) {
    l.pin_to(Anchor::Center);
}
l.position({cx, d.base_pos.y, d.base_pos.z});
```
**`content/common/text/text_reveal.cpp:61-63`**: `slide_up` position animation timeline also reads `d.base_pos.y` + `d.base_pos.z`.

### Existing `TextPlacementKind::CanvasCenter` usage (canonical target precedent)

**`content/compositions/chronon_glow_final.cpp:190`**: `chronon3d::TextPlacementKind::CanvasCenter` + comment "this file (cinematic_glow) is one of 4 single-responsibility TUs that REPLACES pin_to-center via TextPlacement resolver" (Phase 3 SCALA fix inheritance).
**`include/chronon3d/scene/builders/builder_params.hpp`** + cert_* / content/certification / content/text_placement: 49+ canonical consumer sites already use `TextPlacement{TextPlacementKind::CanvasCenter, {}}` for the same semantic (center the text on the canvas).

### Test surfaces affected (golden byte-equivalence is the constraint)

| Test surface | File | Reason affected |
|---|---|---|
| Typewriter golden baseline | `tests/text_golden/text_completeness/text_typewriter.cpp` (6 TEST_CASEs) | All 5 TestRevealDescriptor typewriter factory calls (`anim_typewriter_simple/cursor/slide/glow/stagger` → `TextRevealDescriptor` literal) use `pin_to_center = true` + `base_pos = {0, ..., 0}` |
| `build_2line_typewriter` | `tests/text_golden/...` → calls `typewriter_block.cpp` which sets `pin_to_center=true` + `base_pos={0, base_y - line_spacing*0.5, 0}` | Per-character positioning through canonical resolver |
| Whip-pan cinematic showcase | `content/showcases/cinematic/whip_pan_hero_reveal.cpp:123` | `TextRevealDescriptor{ ... .base_pos = {0.0f, 0.0f, 0.0f}, ... }` for `CHRONON3D` staggered title — uses `base_pos.z = 0` and passes-only canonical resolver |
| Glow_AB composition | `tests/visual/glow_ab/glow_ab_compositions.cpp` | Sibling composition that uses `build_2line_typewriter` (= same TextRevealDescriptor path) |
| Edge cases (combining marks + UTF-8) | `tests/text_golden/text_completeness/text_edge_cases.cpp:171` (§06 combining marks no crash) + `tests/text_golden/text_multilingual/08_fallback_matrix.cpp` | Indirectly verified via typewriter pass-through |

### Test surfaces NOT affected (explicit non-goal exclusion)

| Path | Reason excluded |
|---|---|
| Cinematic showcases (deep_parallax_cascade, abyss, orbit, rack_focus) | Use `l.text("label", def)` directly via `from_text_spec(TextSpec{...})` — bypass `TextRevealDescriptor` entirely |
| `content/compositions/chronon_glow_final.cpp` | Uses `l.text_run("glow_pulse", TextRunSpec{...})` directly with `placement = TextPlacement{Kind::CanvasCenter, ...}` already in canonical form (post-Phase 3 SCALA fix) |
| Whip-pan subtitle line | Uses `l.text("subtitle_label", def)` directly via `TextSpec{...}` |

## Impatto

- **API surface (Cat-3)**: ZERO new public SDK API. The change is removal of
  internal `pin_to_center` + `base_pos` fields and replacement with
  `placement` + `offset` (both ALREADY exist as `TextPlacement` in
  `include/chronon3d/scene/builders/builder_params.hpp`).
- **Caller surface**: 7 callers across `typewriter_block.cpp:34,36,46` +
  `typewriter_animations.cpp:5 call sites` + `whip_pan_hero_reveal.cpp:123`
  must be updated atomically with each phase to maintain buildable state.
- **Cinematic Z showcase**: whip_pan_hero_reveal's `base_pos.z = 0` case is
  trivial (offset.z = {0,0,0} default), but the abstraction raises a
  forward-point for future Z-axis street-typed offset semantics (e.g.,
  parallax depth token).
- **Golden regression risk**: HIGH if Phase B introduces incompatible
  resolver semantics. Mitigated by phased rollout + golden baseline
  snapshot.

## Confine

### In scope

- 4-site caller update + descriptor field replacement in
  `text_reveal.hpp` (pin_to_center/base_pos → placement/offset).
- Build-time discriminator for the dual-path runtime in Phase B
  (e.g., `std::optional<TextPlacement> placement` or auto-detect-by-fingerprint).
- Golden baseline capture procedure (see §Soluzione accettabile Phase A).
- Per-phase user-side bash protocol for commit + golden verify + push.

### NOT in scope

- Modifying the `TextPlacement` resolver itself (its semantics are already
  canonical — emerging in `chronon3d::scene.builders` SDK header).
- Modifying the cinematic showcases that bypass `TextRevealDescriptor`
  (already use `TextPlacement` directly).
- Adding new public SDK API in `include/chronon3d/`.
- Modifying golden test ASSERTIONS — only re-bake goldens post-refactor
  (pre-Phase-A baseline snapshot DOES NOT change assertions).

## Soluzione accettabile

The refactor proceeds in 4 atomic phases, each pushed separately on
`main`, each producing a stable buildable state with all 11 baseline
gates passing (per `tools/run_developer_gates.sh`).

### Phase A (this commit) — Ticket + golden baseline snapshot

1. Open this ticket (file exists at the canonical ticket path).
2. Capture golden PNG baseline snapshot pre-refactor via:

   ```bash
   # ── Golden baseline snapshot procedure (basher dipendenza) ─────────
   # Step 1: Staleness check (AGENTS.md §honesty pre-ctest invariance)
   SRC="src/render_graph/executor/level_timings.cpp"   # proxy più recente
   TEST_BIN="build/tests/text_golden/text_typewriter_tests"
   [ -x "$TEST_BIN" ] || { echo "STALE: rebuild required" >&2; exit 1; }
   [ "$SRC" -nt "$TEST_BIN" ] && { echo "STALE" >&2; exit 1; }

   # Step 2: Capture baseline (single canonical state)
   cmake --build build --target chronon3d_text_golden_tests
   cd build && ctest -R "TextGolden|Typewriter|FallbackMatrix|NoClip|ArabicShaping|HebrewNikud" \
       --output-on-failure
   # Pre-rebake goldens (1 volta baseline — guarda i PNG hashes actual):
   CHRONON3D_UPDATE_GOLDENS=1 ctest -R "TextGolden" --output-on-failure
   # Post-rebake validation (re-run WITHOUT UPDATE_GOLDENS to ensure
   # the re-stamped goldens reproduce deterministically):
   ctest -R "TextGolden" --output-on-failure
   cd "$OLDPWD"

   # Step 3: Hash record (manuale — paste into this ticket)
   rg -c "verify_golden" tests/text_golden/ -g "*.cpp" | tee /tmp/phase_a_baseline.txt
   md5sum build/testing/text_golden/*.png | tee -a /tmp/phase_a_baseline.txt
   # OR: commit-time hash capture via `git diff --stat` post-bake
   ```

3. Append baseline snapshot summary to this ticket's §Forward-points
   (md5sum list scoped per typewriter/file).

### Phase B — Dual-path runtime introduction (single commit)

Add `TextPlacement placement` (default `TextPlacement{TextPlacementKind::Absolute, {0,0}}`)
+ `Vec3 offset{0,0,0}` to `TextRevealDescriptor`. Runtime in
`build_text_reveal_line` becomes a 2-path selector:

- **Old path** (if `placement.kind == TextPlacementKind::Absolute` AND `placement.offset == {0,0}`):
  use `pin_to_center + base_pos` as before (pre-refactor semantics).
- **New path** (otherwise): use `placement + offset` via the canonical
  `placement.resolve(builder, offset)` resolver hook (delegate to
  `text_resolver` SDK consumer).
- This dual-path is BUILDABLE per phase: callers that don't change
  anything continue to take the old path. Callers that explicitly opt
  into the new path get identical visual output (byte-equivalence with
  Phase A baseline).

```cpp
// Phase B dual-path runtime (illustrative)
if (d.placement.kind == TextPlacementKind::Absolute &&
    d.placement.offset == Vec2{0.0f, 0.0f}) {
    // Old path — pin_to_center + base_pos (preserved verbatim)
    if (d.pin_to_center) l.pin_to(Anchor::Center);
    l.position({cx, d.base_pos.y, d.base_pos.z});
} else {
    // New path — placement resolver drives anchor + offset
    l.position(d.placement.resolve_position(cx, d.offset));
}
```

### Phase C — Golden verification (single commit)

Re-bake goldens WITH the dual-path active (no behavior change expected
because old-path triggers by default until Phase D). Compare against
Phase A baseline md5sums:

- PASS criteria: every Golden PNG hash unchanged zero (md5sum-identical
  with Phase A baseline).
- FAIL criteria: ≥ 1 PNG hash differs — investigate; do NOT commit
  until root cause identified (likely a discriminator mistake in Phase B).

### Phase D — Old path removal (single commit)

Only after Phase C verifies byte-equivalence:

1. Remove `pin_to_center` + `base_pos` fields from `TextRevealDescriptor`.
2. Replace old-path runtime in `text_reveal.cpp:45-48` with new-path
   alone.
3. Update 5 callers (`typewriter_block.cpp:34,36,46` + 5 sites in
   `typewriter_animations.cpp` + `whip_pan_hero_reveal.cpp:123`) to use
   `placement + offset` directly.
4. Re-bake goldens + verify byte-equivalence ONE LAST TIME (sanity
   against the Phase C golden validation).

### Subject envelopes (4 commits, one per phase)

```text
docs(text_reveal): open TICKET-REVEAL-PLACEMENT-CANONICAL + golden baseline
refactor(text_reveal): introduce dual-path placement+offset alongside pin_to_center+base_pos
test(text_reveal): verify golden byte-equivalence for dual-path (Phase C lock)
refactor(text_reveal): remove pin_to_center + base_pos canonical placement-only
```

(Phase D is the last cat-3 surface-cleanup; Phase A is docs-only, no
code change; Phase B adds dual-path fields; Phase C is golden-only.)

## Criteri di accettazione

- [ ] All 11 baseline gates PASS at every phase-end (per `tools/run_developer_gates.sh`).
- [ ] Phase A: ticket exists at canonical path + baseline md5sum record
      captured (user-side basher execution documented).
- [ ] Phase A: zero code changes; `git diff` after Phase A commit must
      affect ONLY `docs/tickets/TICKET-REVEAL-PLACEMENT-CANONICAL.md`
      (the canonical ticket file).
- [ ] Phase B: dual-path runtime compiles, both old + new callable
      paths emit byte-identical layer positions for equivalent inputs.
- [ ] Phase C: every typewriter golden PNG hash md5sum-identical with
      Phase A baseline (zero regression in the typewriter golden suite).
- [ ] Phase D: `pin_to_center` + `base_pos` removed cleanly from
      `text_reveal.hpp`; all callers migrated; old-path code deleted.
- [ ] Cat-3 minimal-surface verified at every phase: zero public SDK
      API change (`include/chronon3d/`).
- [ ] Golden testsuite `tests/text_golden/text_completeness/text_typewriter.cpp`
      (6 TEST_CASEs) + `08_fallback_matrix.cpp` §10 + NoClip 04 +
      06/07_arabic_hebrew + 12_anim_framerate_determinism PASS
      byte-equivalent at Phase D final state.

## Forward-points (per phase)

### Phase A forward-points

- `TICKET-REVEAL-PLACEMENT-CANONICAL-GOLDEN-BASELINE` — basher-side
  capture of golden PNG md5sum list before any code change.

### Phase B forward-points

- `TICKET-REVEAL-PLACEMENT-DUAL-PATH-DISCRIMINATOR` — explicit decision
  record on discriminator strategy (default-empty-string fingerprint
  vs. explicit `std::optional<TextPlacement>` field).
- `TICKET-REVEAL-PLACEMENT-DRIFT-CHECK` — future Cat-3 anti-dup gate
  (`tools/check_no_dual_text_api.sh`) extended to also flag dual-path
  legacy `pin_to_center + base_pos` on TextRevealDescriptor.

### Phase C forward-points

- `TICKET-REVEAL-PLACEMENT-MACHINE-VERIFY` — working build host:
  `ctest --test-dir build/chronon/linux-content-dev -R
  "TextGolden|Typewriter|FallbackMatrix" --output-on-failure` expects
  zero regression in golden PNG hashes.

### Phase D forward-points

- `TICKET-REVEAL-PLACEMENT-CALLER-MIGRATION-VERIFY` — after Phase D
  commit, `rg -n "pin_to_center|TextRevealDescriptor.*base_pos" src/
  content/ tests/ apps/` returns 0 matches (full purge).
- `TICKET-REVEAL-PLACEMENT-FORWARD-ONLY` — post-Phase-D stability check:
  any new caller must use `placement + offset` only (no canonical
  undo; matches Phase 3 SCALA fix model from `chronon_glow_final.cpp`).

## Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Phase A baseline snapshot incomplete (missing golden PNGs) | LOW | Explicit procedure with `git diff --stat` post-bake corpus check + md5sum record |
| Phase B dual-path runtime has discriminator bug (silently takes old path always) | MED | Compiler assert via static_assert or explicit warning log when discriminator selects old path on a non-empty descriptor |
| Phase C golden regression undetected | LOW | 5 staggered surfaces (typewriter / curly ligatures / Arabic lam-alef / Hebrew nikud / framerate determinism) |
| Phase D caller migration breaks compilation | MED | 5 explicit caller paths to migrate, listed in §Confine; compile-clean per `tools/build.sh` after each migration |
| `offset` semantics ambiguity (caller uses offset.x for x-pos instead of placing x via ref_offset_x) | LOW | `offset` is reserved for Y/Z by convention; x placement stays via `ref_offset_x` (existing field); docstring pins this |

## Cross-link

- `docs/DOCUMENTATION_GOVERNANCE.md` §ticket-template — the template
  this ticket follows verbatim.
- AGENTS.md §honesty closure — every phase SHA-triple verified via
  `tools/wrap_push.sh` + SHA-triple trick.
- AGENTS.md §Cat-3 minimal-surface — applies to every phase
  (zero new symbols in `include/chronon3d/`).
- The previous `chronon_glow_final.cpp` Phase 3 SCALA fix (commit
  referenced in `content/compositions/chronon_glow_final.cpp:148,177,190`)
  — same canonical pattern (`TextPlacementKind::CanvasCenter` over
  ad-hoc `pin_to_center + base_pos`); this ticket replicates the pattern
  for `TextRevealDescriptor`.
- The existing `tests/tools/check_no_dual_text_api.sh` Cat-3 anti-dup
  precedent — see follow-point `TICKET-REVEAL-PLACEMENT-DRIFT-CHECK`.
- 11-baseline gate suite (`tools/run_developer_gates.sh`) — gated at
  every phase entry + exit.

## Historical notes

- Phase A: ticket opened 2026-07-13; baseline snapshot procedure
  documented for user basher-side execution on working build host
  (Honest-limitation: VPS lacks vcpkg glm/magic_enum + tmpfs quota;
  macchina-verifica of baseline md5sum diff DEFERRED to working build
  host per AGENTS.md §honest-limitation).
