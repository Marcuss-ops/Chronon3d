# TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION — Source code migration of 9 centered_text( callers in test_visual_regression_scenarios.cpp (Blocco 5.2 sub-chore (f))

| ID            | TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION                                                                                                                  |
|---------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Status        | **OPEN** (P2, future chore — byte-equivalence regression lock preservation obbligatorio)                                                                                   |
| Parent        | [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT.md) (audit chaser-ticket-home, this catena) |
| Asset class   | source code refactor (cat-3 minimal-surface test file migration, Chore B precedent)                                                                                       |
| Scope         | 9 callers mapped from `centered_text(CenterTextOptions)` → `TextDefinition{}` direct construction in `tests/deterministic/test_visual_regression_scenarios.cpp`              |
| Surface       | `tests/deterministic/test_visual_regression_scenarios.cpp` (526 LoC, 9 code-only `centered_text(` callers at L218, L276, L297, L324, L344, L372, L506, L509 + 1 comment L10) |

## Stato: OPEN (forward-point registered from audit chaser-ticket; future-chore execution)

## Problema

L'audit chaser-ticket `TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT` (this catena, 2026-07-14) ha confermato che il file `tests/deterministic/test_visual_regression_scenarios.cpp` contiene 9 chiamanti reali a `centered_text(CenterTextOptions)` che NON sono stati pre-migrati in M1.8 §2D. La catena-precedent (c)/(d)/(e)/(i) tutti vacuous diverge da (f) che è NON-VACUOUS. La migrazione source è forward-pointed a questo ticket per rispettare cat-3 minimal-surface + byte-equivalence regression lock preservation.

### User-spec verbatim (audit origin)

> Open sub-chore (f) TESTS-DETERMINISTIC-AREA Blocco 5.2 (~5 callsites in tests/deterministic/test_visual_regression_scenarios.cpp + tests/text/test_visual_regression_scenarios.cpp). macchina-verifica: rg -c 'centered_text\(|glow_text\(|\bcentered_text\b' tests/deterministic/ + tests/text/test_visual_regression_scenarios.cpp = 0. **CRITICAL: regression lock preservation obbligatorio (CenterTextOptions ↔ TextDefinition byte-equivalence test predecessore).** Per catena-precedent (c)/(d)/(e) all vacuous, expect (f) also vacuous — verify via trinity-probe pattern.

Trinity-probe revealed 9 callers (vs 5 user-spec estimate) + 1 FILE_NOT_FOUND second file. Real source migration is required; this ticket is the forward-point from the audit chaser-ticket.

## Soluzione accettabile

Mapping canonico 1:1 `centered_text(CenterTextOptions) → TextDefinition{...}` direct construction (preserva byte-equivalent output):

| CenterTextOptions field | TextDefinition field | Note |
|---|---|---|
| `CenterTextOptions.text` | `TextDefinition.content` | direct |
| `CenterTextOptions.font_size` | `TextDefinition.style.font.size` | direct |
| `CenterTextOptions.color` | `TextDefinition.style.color` | direct |
| `CenterTextOptions.pin_to(Anchor::Center)` (via opts) | `TextDefinition.frame.{size,anchor,align,...}` | structural |
| `CenterTextOptions.background/shadow/glow` (effect opts) | `TextDefinition.style.{paint,shadows,box_style,...}` | structural (effects integrated) |

**Byte-equivalence regression lock tie**: Mapping MUST produce byte-equivalent output such that `kRefVR*` sentinel hashes are preserved (no semantic regression; pixel-perfect identity).

### Concrete worked example (one canonical replacement)

Before (L276-278 verbatim):
```cpp
auto spec = centered_text(make_opts("STROKE", 96.0f, Color::black()));
```

After (canonical `TextDefinition{}` direct construction):
```cpp
auto spec = TextDefinition{
    .content = "STROKE",
    .style = {
        .font = {.size = 96.0f},
        .color = Color::black(),
        .stroke = {/* stroke params derived from make_opts() */},
    },
    .frame = {/* center-anchor frame from pin_to(Anchor::Center) */},
};
```

**Multi-line call handling (L506 + L509)**: L506 `centered_text(make_opts("tiny", 8.0f, Color{...}, ...))` spans L506-L508 (trailing comma + closing paren on subsequent line); L509 `centered_text(make_opts("HUGE", 220.0f, Color{...}, ...))` spans L509-L511. Reconstruction MUST keep field order + default values byte-identical to the multi-line source (no field reordering, no default-arg elision, no implicit conversions).

Mapping table + concrete example + multi-line handling reproduced here for migration self-containment (canonical source = audit chaser-ticket §CRITICAL).

## Vincoli (CRITICAL)

