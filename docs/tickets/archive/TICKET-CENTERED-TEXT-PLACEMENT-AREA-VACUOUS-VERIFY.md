# TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY — Vacuous-truth audit for sub-chore (c) Blocco 5.2

| ID            | TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY                                                                       |
|---------------|------------------------------------------------------------------------------------------------------------------------------|
| Status        | **DONE** 2026-07-14 (vacuous-truth audit chaser-chore, Cat-5 3-doc atomic per AGENTS.md)                                       |
| Parent        | [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (sub-chore (c)) |
| Asset class   | docs discipline choreography (ZERO source modification; Cat-3 anti-dup vacuous-truth audit)                                       |
| Surface       | `docs/tickets/TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY.md` (NEW canonical cronaca home)                                  |

## Stato: DONE (vacuous-truth finding)

## Contesto (§honest-discipline pre-state)

Sub-chore (c) `CONTENT-TEXT-PLACEMENT-AREA migration` del [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (catena Blocco 5.2 forward-point execution) richiedeva la migration di:
- 3 inline `centered_text({...})` callsites (A.7 multisource + C.1 box alignment + F.1 cache invalidation)
- 4 helper-uses via `make_centered_text_comp(opts)` (A.1-A.6 + A.8 + B.1-B.2 + C.1 + F.1)

→ `TextDefinition` direct construction (preservare byte-equivalent output per `tests/deterministic/test_visual_regression_scenarios.cpp` CenterTextOptions ↔ TextDefinition regression lock).

## §HONEST-discipline vacuous-truth finding

Il file `content/text_placement/text_placement_compositions.cpp` ha GIÀ eseguito questa migration **prima della deprecation period**, via AGENTS.md M1.8 §2D / `TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS (2026-07-10)` lineage (cronologia chiusura lineage in canonical ticket-home `TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN.md`).

Evidenza verbatim dal file header (linee 36-46):
```
//   - 4 helper functions (make_centered_text_comp, make_1080p_centered,
//     make_clipping_comp, make_multires) refactored to compute the
//     canonical TextDefinition from CenterTextOptions internally;
//     the 9 call sites passing CenterTextOptions unchanged.
//   - 3 inline `centered_text({...})` call sites (A.7 multisource,
//     C.1 box alignment, F.1 cache invalidation) replaced with
//     `TextDefinition{}` directly.
//   - The legacy `centered_text()` wrapper is no longer invoked
//     anywhere in this file (gate [2/4] satisfied).
```

### macchina-verifica rigorous (this session)

| Pattern | Code-only callsites | Comment-only references | Vacuous? |
|---|---|---|---|
| `centered_text(` | 0 | 8 (header + historical notes) | YES |
| `glow_text(` | 0 | 0 (file has no glow_text refs) | YES |
| `make_centered_text_comp(` | 4 (calls to helper, not legacy) | 0 | HELPER (not legacy) |
| `options_to_definition(` | 2 (helper definition + usage) | 0 | CANONICAL |

### Migration Mapping Verbatim (from `options_to_definition` helper, linee 82-110)

1:1 field mapping from `CenterTextOptions` (helper input type) → `TextDefinition` (canonical F2.A DTO):

- `.content = {.value = opts.text}` ← `opts.text`
- `.style.font = {font_path, font_family, font_weight, font_style, font_size}` ← `opts.font_*` (5 fields)
- `.style.color = opts.color`
- `.frame.size = opts.box` (default {1200, 240})
- `.frame.placement = TextPlacement{TextPlacementKind::Absolute, opts.pos}` (default {0,0,0})
- `.frame.anchor = TextAnchor::Center`
- `.frame.align = TextAlign::Center`
- `.frame.vertical_align = VerticalAlign::Middle`
- `.frame.wrap = TextWrap::Word`
- `.frame.overflow = TextOverflow::Clip`
- `.frame.centering_mode = TextCenteringMode::PixelInk`
- `.frame.line_height`, `.frame.tracking`, `.frame.auto_fit`, `.frame.min_font_size`, `.frame.max_font_size`, `.frame.max_lines` ← `opts.*`

Mapping is **byte-equivalent** to deprecated `centered_text()` body output (`content/text/text_helpers_centered.hpp` was the legacy reference, ora assente). Per AGENTS.md §honest-discipline this done-state warrants explicit closure dei cronaca (no churn duplicato).

## Soluzione applicata (vacuous-truth closure, Cat-5 3-doc atomic)

Per AGENTS.md §honest-discipline + precedent vacuous-truth catena (TICKET-SHAPEDGLYPHLINE-P2-23-VERIFICATION + TICKET-PIPE-EXPORT-SESSION-P2-24-VERIFICATION + TICKET-CONTENT-TEXT-CAMERA-V1-ROT-PHASE-2-AUDIT catena — 5 chaser tickets precedent questo lineage):
1. macchina-verifica rigida (rg code-only filter) per confermare vacuous
2. NEW cronaca home chaser-ticket (this file): preserve the cronaca ext per Cat-3 anti-dup disciplin
3. Cat-5 3-doc atomic update: 1 NEW chaser + 3 EDIT canonicals (CHANGELOG + FOLLOWUP_TICKETS + parent bulk-migration ticket Forward-points (c) → DONE)
4. ZERO source modification (cat-3 minimal-surface)
5. macchina-verifica post-update: parent bulk-migration ticket §Forward-points (c) status aggiornato per catena-discovery

## Vincoli (ottemperati)

- ZERO source modification (no editing of `content/text_placement/text_placement_compositions.cpp`)
- ZERO new SDK API surface in `include/chronon3d/`
- ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere preserved)
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 deny-everywhere preserved)
- Cat-3 anti-dup: cronaca ext lives in canonical ticket-home (this file); CHANGELOG + FOLLOWUP_TICKETS = cite-only + cross-link pointer
- Cat-2 freeze: NON-NEEDED (chore is pure docs; no SDK API touched)
- macchina-verifica: pre-existing migration ritenuta cat-equivalent Strict discipline preserved

