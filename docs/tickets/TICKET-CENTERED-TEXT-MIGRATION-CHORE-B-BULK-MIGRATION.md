# TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION — Per-AREA bulk migration of centered_text/glow_text to TextDefinition (Blocco 5.2 execution)

| ID            | TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION                                                                                                                  |
|---------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Status        | **OPEN** (P2, Blocco 5.2 forward-point execution catena)                                                                                                                |
| Parent        | [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN, Blocco 5.2 forward-point canonical) — this ticket opens the bulk-migration execution catena |
| Asset class   | source code refactor (helper removal post-deprecation cat-3 minimal-surface per-AREA incremental, Chore B precedent)                                                     |
| Scope         | 100+ callers mapped to TextDefinition canonical form, per-AREA bounded migration, helper removal final catena                                                          |
| Surface       | `include/chronon3d/text/text_helpers_centered.hpp` (SDK public header — `centered_text()` + `compute_single_line_glyph_layout()`) + `content/text/text_glow_helpers.hpp` (`glow_text()`) + 12+ callers files (content/examples + content/common + content/text_placement + content/certification + content/text_reveal + src/scene + tests/{deterministic,text,certification,architecture}) |

## Stato: OPEN (Blocco 5.2 forward-point execution catena aperta)

## Problema (§honest pre-state inventory)

`centered_text(CenterTextOptions)` e `glow_text(CenterTextOptions, Color, float, float)` sono stati marcati `[[deprecated]]` in Blocco 5.1 (commits su origin/main `b6397b90` + `74c924b9` + `bacbfc5a` + `cc3ad1a3`). La deprecation bridge è ora completa:
- `centered_text()` emette compile warning (silent rot pre-Blocco-5.1; ora fail-loud rot risolto in this lineage)
- `glow_text()` emette compile warning con `[[deprecated]]` aggiornato (no routing cycle verso centered_text)

Il forward-point ticket parent `TICKET-CENTERED-TEXT-MIGRATION.md` (P2 OPEN) definisce i criteri di accettazione canonici per Phase 2 (Blocco 5.2):
- `0 reference a centered_text in production source`
- `0 reference a glow_text in production source`
- `0 reference a centered_text in test source`
- `0 reference a glow_text in test source`
- `centered_text() helper rimosso da include/chronon3d/text/text_helpers_centered.hpp`
- `glow_text() helper rimosso da content/text/text_glow_helpers.hpp`

I ~100+ callers sono distribuiti su 12+ file in 6 macro-aree. La full migration in 1 atomic chore viola `AGENTS.md §"Fare PR piccole e mirate, senza mescolare refactor indipendenti"` + aggredisce cat-3 minimal-surface + aprerebbe push-window rot su 100+ files simultanei. Il pattern `TICKET-COMPOSITIONDESCRIPTOR-MIGRATION-CHORE-B-BULK-MIGRATION` precedent ha dimostrato per-AREA Chore B incremental come canonico AGENTS.md path per bulk migration di helper pubblici.

## Soluzione accettabile (per-AREA Chore B precedent mapping, 7+1 sub-chori)

Mapping canonico 1:1 `centered_text(CenterTextOptions) → TextDefinition{...}` direct construction (preserva byte-identical output):
- `CenterTextOptions.content` → `TextDefinition.content`
- `CenterTextOptions.font_size` → `TextDefinition.style.font.size`
- `CenterTextOptions.color` → `TextDefinition.style.color`
- `CenterTextOptions.anchor` + `pin_to(Anchor::Center)` → `TextDefinition.frame.{size,anchor,align,...}`
- `CenterTextOptions.background` / `shadow` / `glow` → `TextDefinition.style.{paint,shadows,box_style,...}`

Mapping `glow_text(CenterTextOptions, glow_color, radius, intensity) → TextDefinition` + `ChrononGlowFinalAE` material glow fields (or `style.box_style.glow_*` per canonical):
- `glow_color` → `TextDefinition.style.box_style.glow_color`
- `radius` → `TextDefinition.style.box_style.glow_radius`
- `intensity` → `TextDefinition.style.box_style.glow_intensity`

## Vincoli (cat-2 freeze + cat-3 anti-dup + Gate 5 deny-everywhere)

