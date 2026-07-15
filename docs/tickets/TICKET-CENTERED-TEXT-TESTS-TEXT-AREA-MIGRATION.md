# TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION — Source code migration of 25 centered_text/glow_text callers in test_text_simplicity_adapters.cpp (Blocco 5.2 sub-chore (g))

| ID            | TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION                                                                                                                  |
|---------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Status        | **OPEN** (P2, future chore — byte-equivalence regression lock preservation obbligatorio)                                                                            |
| Parent        | [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT.md) (audit chaser-ticket-home, this catena) |
| Asset class   | source code refactor (cat-3 minimal-surface test file migration, Chore B precedent)                                                                              |
| Scope         | 25 callers mapped from `centered_text(CenterTextOptions)` + `glow_text(...)` → `TextDefinition{}` direct construction in `tests/text/test_text_simplicity_adapters.cpp` |
| Surface       | `tests/text/test_text_simplicity_adapters.cpp` (665 LoC, 18 code-only `centered_text(` callers + 7 code-only `glow_text(` callers)                            |

## Stato: OPEN (forward-point registered from audit chaser-ticket; future-chore execution)

## Problema

L'audit chaser-ticket `TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT` (this catena, 2026-07-15) ha confermato che il file `tests/text/test_text_simplicity_adapters.cpp` contiene 25 chiamanti reali a `centered_text(CenterTextOptions)` (18) e `glow_text(...)` (7) che NON sono stati pre-migrati in M1.8 §2D. La catena-precedent (a)/(c)/(d)/(e)/(i) tutti vacuous diverge parzialmente da (g): 3 of 4 user-spec files sono vacuous, ma `test_text_simplicity_adapters.cpp` ha 25 chiamanti reali. La migrazione source è forward-pointed a questo ticket per rispettare cat-3 minimal-surface + byte-equivalence regression lock preservation.

### User-spec verbatim (audit origin)

> Open sub-chore (g) TESTS-TEXT-AREA Blocco 5.2 (~30 callsites in tests/text/test_text_simplicity_adapters.cpp + tests/certification/test_text_production_v1.cpp + tests/certification/test_cert_text_bbox.cpp + tests/architecture/test_text_definition_round_trip.cpp). macchina-verifica: rg -c 'centered_text\(|glow_text\(' across 4 test files = 0. **CRITICAL: byte-equivalence regression lock preservation obbligatorio (CenterTextOptions ↔ TextDefinition test predecessore in test_visual_regression_scenarios.cpp).** Per catena-precedent (a)/(c)/(d)/(e) all vacuous, expect (g) also vacuous — verify via trinity-probe.

Trinity-probe revealed 25 callers in test_text_simplicity_adapters.cpp (vs ~30 user-spec estimate) + 0 callers in the other 3 files. Real source migration is required; this ticket is the forward-point from the audit chaser-ticket.

## Soluzione accettabile

Mapping canonico 1:1 `centered_text(CenterTextOptions) + glow_text(...)` → `TextDefinition{}` direct construction (preserva byte-equivalent output):

| Legacy API field | TextDefinition field | Note |
|---|---|---|
| `centered_text(opt).text` | `TextDefinition.content` | direct |
| `centered_text(opt).font_size` | `TextDefinition.style.font.size` | direct |
| `centered_text(opt).color` | `TextDefinition.style.color` | direct |
| `centered_text(opt).pin_to(Anchor::Center)` (via opts) | `TextDefinition.frame.{size,anchor,align,...}` | structural |
| `glow_text(opt).glow_color` | `TextDefinition.style.box_style.glow_color` | direct |
| `glow_text(opt).radius` | `TextDefinition.style.box_style.glow_radius` | direct |
| `glow_text(opt).intensity` | `TextDefinition.style.box_style.glow_intensity` | direct |

### Concrete worked examples (canonical `TextDefinition{}` direct construction, mirroring (f) sibling migration ticket)

#### Example 1: `centered_text(opt)` → `TextDefinition{}` (e.g., L145 + L156)

**Before (L145-L156 verbatim, deprecation-warning capture pattern)**:
```cpp
const auto opt = make_opts("HELLO", 96.0f, Color::white());
auto spec = centered_text(opt);  // [[deprecated]] warning captured by WarnCaptureGuard
REQUIRE(capture.has_warned());
```