## Forward-points

(*closed per Cronologia Chiusura (c)* — sub-chore move-forward a (d)/(e)/(f)/(g)/(h) ancora OPEN)

## Cronologia chiusura (per SHA cite-pattern AGENTS.md §Regole di lint documentale)

- Parent chaser-chore precedent (this lineage 2026-07-14): [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (this session, NEW canonical catena opener) — commit `<this-session-chore-hash>` su origin/main
- Migrated file: `content/text_placement/text_placement_compositions.cpp` (ZERO source modified this session — line annotations pre-existing M1.8 §2D lineage)
- Sibling chaser-ticket precedent (vacuous-truth + audit chaser pattern, this conversation): [TICKET-VCPKG-ACTUAL-VPS-CLOSURE](TICKET-VCPKG-ACTUAL-VPS-CLOSURE.md) + [TICKET-SHAPEDGLYPHLINE-P2-23-VERIFICATION](TICKET-SHAPEDGLYPHLINE-P2-23-VERIFICATION.md) + [TICKET-PIPE-EXPORT-SESSION-P2-24-VERIFICATION](TICKET-PIPE-EXPORT-SESSION-P2-24-VERIFICATION.md) + [TICKET-CONTENT-TEXT-CAMERA-V1-ROT-PHASE-2-AUDIT](TICKET-CONTENT-TEXT-CAMERA-V1-ROT-PHASE-2-AUDIT.md) + [TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL](TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL.md) + [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL](TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL.md)
- AGENTS.md governance rules invoked: `§honest-discipline` (vacuous-truth MUST be documented) + `§### Code review feedback discipline` (post-critic fix) + `§Cat-3 anti-dup` (cronaca NOT in canonical docs) + `§### Docs canonical update discipline rule` (Cat-5 3-doc discipline chore framing)

## Lessons learned (per Cat-5 chaser-chore precedent lineage)

Per future agenti:
- **`git pull --rebase` pre-check obbligatorio** per divergence resolution (`branch.main.rebase = true` già config)
- **macchina-verifica rigorosa con comment-filter** prima di aprire un chaser-ticket vacuous (rg -v '//\|/\*\|\*' o equivalent)
- **Cat-5 3-doc discipline** preserves cronaca ext + lightness canonical: 1 NEW chaser-home + 3 EDIT canonicals + ZERO source
- **Per-AREA ordering** per Chore B precedent track: examples → common → text-placement (DONE this session) → certification → other → tests-deterministic → tests-text → scene → helper-removal-final