- `AGENTS.md §"Fare PR piccole e mirate, senza mescolare refactor indipendenti"`: per-AREA bounded sub-chori (10 forward-points), OGNUNO atomic Cat-3 minimal-surface scope
- `AGENTS.md cat-2 freeze`: rimozione helper pubblico dal SDK NON richiede ADR (vs addizione che richiede ADR). Rimozione = contrazione SDK surface = consentita post-freeze (REVOCATO 2026-07-06)
- `AGENTS.md Cat-3 anti-dup`: ogni sub-chore ZERO nuovi SDK API + ZERO nuovi singleton/registry/resolver/cache + Cronaca estesa lives in canonical ticket-home (per Cat-3 anti-dup non duplico cronaca in canonici + CHANGELOG/FOLLOWUP)
- `AGENTS.md Gate 5 deny-everywhere`: zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` per ogni sub-chore
- `AGENTS.md` codifica meccanica "same-content identity": output `text(name, TextDefinition)` deve essere output byte-equivalent a `centered_text()` per OGNI callsite (no functional regression; visual regression lock in `tests/deterministic/test_visual_regression_scenarios.cpp` deve preservare centerline pixel-perfect)
- Parent ticket `TICKET-CENTERED-TEXT-MIGRATION.md` Phase 2 forward-point: helper rimosso SOLO DOPO che TUTTE le sub-chori sono macchina-verificate verde (`bash tools/wrap_push.sh origin main` push-range 11/11 per sub-chore)

## Criteri di accettazione (verifiable machine-verification)

- [ ] **Per-AREA inventory basato-su fatti**: `rg -c 'centered_text\(' src/ include/ content/ tests/ apps/` ritorna distribuzione categorized per-file
- [ ] **Sub-chore Blocco 5.2.A: CONTENT-EXAMPLES-AREA** DONE — `content/examples/light/light_text_animations.cpp` migration
- [ ] **Sub-chore Blocco 5.2.B: CONTENT-COMMON-AREA** DONE — `content/common/animation_helpers.hpp` migration + TODO comment cleanup
- [x] **Sub-chore Blocco 5.2.C: CONTENT-TEXT-PLACEMENT-AREA** DONE (vacuous-truth, 2026-07-14) — `content/text_placement/text_placement_compositions.cpp` migration pre-completed via M1.8 §2D / TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS (cronologia chiusura: [TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY.md)).

- [ ] **Sub-chore Blocco 5.2.D: CONTENT-CERTIFICATION-AREA** DONE — `content/certification/cert_*.cpp` (multilingual + lower_third + long_text + title) 16 callsites migration
- [ ] **Sub-chore Blocco 5.2.D: CONTENT-CERTIFICATION-AREA** DONE — `content/certification/cert_*.cpp` (multilingual + lower_third + long_text + title) 16 callsites migration
- [ ] **Sub-chore Blocco 5.2.E: CONTENT-OTHER-AREA** DONE — `content/text/text_reveal.cpp` + `content/text/text_glow_helpers.hpp` helper removal phase
- [ ] **Sub-chore Blocco 5.2.F: TESTS-DETERMINISTIC-AREA** DONE — `tests/deterministic/test_visual_regression_scenarios.cpp` 5 callsites + `tests/text/test_visual_regression_scenarios.cpp` migration (regression lock preservation CRITICAL)
- [ ] **Sub-chore Blocco 5.2.G: TESTS-TEXT-AREA** DONE — ~30 callsites test_text_simplicity_adapters + test_text_production_v1 + test_cert_text_bbox + test_text_definition_round_trip migration
- [ ] **Sub-chore Blocco 5.2.H: CONTENT-SCENE-AREA** DONE — `src/scene/builders/layer_builder_text.cpp` + camera overlay panels (CenterTextOptions indirect ~20 references) audit + migration
- [x] **Sub-chore Blocco 5.2.I-FINAL: HELPER-REMOVAL-FINAL** DONE (vacuous-truth, 2026-07-14) — `centered_text()` + `glow_text()` + `compute_single_line_glyph_layout()` rimosse dai headers in pre-session lineage (cronologia chiusura: [TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY.md)). Cat-5 3-doc same-atomic per AGENTS.md §Honest-discipline + vacant-truth chaser-chore precedent (8+ sibling tickets).
- [ ] macchina-verifica finale: `rg -c 'centered_text\(' src/ include/ content/ tests/ apps/` = 0
- [ ] macchina-verifica finale: `rg -c 'glow_text\(' src/ include/ content/ tests/ apps/` = 0
- [ ] macchina-verifica finale: `rg -c 'compute_single_line_glyph_layout\(' src/ include/ content/ tests/ apps/` = 0
- [ ] macchina-verifica finale: `bash tools/wrap_push.sh origin main` push-range 11/11 verde post-Ogni-sub-chore
- [ ] regression lock: `tests/deterministic/test_visual_regression_scenarios.cpp` CenterTextOptions ↔ TextDefinition byte-equivalence test verde post-migration

## Per-AREA inventory (sub-chori forward-point table)

| # | Sub-chore                          | Area                                                                        | Estimated callsites | Status |
|---|------------------------------------|-----------------------------------------------------------------------------|--------------------:|--------|
| a | CONTENT-EXAMPLES-AREA              | `content/examples/light/light_text_animations.cpp`                          |                   1 | OPEN   |
| b | CONTENT-COMMON-AREA                | `content/common/animation_helpers.hpp` (1 forward-decl + 1 TODO impl references) | 2 | OPEN   |
| c | CONTENT-TEXT-PLACEMENT-AREA        | `content/text_placement/text_placement_compositions.cpp` (3 inline + 4 helper) | ~12 | DONE (vacuous, 2026-07-14, M1.8 §2D pre-existing) |
| d | CONTENT-CERTIFICATION-AREA         | `content/certification/cert_*.cpp` (multilingual + lower_third + long_text + title) | ~16 | DONE (vacuous, 2026-07-14, callers already pre-migrated M1.8 §2D) |
| e | CONTENT-OTHER-AREA                 | `content/text/text_reveal.cpp` + `content/text/text_glow_helpers.hpp`       |                  ~5 | OPEN   |
| f | TESTS-DETERMINISTIC-AREA           | `tests/deterministic/test_visual_regression_scenarios.cpp` 5 callsites + `tests/text/test_visual_regression_scenarios.cpp` | ~5 | OPEN |
| g | TESTS-TEXT-AREA                    | `tests/text/test_text_simplicity_adapters.cpp` (~12 callsites) + `tests/certification/test_text_production_v1.cpp` + `tests/certification/test_cert_text_bbox.cpp` + `tests/architecture/test_text_definition_round_trip.cpp` | ~30 | OPEN |
| h | CONTENT-SCENE-AREA                 | `src/scene/builders/layer_builder_text.cpp` + camera overlay panels CenterTextOptions indirect references (~20 panels) | ~20 | OPEN |
| i | HELPER-REMOVAL-FINAL (Blocco 5.2.I) | Rimozione `centered_text()` + `glow_text()` + `compute_single_line_glyph_layout()` da `include/chronon3d/text/text_helpers_centered.hpp` + `content/text/text_glow_helpers.hpp` | 3 functions | DONE (vacuous, 2026-07-14, helpers already absent pre-session + zero callers) |

Total target: ~91 callsites + 3 helper functions. Counts approximated basato-su prior basher-scan (this session, 2026-07-14); precision verification via `rg -c 'centered_text\(|glow_text\(|compute_single_line_glyph_layout\(' src/ include/ content/ tests/ apps/` at start of each sub-chore.

## Forward-points (sub-chori per-AREA Chore B execution queue)

| # | Status | Description |
|---|--------|-------------|
| a | OPEN (P2) | Sub-chore Blocco 5.2.A: CONTENT-EXAMPLES-AREA migration. Cat-3 minimal-surface; ZERO new SDK API. macchina-verifica: `light_text_animations.cpp` non più references centered_text/glow_text. |
| b | OPEN (P2) | Sub-chore Blocco 5.2.B: CONTENT-COMMON-AREA migration. animation_helpers.hpp TODO comment cleanup (`TODO: migrate to centered_text()...` → `TODO: migrate to canonical`). |
| c | DONE (vacuous, 2026-07-14) | Sub-chore Blocco 5.2.C: CONTENT-TEXT-PLACEMENT-AREA migration pre-completed via M1.8 §2D / TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS (cronologia chiusura: [TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY.md)). ZERO source modification this session (cat-3 minimal-surface). NO macchina-verifica additional work needed. |
| d | DONE (vacuous, 2026-07-14) | Sub-chore Blocco 5.2.D: CONTENT-CERTIFICATION-AREA migration pre-completed pre-this-session (cronologia chiusura: [TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY.md)). ZERO source questo turno (cat-3 minimal-surface). 0 code-only callers across 4 cert_*.cpp files post comment-strip filter. NO macchina-verifica additional work needed. |
| e | OPEN (P2) | Sub-chore Blocco 5.2.E: CONTENT-OTHER-AREA migration. text_reveal.cpp + glow_text() helper itself (already partially migrated via text_glow_helpers.hpp:23 cross-link comment, ma ora helper rimosso). |
| f | OPEN (P2) | Sub-chore Blocco 5.2.F: TESTS-DETERMINISTIC-AREA migration. ~5 callsites in test_visual_regression_scenarios.cpp (CenterTextOptions regression lock). **CRITICAL**: regression lock preservation obbligatorio (CenterTextOptions ↔ TextDefinition byte-equivalence test). |
| g | OPEN (P2) | Sub-chore Blocco 5.2.G: TESTS-TEXT-AREA migration. ~~30 callsites distribuiti su 4 test files. Tests highest blast radius (reproducibility regression suite) — execute LAST post-F+K+H verde. |
| h | OPEN (P2) | Sub-chore Blocco 5.2.H: CONTENT-SCENE-AREA migration. ~20 CenterTextOptions indirect references in `src/scene/builders/layer_builder_text.cpp` + camera overlay panels. Audit richiesto: 1 callsite per panel × ~20 panels. Per AGENTS.md "PR piccole e mirate", questo sub-chore può richiedere split per-layer (1 chore per panel-batch). |
| i | DONE (vacuous, 2026-07-14) | Sub-chore Blocco 5.2.I-FINAL: HELPER-REMOVAL-FINAL pre-completed pre-this-session (cronologia chiusura: [TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY.md)). 3 helpers + header file already absent, zero prod callers via rg-probe. ZERO source questo turno (cat-3 minimal-surface). NO macchina-verifica additional work needed. |

## Cross-link canonical (per SHA cite-pattern AGENTS.md §Regole di lint documentale)

- **Parent forward-point**: [TICKET-CENTERED-TEXT-MIGRATION.md](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN, Blocco 5.2 forward-point canonical)
- **Sibling bulk-migration precedent**: [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION-CHORE-B-BULK-MIGRATION.md](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION-CHORE-B-BULK-MIGRATION.md) (CompositionDescriptor Chore B pattern, 200+ callers, per-AREA incremental, 10 sub-chori Chore B precedent)
- **Sibling rotation**: [TICKET-TEXT-SPEC-MIGRATION.md](TICKET-TEXT-SPEC-MIGRATION.md) (P1 OPEN, parallel `text(name, TextSpec)` overload + `from_text_spec()` migration target) — execuzione parallela possibile post-Phase-1-DONE-this-lineage
- **Deprecation origin (Blocco 5.1 commit set on origin/main)**:
  - `b6397b90` chore(text): deprecate centered_text + glow_text (Blocco 5.1)
  - `74c924b9` chore(text): fix deprecation ticket refs
  - `bacbfc5a` chore(text): fix-up-2: §10 numbering + cat-3 budget
  - `cc3ad1a3` chore(text): fix deprecation markers and changelog

## AGENTS.md governance rules applicate

AGENTS.md `§Regole di lavoro + §Cat-3 anti-dup + §Docs canonical update discipline + §### 2×-in-one-chore + §Post-push SHA-selfcheck + §GATE-MNT-01 + §Per-branch rebase + §Regole di lint documentale` — tutte le regole canoniche disciplinano questo ticket-home (cross-link canonico sopra).
- `§Cat-3 anti-dup` — cronaca estesa lives in canonical ticket-home (questo file); CHANGELOG + FOLLOWUP = cite-only + cross-link
- `§Docs canonical update discipline rule` — Cat-5 3-doc atomic pattern
- `§### 2×-in-one-chore: deprecation reversal bundles forward-point tickets (Cat-3 anti-dup)` — forward-point bundled atomicamente con deprecation reversal (helper rimosso finale)
- `§Post-push SHA-selfcheck invariant` — SHA-triple verify post-push per AGENTS.md lost-commit prevention
- `§GATE-MNT-01 closure lineage` — per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` triad per push hygiene
- `§Per-branch rebase convention (read-side)` — `branch.main.rebase = true` (auto-FF unidirezionale per divergence recovery)
- `§Regole di lint documentale` — canonico-fence FOLLOWUP_TICKETS row ≤500 chars + description ≤200 chars; CHANGELOG cite-only ≤300 chars

## Riferimenti canonical + cronologia

- **Phase 1 commit (Blocco 5.1 deprecation bridge, DONE 2026-07-14)**: `b6397b90` + `74c924b9` + `bacbfc5a` + `cc3ad1a3` su origin/main (current HEAD `3cedbad2` post-FF-sync con questo chore ticket)
- **Canonical cronaca home per Phase 2**: this ticket-home (`TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md`)
- **Cross-link canonico**: `docs/FOLLOWUP_TICKETS.md` §Open Blockers row per catena-discovery (Cat-5 3-doc chaser-chore pattern)
- **Sibling Cat-5 3-doc chaser-chore precedent tickets**:
  - `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV-VPS-3DOC-CAT5-ALIGN.md`
  - `TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN.md`
  - `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN.md`
  - `TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN.md`
  - `TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN.md`
  - `TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN.md`
  - `TICKET-CHECK-NO-DUAL-TEXT-API-REGEX-IMPROVEMENT.md`

Cronaca estesa lives in this canonical ticket-home per AGENTS.md Cat-3 anti-dup doctrine (canonical = cronaca home, CHANGELOG/FOLLOWUP = cite-only + cross-link pointer).
