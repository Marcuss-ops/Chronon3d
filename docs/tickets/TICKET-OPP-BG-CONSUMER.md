# TICKET-OPP-BG-CONSUMER \u2014 OPP wiring for CompositionSpec::background_color_rgba

## Stato: OPEN (2026-07-20, opened atomicamente with Batch 1.5 NETA-ZERO commit)

## Problema

TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1 Batch 1.5 PARTIAL (Round 2) added
additive `std::uint32_t background_color_rgba{0x00000000u}` to
`chronon3d::CompositionSpec`.  CRITICAL A confirmed via rg this session:
the OPP compiler does NOT consume the field during the clear pass / scene compile:

  SoftwareRenderer::render(comp, frame)
    \u2192 render_scene_via_graph(comp.m_spec, ...)
       \u2192 OPP compile (consumes text_spec / spec.frame_rate / etc.)
          \u2192 clear pass (HARDCODED transparent-black, ignores m_spec.background_color_rgba)
          \u2192 fb.fill(...)

Adding the bg dimension to the matrix BEFORE the OPP consumer is wired would
emit bit-identical goldens for `_l` and `_d` cells (silent-fake green suite \u2014
`verify_golden` checks each cell against its OWN captured golden, not
cross-cell uniqueness).

## Soluzione accettabile (deferred \u2014 requires ADR-029)

### Wiring paths (any ONE sufficiently wires the bg dimension)

- **Option \u03b1**: thread `m_spec.background_color_rgba` through OPP compile
  into the clear pass.  1 OPP compile site update + 1 clear pass update.

- **Option \u03b2**: derive bg color in `render_scene_via_graph` from
  `comp.m_spec.background_color_rgba`, store as a rendering-graph parameter,
  clear pass reads from graph params.

- **Option \u03b3**: (escape hatch) post-render fb.fill in the matrix test
  (`(*fb).fill(Color::dark_slate)` if `cfg.bg_dark`).  Spec-violating but
  pragmatic \u2014 bypasses the canonical OPP path.

### Recommended: Option \u03b1 + ADR-029-bg-opp-consumer.md

- Minimal blast radius.
- ADR-grade: document the OPP-clear semantic (when bg non-zero, ALL pixels
  overwritten; when bg is 0, preserve default-clear behaviour).
- Cross-reference: TICKET-EXECUTION-SCHEDULER-SET-MODE for parallel
  scheduler API architectural decision.

## Criteri di accettazione

- [ ] ADR authored (new doc `docs/adr/ADR-029-bg-opp-consumer.md`) documenting
  the OPP-clear semantic + the cross-cell uniqueness assertion requirement.
- [ ] OPP compile path updated to thread the field into clear pass.
- [ ] Test: `tests/text_golden/matrix/test_golden_matrix_subtitle.cpp` Batch
  1.5-ext adds bg dim back (192 \u2192 384 cells), with explicit cross-cell assertion
  `bg_dark=true cell hash != bg_dark=false cell hash`.
- [ ] ZERO new `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 deny-everywhere).
- [ ] Subject envelope \u2264 72 chars per `tools/check_commit_subject_length.sh`.
- [ ] Cronaca canonical ticket-home per AGENTS.md \u00a7`### Docs canonical update discipline rule` Cat-3 anti-dup.

## Accountability (anti-rot hook)

To prevent the retained `CompositionSpec::background_color_rgba` field from
decaying into permanent dead code per AGENTS.md "no espansione API non
necessaria" + the zero-callers invariant:

- **Owner**: TBD (cat-2 freeze assigns via `docs/FOLLOWUP_TICKETS.md` §Open Blockers)
- **Trigger**: V0.1 release-prep milestone or earlier if the bg dimension
  is needed for a downstream chore (TICKET-GOLDEN-MATRIX-CINEMATIC-BATCH or
  TICKET-GOLDEN-MATRIX-REVEAL-BATCH).
- **Resolution paths if not consumed by V0.1-prep**:
  1. OPP consumer wired (this ticket ACTIVE: closes via ADR-029-bg-opp-consumer.md).
  2. OR Remove `CompositionSpec::background_color_rgba` field via
     `git revert` + drop the field (clean tree, no rot).

## Acceptance verification (gated by accountability)

- [x] ADR ref ADR-029-bg-opp-consumer.md documented (forward-point).
- [ ] **OpB-1**: OPP compile path updated to thread the field into clear pass
       (ADR-required, assigned per V0.1-prep).
- [ ] **OpB-2**: Test cronaca verifies `bg_dark=true hash != bg_dark=false hash`
       cross-cell uniqueness assertion in matrix test (silent-fake green prevention).
- [ ] **OpB-3-or-remove**: Either OpB-1+OpB-2 land, or `CompositionSpec::background_color_rgba`
       field REMOVED + forward-point ticket CLOSED with rationale.

## Forward-points (out of scope for this ticket)

- TICKET-GOLDEN-MATRIX-CI-OPTIMIZATION (P2) \u2014 matrix CI expansion.
- TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-2 (P1) \u2014 orphan productive-subtitle WIP on `stash@{0}`.

## Cross-link

- `TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1` \u00a7Forward-points (re-opened per Batch 1.5 NETA-ZERO)
- `include/chronon3d/timeline/composition.hpp` (canonical CompositionSpec)
- `include/chronon3d/backends/software/software_renderer.hpp` (`render()` entry)
- `src/render_graph/` (OPP compile path; consult file_picker for canonical entry)

## \u00a7honest-discipline lineage

This ticket was opened atomicamente per AGENTS.md `### 2×-in-one-chore: deprecation reversal bundles forward-point tickets (Cat-3 anti-dup)` rule.  Cronaca canonical ticket-home per AGENTS.md \u00a7honest-discipline.