**After (canonical `TextDefinition{}` direct construction, byte-equivalent output)**:
```cpp
const auto opt = make_opts("HELLO", 96.0f, Color::white());
auto spec = TextDefinition{
    .content = "HELLO",
    .style = {
        .font = {.size = 96.0f},
        .color = Color::white(),
    },
    .frame = {
        // pin_to(Anchor::Center) from CenterTextOptions frame
        .size  = {kVW * 0.85f, kVH * 0.85f},
        .align = TextAlignment::Center,
    },
};
// No [[deprecated]] warning (TextDefinition is canonical); capture.has_warned() = false
```

**Byte-equivalence tie**: `spec` produced by `centered_text(opt)` and `TextDefinition{...}` direct construction MUST produce identical sentinel hash `VR/<scenario>` in test_visual_regression_scenarios.cpp regression lock.

#### Example 2: `glow_text(opt, color, radius, intensity)` → `TextDefinition{}` + `style.box_style.glow_*` (e.g., L73 + L88)

**Before (L73-L88 verbatim, glow consolidation pattern)**:
```cpp
const auto opt = make_opts("GLOW", 96.0f, Color::white());
auto spec = glow_text(opt, Color{1.0f, 0.55f, 0.18f, 1.0f}, 24.0f, 0.85f);
```

**After (canonical `TextDefinition{}` + `style.box_style.glow_*` direct construction)**:
```cpp
const auto opt = make_opts("GLOW", 96.0f, Color::white());
auto spec = TextDefinition{
    .content = "GLOW",
    .style = {
        .font = {.size = 96.0f},
        .color = Color::white(),
        .box_style = {
            .glow_color     = Color{1.0f, 0.55f, 0.18f, 1.0f},
            .glow_radius    = 24.0f,
            .glow_intensity = 0.85f,
        },
    },
    .frame = {
        .size  = {kVW * 0.85f, kVH * 0.85f},
        .align = TextAlignment::Center,
    },
};
```

**Byte-equivalence tie**: `spec` produced by `glow_text(opt, color, radius, intensity)` and `TextDefinition{...}` + `style.box_style.glow_*` direct construction MUST produce identical sentinel hash.

#### Multi-line call handling (L596, L619 old-vs-new API patterns)

The old-vs-new API TEST_CASE blocks (L596 + L619) compare the deprecated API output to the new API output (pixel-hash equivalence test). After migration, the comparison shifts from `centered_text(opt)` vs `TextDefinition{...}` to `TextDefinition{...}` (canonical) vs `TextDefinition{...}` (canonical) — the TEST_CASE body MUST be updated to compare two canonical instantiations of the SAME `TextDefinition{...}` shape. The deprecation-warning capture (`WarnCaptureGuard.has_warned()`) MUST be removed (no deprecation warning on canonical).

Mapping table + 2 worked examples + multi-line handling reproduced here for migration self-containment (canonical source = audit chaser-ticket §CRITICAL).


**Byte-equivalence regression lock tie**: Mapping MUST produce byte-equivalent output such that the deprecation-warning capture pattern (WarnCaptureGuard/spdlog::warn) AND the pixel-hash equivalence test pattern produce identical sentinel values post-migration.

## Per-call evidence (line-numbered, basher-verified)

The 25 callers in `test_text_simplicity_adapters.cpp` (665 LoC) are spread across the file. Specific line numbers documented in audit chaser-ticket §PROBE-1. The 18 `centered_text(` callers + 7 `glow_text(` callers cover the full surface of the file's CenterTextOptions usage.

## Vincoli (CRITICAL)

- **CRITICAL: byte-equivalence regression lock preservation obbligatorio**. The 15 kRefVR* sentinel hashes in test_visual_regression_scenarios.cpp (kRefVRStatic + kRefVRMultiline + kRefVRTracking + kRefVRStroke + kRefVRGradient + kRefVRShadow + kRefVRGlow + kRefVRBlur + kRefVRTypewriter + kRefVRAnimGlyph + kRefVRAnimWord + kRefVRRTL + kRefVRCJK + kRefVREmojiFallback + kRefVRScaleExtreme) MUST remain byte-equivalent post-migration. La trasformazione DEVE produrre output pixel-perfect identico alla versione `centered_text(opt)` + `glow_text(opt)` attuale. `VR_GATE(short_label, kRef*, m)` macro captures sentinel hashes at first-run; any drift = regression test failure.
- Cat-2 freeze: source REMOVAL (rimozione 25 chiamanti `centered_text(` + `glow_text(`) NON richiede ADR (vs addizione). Sostituzione 1:1 = contrazione surface = consentita post-freeze (REVOCATO 2026-07-06).
- Cat-3 minimal-surface: ZERO new SDK API + ZERO nuovi singleton/registry/resolver/cache + ZERO Gate 5 deny-everywhere violationi
- AGENTS.md §Fare PR piccole e mirate: atomic commit scope = 1 file (the test file) + ZERO altri source touched
- macchina-verifica: `ctest -R test_text_simplicity_adapters` + `ctest -R VisualRegression` DEFERRED-WBH per VPS vcpkg glm/magic_enum env-block (TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV pattern)

