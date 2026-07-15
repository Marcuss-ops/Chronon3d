# TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION — Delete M1.8 §6 deprecation-bridge test blocks post Phase 3 helper-removal (replacement for retracted TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION)

| ID            | TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION                                                                                                                     |
|---------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Status        | **OPEN** (P3, future chore — post Phase 3 helper-removal ack)                                                                                                       |
| Replaces      | [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION.md) (RETRACTED 2026-07-15, premise failures discovered) |
| Parent        | [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (bulk-migration catena; sub-chore (g) row)        |
| Asset class   | test file deletion (cat-3 minimal-surface; sequencing-dependence on Phase 3 helper-removal)                                                                          |
| Scope         | Delete M1.8 §6 deprecation-bridge test blocks (purpose-built to test deprecated API) post-Phase-3 helper-removal                                                   |
| Surface       | `tests/text/test_text_simplicity_adapters.cpp` (665 LoC; 8 of 10 TEST_CASE blocks deleted, 2 retained as canonical-API property tests)                              |

## Stato: OPEN (replacement forward-point for retracted sub-chore (g) ticket)

## Problema

The retracted sub-chore (g) ticket `TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION.md` proposed **migrating** 25 callers from `centered_text(CenterTextOptions)` + `glow_text(...)` to `TextDefinition{}` direct construction. Premise analysis (2026-07-15, this-session discovery) revealed this migration is **structurally invalid**:

1. **Test purpose inversion**: The file's purpose (per its own header "M1.8 §6 / TICKET-SIMPLICITY-ADAPTERS") is to test the DEPRECATION BRIDGE — i.e., that (a) the deprecated API still compiles + emits warnings; (b) the new API produces byte-equivalent output; (c) the deprecation-warning capture pattern works. Migrating the test's calls from deprecated to canonical DEFEATS THE TEST PURPOSE; the test would become vacuous circular (`TextDefinition` vs `TextDefinition` with no legacy API validation).

2. **Byte-equivalence scope mismatch**: The `kRefVR*` sentinel hashes (referenced in the broken ticket as "byte-equivalence regression lock preservation obbligatorio") reside in `tests/deterministic/test_visual_regression_scenarios.cpp` — that is the (f) sibling migration ticket scope `TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION`, NOT this ticket's target file.

3. **Cross-file caller distribution**: 78 callers exist across `tests/` (24+16 in `test_text_definition.cpp`, 9 in `test_visual_regression_scenarios.cpp`, 18+7 in target, 2 in `test_text_preset_subtitle.cpp`, 1 in `test_text_golden.cpp`, 1 in `text_visual_fixture.hpp`). The 1-file ticket scope contradicts the macchina-verifica `rg 'tests/' = 0` criterion; naive adherence would require migration of 78 callers across 6 files (violates §Fare PR piccole e mirate atomic-commit scope).

4. **Local verification gap**: `ctest -R test_text_simplicity_adapters` is DEFERRED-WBH per VPS vcpkg glm/magic_enum env-block. Cannot empirically verify the CRITICAL byte-equivalence claim. §honest-discipline violation if migration is claimed successful without empirical verification.

## Soluzione accettabile (test-deletion post Phase 3)

After Phase 3 completes (`TICKET-CENTERED-TEXT-MIGRATION` Phase 3 forward-point → `centered_text()` + `glow_text()` + `CenterTextOptions` removed from headers; zero prod callers), the M1.8 §6 deprecation-bridge tests have NO PURPOSE: there is no longer a deprecated API to bridge. The correct action is to **DELETE the M1.8 §6 deprecation-bridge TEST_CASE blocks** in `tests/text/test_text_simplicity_adapters.cpp` in lockstep with Phase 3 helper-removal.

### Concrete deletion scope

| TEST_CASE block | Lines (approx) | Action | Reason |
|---|---|---|---|
| `Adapters: glow_text() consolidation writes TextMaterial glow fields` | §1 (~L60-L91) | **DELETE** | post-Phase-3, no legacy `glow_text()` |
| `Adapters: centered_text() emits [DEPRECATED] spdlog::warn (capture via sink)` | §2-3 (~L138-L160) | **DELETE** | post-Phase-3, no legacy `centered_text()` deprecation |
| `Adapters: centered_text() deprecation warning is one-shot per process` | §2-3 (~L162-L180) | **DELETE** | post-Phase-3, no legacy `centered_text()` deprecation |
| `Adapters: backward compat — LayerBuilder::text_run() routes through canonical pipeline` | §4 (~L185-L201) | **RETAIN** | new API; unaffected by Phase 3 |
| `Adapters: new API built node matches old API built node` | §4 (~L203-L259) | **DELETE** | no old-API surface post-Phase-3; comparison no longer meaningful |
| `Adapters: new API TextRunSpec matches old API centered_text spec` | §4 (~L261-L330) | **DELETE** | no old-API surface post-Phase-3; comparison no longer meaningful |
| `Adapters: old API centered_text() vs new API Layer::text() — pixel hash equal` (Blend2D-gated) | §5-7 (~L420-L457) | **DELETE** | post-Phase-3, no old-API |
| `Adapters: old API glow_text() vs new API Material::glow() — pixel hash equal` (Blend2D-gated) | §5-7 (~L459-L487) | **DELETE** | post-Phase-3, no old-API |
| `Adapters: new API Layer::text() is deterministic — same input same pixel hash` (Blend2D-gated) | §5-7 (~L489-L513) | **RETAIN** | new API property test; unaffected by Phase 3 |

**Net effect after deletion + Phase 3**: 7 of 9 TEST_CASE blocks deleted. 2 retained (LayerBuilder::text_run route test + Layer::text() determinism test). File reduced from 665 LoC to ~80 LoC.

### Manifest cleanup (sibling scope)

`tests/manifests/test_aggregates.cmake` MUST remove the `chronon3d_add_test_suite(test_text_simplicity_adapters)` invocation post-deletion (orphan suite name → cmake warning cascade).

## Vincoli (cat-2 freeze + cat-3 minimal-surface + §honest-discipline)

- §Fare PR piccole e mirate: atomic commit scope = 1 test file + `tests/manifests/test_aggregates.cmake` update + ZERO altri source touched
- §Cat-3 minimal-surface: ZERO new SDK API + ZERO nuovi singleton/registry/resolver/cache + ZERO Gate 5 deny-everywhere violationi (`#include <msdfgen>/<libtess2>/<unicode[/...]>`)
- §honest-discipline: ticket content reflects corrected scope; rot-discovery audit-trail preserved in retracted ticket-home
- Sequencing-dependency on Phase 3 (`TICKET-CENTERED-TEXT-MIGRATION` Phase 3 forward-point: helpers literally absent from headers)

## Criteri di accettazione

- [ ] Phase 3 helper-removal DONE (`centered_text()` + `glow_text()` + `CenterTextOptions` removed from headers)
- [ ] macchina-verifica: `rg 'centered_text\(|glow_text\(' tests/text/test_text_simplicity_adapters.cpp` returns 0 post-deletion
- [ ] macchina-verifica: `ctest -R test_text_simplicity_adapters` exit 0 (DEFERRED-WBH per vcpkg glm/magic_enum env-block)
- [ ] macchina-verifica: `tests/manifests/test_aggregates.cmake` removes `chronon3d_add_test_suite(test_text_simplicity_adapters)` invocation
- [ ] Cat-3 surface: ZERO new SDK API + ZERO new singleton/registry/resolver/cache + ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` Gate 5 deny-everywhere preserved
- [ ] Subject envelope ≤72 chars per `tools/check_commit_subject_length.sh`
- [ ] SHA-triple equality verify post-push (per AGENTS.md §Post-push invariant)

## Forward-points

| Status | Description |
|--------|-------------|
| OPEN (P3) | Esegue deletion dei 7 deprecation-bridge TEST_CASE blocks in `tests/text/test_text_simplicity_adapters.cpp` post-Phase-3. Subject envelope proposed: `chore(test): delete M1.8 §6 deprecation-bridge test blocks` (~62 chars) |
| OPEN | Aggiorna `tests/manifests/test_aggregates.cmake` per rimuovere `chronon3d_add_test_suite(test_text_simplicity_adapters)` invocation post-deletion |
| DEFERRED | ctest verification post-deletion: `ctest -R test_text_simplicity_adapters` real-green-PASS su workstation pulita (vcpkg glm/magic_enum installato). DEFERRED-WBH per VPS vcpkg env-block |

## Cross-link canonical

- **Parent**: [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (parent bulk-migration catena; sub-chore (g) row → rot-discovery this-session + replacement forward-point = this ticket)
- **Replaces**: [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION.md](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION.md) (RETRACTED 2026-07-15; original content retained as historical crona per AGENTS.md §Lint documentale)
- **Sequencing-dependency**: [TICKET-CENTERED-TEXT-MIGRATION.md](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN; Phase 3 forward-point = helper-removal canonical)
- **Audit chaser-ticket**: [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT.md) (catena origin; the audit non-vacuous finding triggered both the retracted migration ticket and this replacement deletion ticket)

## AGENTS.md governance rules applicate

- §Cat-3 minimal-surface (1 file EDIT + 1 manifest EDIT, ZERO new SDK API, ZERO new singleton/registry/resolver/cache)
- §honest-discipline (corrects retracted ticket premises; documents discovery trail in retracted ticket)
- §Docs canonical update discipline (this ticket is the corrected forward-point replacement; cat-5 3-doc atomic bundle per §### 2×-in-one-chore)
- §"Fare PR piccole e mirate" (atomic commit scope = 1 test file + 1 manifest update)
- §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- §Regole di lint documentale (canonico-fence FOLLOWUP_TICKETS row + CHANGELOG cite-only + subject envelope ≤72)
- §GATE-MNT-01 closure lineage (per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` triad)
- §Per-branch rebase convention (read-side, `branch.main.rebase = true`)