- **CRITICAL: byte-equivalence regression lock preservation obbligatorio** (kRefVR* sentinel hashes MUST remain byte-equivalent post-migration). La trasformazione DEVE produrre output pixel-perfect identico alla versione `centered_text(opt)` attuale. `VR_GATE(short_label, kRef*, m)` macro captures sentinel hashes at first-run; any drift = regression test failure.
- Cat-2 freeze: source REMOVAL (rimozione 9 chiamanti `centered_text(`) NON richiede ADR (vs addizione). Sostituzione 1:1 = contrazione surface = consentita post-freeze (REVOCATO 2026-07-06).
- Cat-3 minimal-surface: ZERO new SDK API + ZERO nuovi singleton/registry/resolver/cache + ZERO Gate 5 deny-everywhere violationi
- AGENTS.md §Fare PR piccole e mirate: atomic commit scope = 1 file (the test file) + ZERO altri source touched
- macchina-verifica: `ctest -R VisualRegression` DEFERRED-WBH per VPS vcpkg glm/magic_enum env-block (TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV pattern)

## Criteri di accettazione

- [ ] Apply 9 `centered_text(CenterTextOptions)` → `TextDefinition{...}` mapping in `tests/deterministic/test_visual_regression_scenarios.cpp` (L218, L276, L297, L324, L344, L372, L506, L509, + L10 comment update)
- [ ] macchina-verifica: `rg -c 'centered_text\(' tests/deterministic/test_visual_regression_scenarios.cpp` returns 0 (or 1 for the L10 comment-only header)
- [ ] macchina-verifica: `rg -c 'TextDefinition' tests/deterministic/test_visual_regression_scenarios.cpp` returns 9+ (all 9 migrated callsites use canonical)
- [ ] macchina-verifica: `rg -c 'centered_text\(' tests/` returns 0
- [ ] macchina-verifica: `ctest -R VisualRegression` exit 0 + kRefVR* sentinel hashes byte-equivalent (DEFERRED-WBH per vcpkg env-block)
- [ ] regression lock: kRefVR* hashes NON cambiano (preserved byte-equivalence)
- [ ] ZERO new SDK API surface
- [ ] ZERO new singleton/registry/resolver/cache
- [ ] ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 deny-everywhere preserved)
- [ ] Subject envelope ≤72 chars per `tools/check_commit_subject_length.sh`
- [ ] SHA-triple equality verify post-push (per AGENTS.md §Post-push invariant)

## Forward-points

| # | Status | Description |
|---|--------|-------------|
| (f)-migration-execute | OPEN (P2) | Esecuzione della migrazione source 9 callsites con byte-equivalence regression lock preservation. Subject envelope proposed per future chore: `chore(text): migrate sub-chore (f) tests-deterministic 9 callers` (~58 chars) |
| (f)-ctest-verify | OPEN (P2) | ctest -R VisualRegression real-green-PASS post-migration su workstation pulita (vcpkg glm/magic_enum installato). DEFERRED-WBH per VPS vcpkg env-block. |

## Cross-link canonical

- **Forward-point origin**: [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT.md) (audit chaser-ticket-home, this catena — documents 9 callers + per-call evidence + catena-precedent divergence framing)
- **Parent**: [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (parent bulk-migration catena, §Forward-points (f) row → DONE (audit) + future-migration forward-point)
- **Mapping target**: [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN, helper rimosso vacuous pre-session per sub-chore (i))
- **Phase 3 forward-point** (after (f)-migration DONE): `centered_text` function itself can be removed from `content/text/text_helpers_centered.hpp` per TICKET-CENTERED-TEXT-MIGRATION §Criteri (Phase 3 overload REMOVAL + ABI-stability ADR per cat-2 freeze). The `CenterTextOptions` struct itself is also legacy-deprecated; same Phase 3 removal scope.

## AGENTS.md governance rules applicate

- §Cat-2 freeze (source REMOVAL = no ADR needed; source ADDITION canonical-replacement = no new SDK surface)
- §Cat-3 minimal-surface (1 file EDIT, ZERO new SDK API, ZERO new singleton/registry/resolver/cache)
- §honest-discipline (audit cronaca ext, future-migration forward-pointed, no premature migration in audit chore)
- §Docs canonical update discipline (cat-5 3-doc discipline; this ticket is forward-point from audit chore per (e) cleanup-chore rider-4 pattern)
- §Fare PR piccole e mirate (atomic commit scope = 1 test file + ZERO altri source)
- §Regole di lint documentale (canonico-fence FOLLOWUP_TICKETS row + CHANGELOG cite-only + subject envelope ≤72)
- §Post-push SHA-selfcheck invariant (SHA-triple verify post-push; per (b589fdba) + (21ece2b3) catena precedent)
- §GATE-MNT-01 closure lineage (per-branch rebase + tools/wrap_push.sh + tools/check_main_clean.sh triad)
- §Per-branch rebase convention (read-side, branch.main.rebase = true)
- §Test binary staleness check (per AGENTS.md §Test binary staleness: verify binary exists + is fresh before ctest invocation, prevents stale-build false-negative)