## Criteri di accettazione

- [ ] Apply 18 `centered_text(CenterTextOptions)` → `TextDefinition{...}` mapping in `tests/text/test_text_simplicity_adapters.cpp`
- [ ] Apply 7 `glow_text(...)` → `TextDefinition{...}` mapping in `tests/text/test_text_simplicity_adapters.cpp`
- [ ] macchina-verifica: `rg -c 'centered_text\(' tests/text/test_text_simplicity_adapters.cpp` returns 0
- [ ] macchina-verifica: `rg -c 'glow_text\(' tests/text/test_text_simplicity_adapters.cpp` returns 0
- [ ] macchina-verifica: `rg -c 'TextDefinition' tests/text/test_text_simplicity_adapters.cpp` returns 25+
- [ ] macchina-verifica: `rg -c 'centered_text\(' tests/` returns 0
- [ ] macchina-verifica: `rg -c 'glow_text\(' tests/` returns 0
- [ ] macchina-verifica: `ctest -R test_text_simplicity_adapters` exit 0 (DEFERRED-WBH per vcpkg env-block)
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
| (g)-migration-execute | OPEN (P2) | Esecuzione della migrazione source 25 callsites con byte-equivalence regression lock preservation. Subject envelope proposed per future chore: `chore(text): migrate sub-chore (g) tests-text-area 25 callers` (~60 chars) |
| (g)-ctest-verify | OPEN (P2) | ctest -R test_text_simplicity_adapters + ctest -R VisualRegression real-green-PASS post-migration su workstation pulita (vcpkg glm/magic_enum installato). DEFERRED-WBH per VPS vcpkg env-block. |

## Cross-link canonical

- **Forward-point origin**: [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT.md) (audit chaser-ticket-home, this catena — documents 25 callers + per-call evidence + catena-precedent partial-divergence framing)
- **Sibling NON-VACUOUS migration forward-point**: [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION.md) ((f) sibling migration ticket, 9 callers, future chore)
- **Parent**: [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (parent bulk-migration catena, §Forward-points (g) row → DONE (audit) + (g)-migration forward-point)
- **Mapping target**: [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN, helper rimosso vacuous pre-session per sub-chore (i))
- **Phase 3 forward-point** (after (g)-migration DONE): `centered_text` function + `glow_text` function can be removed from `content/text/text_helpers_centered.hpp` per TICKET-CENTERED-TEXT-MIGRATION §Criteri (Phase 3 overload REMOVAL + ABI-stability ADR per cat-2 freeze). The `CenterTextOptions` struct itself is also legacy-deprecated; same Phase 3 removal scope.

## AGENTS.md governance rules applicate

- §Cat-2 freeze (source REMOVAL = no ADR needed; source ADDITION canonical-replacement = no new SDK surface)
- §Cat-3 minimal-surface (1 file EDIT, ZERO new SDK API, ZERO new singleton/registry/resolver/cache)
- §honest-discipline (audit cronaca ext, future-migration forward-pointed, no premature migration in audit chore)
- §Docs canonical update discipline (cat-5 3-doc discipline; this ticket is forward-point from audit chore per (f) precedent pattern)
- §Fare PR piccole e mirate (atomic commit scope = 1 test file + ZERO altri source)
- §Regole di lint documentale (canonico-fence FOLLOWUP_TICKETS row + CHANGELOG cite-only + subject envelope ≤72)
- §Post-push SHA-selfcheck invariant (SHA-triple verify post-push; per (b589fdba) + (21ece2b3) catena precedent)
- §GATE-MNT-01 closure lineage (per-branch rebase + tools/wrap_push.sh + tools/check_main_clean.sh triad)
- §Per-branch rebase convention (read-side, branch.main.rebase = true)
- §Test binary staleness check (per AGENTS.md §Test binary staleness: verify binary exists + is fresh before ctest invocation, prevents stale-build false-negative)
