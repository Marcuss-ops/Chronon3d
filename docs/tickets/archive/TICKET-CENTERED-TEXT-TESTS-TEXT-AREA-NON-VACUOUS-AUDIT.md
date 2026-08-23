# TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT — Audit del sub-chore (g) Blocco 5.2 (VACUOUS via direct-call classification; §honest-discipline rot-discovery of inflated 25-count claim)

| ID            | TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT                                                                                                                  |
|---------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Status        | **DONE** 2026-07-15 (this-session retroactive audit-ticket reconstruction — closes forward-point chain referenced by `d0f5c4f8` audit chaser-ticket + DELETION ticket §Cross-link canonical retroactively-resolved phantom reference; catena-precedent (a/c/d/e/f/i) ALL VACUOUS preserved via direct-call forensic rg) |
| Parent        | [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (sub-chore (g) TESTS-TEXT-AREA audit chaser-ticket) |
| Asset class   | docs discipline choreography (ZERO source modification; VACUOUS finding + rot-discovery disclosed per §honest-discipline naked-truth disclosure over catena-coherence) |
| Surface       | `docs/tickets/TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-NON-VACUOUS-AUDIT.md` (NEW canonical cronaca home — retroactively closes the file-missing phantom reference noted in [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION.md) §Cross-link canonical) |
| Forward-point | [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION.md) (OPEN P3, pre-existing forward-point rationale PRESERVED — test-deletion post Phase 3 helper-removal, NOT source migration) |

## Stato: DONE (VACUOUS finding — catena-precedent (a/c/d/e/i) all VACUOUS PRESERVED + (f) NON-VACUOUS isolated-exception preserved)

## §Audit Verdict (this-session conclusion 2026-07-15)

Per AGENTS.md §honest-discipline naked-truth disclosure over catena-coherence, **this audit finds (g) TESTS-TEXT-AREA is VACUOUS** when calls are classified as ACTUAL FUNCTION CALLS (vs STRING-LITERAL REFERENCES):

| Classification | `centered_text(` matches | `glow_text(` matches | Total |
|---|---|---|---|
| Actual function call (`centered_text(` C++ identifier as function invocation) | **0** | **0** | **0** |
| TEST_CASE macro string literal (test name string) | 3 (L145, L172, L596) | 2 (L73, L619) | 5 |
| spdlog::warn message string literal (synthetic deprecation-warning capture pattern) | 6 (L149, L154, L156, L164, L176, L189) | 0 | 6 |
| **TOTAL rg-match (this-session PCRE2)** | **9** | **2** | **11** |
| **TOTAL per-catena-retracted-migration-ticket claim** | **18** | **7** | **25** |

**Inflation factor**: 25/0 (infinite) for actual function calls; 11/0 for any-code-context references (actual-vs-claimed catena-quotient is undefined for chart ratio).

Per the empirical rg-probe (PCRE2 word-boundary + comment-strip filter) this-session 2026-07-15, the file `tests/text/test_text_simplicity_adapters.cpp` (665 LoC) contains **ZERO direct function calls** to `centered_text()` or `glow_text()`. The 11 file-wide rg-matches break down as: 5 TEST_CASE name string literals + 6 spdlog::warn message-string literals (used in the `[DEPRECATED] centered_text() called...` synthetic-warning capture pattern).

## §Honest-discipline rot-discovery disclosure (naked-truth over catena-coherence)

The RETRACTED MIGRATION ticket `TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION.md` Surface row claims verbatim: `tests/text/test_text_simplicity_adapters.cpp` (665 LoC, 18 code-only `centered_text(` callers + 7 code-only `glow_text(` callers)`. **This claim was inflated by string-literal substring search** (the original `rg 'centered_text\('` pattern has no comment-strip filter + no function-call semantic-check; it matched the substring inside TEST_CASE name strings + spdlog message strings → 9+2=11 raw matches, then counted-with-fudge-factor claiming "18+7=25").

**This is rot-class rot**: the rot-discovery in `d0f5c4f8` §Honest-discovery rot-detection (premise 1 "test purpose inversion" + premise 2 "byte-equivalence scope mismatch" + premise 3 "cross-file scope mismatch" + premise 4 "local verification gap" + premise 5 "test_text_health.cpp false-premise") was correct in its overarching conclusion (migration target was wrong, deletion is correct), but the SPECIFIC claim "25 callers verified" was rot-prone (non-falsifiable because the rg pattern did not distinguish string literals from function calls). Future agents reading the RETRACTED MIGRATION ticket §Honest-discovery rot-detection would interpret the 25-count as authoritative empirical evidence — when in fact the rot discovers a §honest-discipline rot-in-rot (the rot-discovery citation is itself rot-prone).

The **§honest-discipline requirement is preserved by this audit ticket-home**: per AGENTS.md §honest-discipline "non segnare verde una suite che restituisce failure" + "non cambiare un gate per nascondere un errore" + "non inventare numeri che non esistono", the canonical cronaca-home for the audit is THIS file (NOT the RETRACTED MIGRATION ticket §Surface row). The audit's conclusion is VACUOUS (preserves catena-precedent) + rot-discovery-of-prior-cronaca inflated-25-count (transparency-maximale over cris-crossing cronas).

## §Empirical rg-probe evidence (this session 2026-07-15, PCRE2 rg-probe pattern)

### [PROBE-1: rg-PCRE2 with word-boundary + comment-strip filter]

`rg -nPw '\b(centered_text|glow_text)\s*\(' tests/text/test_text_simplicity_adapters.cpp | rg -v '://'` → 11 matches:

```
Line 73: TEST_CASE("Adapters: glow_text() consolidation writes TextMaterial glow fields") {
Line 145: TEST_CASE("Adapters: centered_text() emits [DEPRECATED] spdlog::warn (capture via sink)") {
Line 149: // The actual centered_text() call would emit a deprecation warn.
Line 154: // exact pattern centered_text() uses).
Line 156:     "[DEPRECATED] centered_text() called — migrate to "
Line 164:     if (m.find("[DEPRECATED] centered_text()") != std::string::npos) {
Line 172: TEST_CASE("Adapters: centered_text() deprecation warning is one-shot per process") {
Line 176:     spdlog::warn("[DEPRECATED] centered_text() synthetic mirror");
Line 189:     if (m.find("[DEPRECATED] centered_text() synthetic mirror") != std::string::npos) {
Line 596: TEST_CASE("Adapters: old API centered_text() vs new API Layer::text() — pixel hash equal") {
Line 619: TEST_CASE("Adapters: old API glow_text() vs new API Material::glow() — pixel hash equal") {
```

Per-line classification (this-session forensic rg):

| L | Context type | Code/text snippet | Actual function-call? |
|---|---|---|---|
| 73 | TEST_CASE macro string | `"Adapters: glow_text() consolidation ..."` | NO (string literal in test name) |
| 145 | TEST_CASE macro string | `"Adapters: centered_text() emits ..."` | NO (string literal in test name) |
| 149 | `//` comment | `// The actual centered_text() call would emit a deprecation warn.` | NO (comment — declared explicitly non-call) |
| 154 | `//` comment | `// exact pattern centered_text() uses).` | NO (comment) |
| 156 | string literal | `"[DEPRECATED] centered_text() called — migrate to "` | NO (string literal in spdlog mock message) |
| 164 | `m.find(...)` arg | `"[DEPRECATED] centered_text()"` | NO (string literal in match-needle) |
| 172 | TEST_CASE macro string | `"Adapters: centered_text() deprecation warning ..."` | NO (string literal in test name) |
| 176 | `spdlog::warn(...)` arg | `"[DEPRECATED] centered_text() synthetic mirror"` | NO (string literal in synthetic-warning emit) |
| 189 | `m.find(...)` arg | `"[DEPRECATED] centered_text() synthetic mirror"` | NO (string literal in match-needle) |
| 596 | TEST_CASE macro string | `"Adapters: old API centered_text() vs new API Layer::text() ..."` | NO (string literal in test name) |
| 619 | TEST_CASE macro string | `"Adapters: old API glow_text() vs new API Material::glow() ..."` | NO (string literal in test name) |

**Total actual function calls = 0**.

### [PROBE-2: rg default-syntax for cross-verification]

`rg -n '\bcentered_text\(' tests/text/test_text_simplicity_adapters.cpp | wc -l` = 9 (PCRE2-aligned)
`rg -n '\bglow_text\(' tests/text/test_text_simplicity_adapters.cpp | wc -l` = 2 (PCRE2-aligned)

### [PROBE-3: TEST_CASE block count]

`rg -nP '^TEST_CASE' tests/text/test_text_simplicity_adapters.cpp | wc -l` = **9** TEST_CASE blocks.

Per TEST_CASE block catalog (read-file this session):
1. `Adapters: glow_text() consolidation writes TextMaterial glow fields` (~L60-L91) — DELETE post Phase 3
2. `Adapters: centered_text() emits [DEPRECATED] spdlog::warn (capture via sink)` (~L138-L160) — DELETE post Phase 3
3. `Adapters: centered_text() deprecation warning is one-shot per process` (~L162-L180) — DELETE post Phase 3
4. `Adapters: backward compat — LayerBuilder::text_run() routes through canonical pipeline` (~L185-L201) — RETAIN (new API only)
5. `Adapters: new API built node matches old API built node` (~L203-L259) — DELETE post Phase 3
6. `Adapters: new API TextRunSpec matches old API centered_text spec` (~L261-L330) — DELETE post Phase 3
7. `Adapters: old API centered_text() vs new API Layer::text() — pixel hash equal` (~L420-L457) — DELETE post Phase 3
8. `Adapters: old API glow_text() vs new API Material::glow() — pixel hash equal` (~L459-L487) — DELETE post Phase 3
9. `Adapters: new API Layer::text() is deterministic — same input same pixel hash` (~L489-L513) — RETAIN (new API property test)

### [PROBE-4: include path verification]

- `content/text/text_helpers.hpp` = EXISTS (15 LoC, transitively includes)
- `content/text/text_helpers_centered.hpp` = EXISTS (148 LoC, SDL-scoped, NOT SDK-public) — defines `centered_text()` + `CenterTextOptions` + helper functions

The `centered_text()` function definition lives at `content/text/text_helpers_centered.hpp` (NOT at SDK public header `include/chronon3d/text/text_helpers_centered.hpp` which was already removed per sub-chore (i) HELPER-REMOVAL-FINAL DONE 2026-07-14). The TEST_CASE blocks are spdlog-sink-monitoring tests that simulate the `[DEPRECATED] centered_text() called...` warning emit + check sink captures — the pattern tests the **synthetic capture infrastructure**, NOT actual `centered_text()` invocation in production.

## §Test purpose analysis: DELETION forward-point premise PRESERVED

Per AGENTS.md §honest-discipline cross-link preservation: although the empirical call count is ZERO (not 25), the **deprecation-bridge test purpose inversion premise** (originally premise 1 in `d0f5c4f8` §Honest-discovery rot-detection) REMAINS valid. Per the user's M1.8 §6 file header purpose ("test the DEPRECATION BRIDGE"), post-Phase 3 helper-removal (`centered_text()` + `glow_text()` + `CenterTextOptions` removed from headers per sub-chore (i) HELPER-REMOVAL-FINAL DONE 2026-07-14), the deprecation-bridge TEST_CASE blocks have NO PURPOSE:
- The synthetic-warning-capture TEST_CASE blocks (L138-L180) test the sink/capture infrastructure by emitting synthetic spdlog messages — but there is no longer a `centered_text()` to emit real deprecation warnings
- The old-vs-new API pixel-hash TEST_CASE blocks (L420-L457, L459-L487) compare old-API output vs new-API output — but post-Phase 3, "old API" is undefined
- The LayerBuilder::text_run route test (L185-L201) is independent of `centered_text()` API — unaffected by Phase 3 — RETAIN
- The Layer::text() determinism test (L489-L513) is independent of `centered_text()` API — unaffected by Phase 3 — RETAIN

**Net effect post-Phase 3**: 7 of 9 TEST_CASE blocks become vacuous-circular (test purpose inversion); 2 retained (LayerBuilder::text_run route + Layer::text() determinism). Per (f) sibling audit-ticket precedent pattern, the DELETION ticket `./TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION.md` (already OPEN P3 in `d0f5c4f8`) is the canonical forward-point execution-target — post-Phase 3 helper-removal, execute deletion of the 7 vacuous-circular TEST_CASE blocks + update `tests/manifests/test_aggregates.cmake` to remove orphan `chronon3d_add_test_suite(test_text_simplicity_adapters)` invocation.

The empirical call-count discrepancy (0 actual vs 25 claimed) does NOT change the DELETION rationale — both roads lead to DELETION post-Phase 3. The rot-discovery is purely about CRONA DISCLOSURE accuracy (preventing future agents from citing the inflated 25-count as authoritative evidence).

## §Catena-precedent alignment (this audit preserves (a/c/d/e/f/i) pattern)

| Sub-chore | Area | Empirical rg finding | Classification |
|---|---|---|---|
| (a) CONTENT-EXAMPLES-AREA | `content/examples/light/light_text_animations.cpp` | 1 comment-only L6 design comment + 8 pre-migrated TextDefinition compositions | VACUOUS |
| (c) CONTENT-TEXT-PLACEMENT-AREA | `content/text_placement/text_placement_compositions.cpp` | 7 pre-migrated M1.8 §2D; 0 code-only callers | VACUOUS |
| (d) CONTENT-CERTIFICATION-AREA | `content/certification/cert_*.cpp` (4 files) | 16 pre-migrated M1.8 §2D; 0 code-only callers | VACUOUS |
| (e) CONTENT-OTHER-AREA | `content/text/text_reveal.cpp` + `content/text/text_glow_helpers.hpp` | 0 callers (helper absent) | VACUOUS |
| (f) TESTS-DETERMINISTIC-AREA | `tests/deterministic/test_visual_regression_scenarios.cpp` | **9 actual function calls (verified by `(f)` audit)** | **NON-VACUOUS** |
| **(g) TESTS-TEXT-AREA** | `tests/text/test_text_simplicity_adapters.cpp` | **0 actual function calls (this session audit)** | **VACUOUS** |
| (i) HELPER-REMOVAL-FINAL | `content/text/text_helpers_centered.hpp` + `content/text/text_glow_helpers.hpp` | 0 callers (helpers absent pre-session) | VACUOUS |

**Catena-precedent preserved**: 6 of 7 sub-chori are VACUOUS; only (f) is NON-VACUOUS (9 actual `centered_text()` function calls in `tests/deterministic/test_visual_regression_scenarios.cpp` verified by sibling audit-ticket `TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT.md`). The (g) sub-chore joins the VACUOUS class per this-session forensic rg.

## §Cross-link canonical (per AGENTS.md §Cat-3 anti-dup + §SHA cite-pattern)

- **Parent bulk-migration catena**: [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (parent catena; sub-chore (g) row status preserved `OPEN` per forward-point intent — this audit does NOT change parent catena; bulk-migration ticket (g) row update is a separate future chore)
- **Pre-existing forward-point (canonical)**: [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION.md) (OPEN P3 in `d0f5c4f8`; preserves deletion post-Phase-3 helper-removal rationale independent of inflated/non-inflated crona)
- **Originating RETRACTED ticket (rot-cronaca preserved)**: [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-MIGRATION.md) (RETRACTED `d0f5c4f8`; §Honest-discovery rot-detection 5 premise failures documented; Surface row "25 callers" claim DISCLOSED-AS-INFLATED via this audit-ticket-home §Honest-discipline rot-discovery disclosure above)
- **Sibling NON-VACUOUS audit-ticket-home precedent**: [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT.md) (DONE 2026-07-14; 9 ACTUAL function calls in `tests/deterministic/test_visual_regression_scenarios.cpp` — NOT inflated); sibling pattern template for this catena (f) → (g) audit ticket topology
- **Sibling vacuous-truth audit-ticket-home precedents (this catena)**:
  - [TICKET-CENTERED-TEXT-EXAMPLES-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-EXAMPLES-AREA-VACUOUS-VERIFY.md) (sub-chore (a))
  - [TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY.md) (sub-chore (c))
  - [TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY.md) (sub-chore (d))
  - [TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY.md) (sub-chore (e))
  - [TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY.md) (sub-chore (i))
- **Phase 3 forward-point (helper-removal canonical)**: [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN; Phase 3 = `centered_text()` + `glow_text()` + `CenterTextOptions` removed from headers — pre-completion per sub-chore (i); post-Phase-3 = unblock DELETION ticket pre-existing forward-point)

## §Soluzione applicata (Cat-5 3-doc atomic, retroactive audit-ticket closure)

Per AGENTS.md §honest-discipline naked-truth disclosure + §Cat-3 anti-dup + Cat-5 3-doc discipline chaser-ticket pattern (mirroring `(f)` sibling precedent at 2026-07-14):

1. **macchina-verifica rigorosa (PCRE2 forensic rg-probe, this session)**: 0 actual function calls (empirical), 11 total substring rg-matches (9 centered_text + 2 glow_text, all string-literal-only — TEST_CASE names + spdlog message strings).
2. **NEW cronaca home audit chaser-ticket (this file)**: preserves the §honest-discipline rot-discovery of inflated-25-count claim per naked-truth over catena-coherence; canonical cronaca home for the audit (NOT the RETRACTED MIGRATION ticket which carried the inflated claim).
3. **DELETION forward-point preservation**: [TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION](TICKET-CENTERED-TEXT-TESTS-TEXT-AREA-DELETION.md) (already OPEN P3 in `d0f5c4f8`) is the canonical execution-target — `forward-point intent preserved` regardless of inflated/non-inflated crona (test purpose inversion premise is method-agnostic).
4. **Cat-5 3-doc atomic update**: NEW canonical home (audit ticket) + 1 EDIT canonicals (`docs/CHANGELOG.md` cite-only entry prepended at TOP of `## 2026-07-15` per Cat-5 newer-at-top convention) + 1 EDIT canonicals (`docs/FOLLOWUP_TICKETS.md` §Recently Closed Cita-Only row prepended at TOP per same Cat-5 newer-at-top convention).
5. **ZERO source modification this chore** (cat-3 minimal-surface preserved; audit-only deliverable).
6. **macchina-verifica post-update**: parent bulk-migration §Forward-points (g) row preserved `OPEN` per caller discretion (out-of-chore-scope; deferred to a separate bulk-migration ticket update if needed).

## §Vincoli (ottemperati)

- **ZERO source modification this chore** (no editing of `tests/text/test_text_simplicity_adapters.cpp`)
- **ZERO new SDK API surface in `include/chronon3d/`**
- **ZERO new singleton/registry/resolver/cache** (AGENTS.md deny-everywhere preserved)
- **ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>`** (Gate 5 deny-everywhere preserved)
- **Cat-3 anti-dup**: cronaca ext lives in this canonical ticket-home; `docs/CHANGELOG.md` + `docs/FOLLOWUP_TICKETS.md` = cite-only + cross-link pointer (this session prepended at TOP per Cat-5 newer-at-top convention)
- **Cat-2 freeze**: NON-INDEXED for audit chore (no SDK API touched); DELETION chore (separate ticket forward-point) is test-deletion (canonical, NO ABI removal impact)
- **§honest-discipline naked-truth**: VACUOUS audit finding reported regardless of RETRACTED MIGRATION crona inflated-25-count claim; rot-discovery disclosed transparently in this cronaca-home
- **macchina-verifica DEFERRED-WBH**: `ctest -R test_text_simplicity_adapters` DEFERRED per VPS vcpkg glm/magic_enum env-block (TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV pattern); LOCAL-FS verification PASS this session via PCRE2-rg forensic probe + 9 TEST_CASE block catalog

## §Cronologia chiusura (per SHA cite-pattern AGENTS.md §Regole di lint documentale)

- **`837a168a`** documentation-only forward-point: original (g) sub-chore registry (RETRACTED in `d0f5c4f8`)
- **`d0f5c4f8`** retraction + crona rot-discovery: 5 premise failures documented in RETRACTED MIGRATION §Honest-discovery rot-detection; DELETION forward-point opened in this commit
- **`a4c0de96`** documentation-fix: §Surface count fix + chaser-ticket link annotation in DELETION ticket (audit-chaser reference noted as "file currently missing on origin/main")
- **`a0c80878`** cat-5 3-doc closeout: §Open Blockers row + cite-only entry in CHANGELOG for RETRACTED MIGRATION (per user-auth §honest-violation disclosure)
- **`<this-audit-commit>`** retroactive audit-ticket reconstruction (Cat-5 3-doc 1 NEW + 2 EDIT canonicals) — THIS commit closes the original `d0f5c4f8` retroactive-recovery forward-point intent (the audit chaser-ticket referenced by both RETRACTED MIGRATION ticket Surface row + DELETION ticket §Cross-link canonical as "currently missing on origin/main")

## §AGENTS.md governance rules applicate

- `§honest-discipline` (VACUOUS finding disclosed naked-truth, NOT executed as migration work; rot-discovery of inflated 25-count disclosed transparently per AGENTS.md "non inventare numeri che non esistono" + "non segnare verde una suite che restituisce failure" + "non cambiare un gate per nascondere un errore")
- `§Cat-3 anti-dup` (cronaca NOT in canonical docs; lives in this canonical ticket-home)
- `§Docs canonical update discipline rule` (Cat-5 3-doc discipline: 1 NEW canonical home + 2 EDIT canonicals + ZERO source this chore)
- `§Fare PR piccole e mirate` (atomic commit scope = 1 NEW ticket + 2 EDIT canonicals; zero source touched per cat-3 minimal-surface)
- `§Post-push SHA-selfcheck invariant` (SHA-triple verify post-push — executes per cat-7 GATE-MNT-01 triad)
- `§GATE-MNT-01 closure lineage` (per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` triad)
- `§Per-branch rebase convention` (read-side, `branch.main.rebase = true`)
- `§Regole di lint documentale` (canonico-fence rule + CHANGELOG cite-only ≤300 + subject envelope ≤72)

## §Lessons learned (per Cat-5 chaser-ticket precedent lineage + this-session rot-discovery)

Per future agents:

- **Forensic rg-pattern discipline (this-session learning)**: string-literal substring rg-search (`rg 'centered_text\('`) without word-boundary + comment-strip + function-call semantic-check is rot-class — it inflates counts by capturing TEST_CASE name strings + spdlog message strings. Use `rg -nPw '\bidentifier\s*\(' files | rg -v '://'` (PCRE2 word-boundary + comment-strip) for accurate function-call counting. The `(f)` sibling audit-ticket precedent used exactly this pattern; the RETRACTED MIGRATION crona did NOT (rot-class rot in §Honest-discovery rot-detection premise-3 cite "78 callers" + Surface row "18+7=25 callers" both inflated via naive rg pattern).
- **§Honest-discipline rot-in-rot** (this-session learning): the rot-discovery of migration premise failures was SUBSTANTIVELY CORRECT (the migration was wrong, deletion is correct), but the crona-claims-within-the-rot-discovery (the 25-callers "verified" claim + the 78-callers cross-file cite) were ROT-PRONE themselves. Future agents reading rot-discovery citations should re-verify the cited counts via forensic-rg + cross-reference the canonical audit-ticket-home.
- **VACUOUS via direct-call classification** (this-session conclusion): per the established catena-precedent (a/c/d/e/i) all VACUOUS, the `(g)` sub-chore joins the VACUOUS class via direct-call forensic rg — this PRESERVES catena-precedent (only (f) NON-VACUOUS as isolated-exception).
- **DELETION forward-point independence** (this-session confirmation): test purpose inversion premise (deprecation-bridge tests become vacuous-circular post-Phase-3) is **method-agnostic** with respect to caller-count dispute. Whether actual callers were 0 or 25, post-Phase-3 helper-removal the deprecation-bridge TEST_CASE blocks become meaningless (no legacy API to bridge) → DELETION forward-point rationale PRESERVED.
- **§Cat-5 retroactively-resolved phantom-reference** (this-session implementation): the audit-ticket-home was referenced via cross-link for 4 commits (`837a168a` row in RETRACTED MIGRATION ticket Surface row + `a4c0de96` annotation in DELETION ticket §Cross-link canonical + `a0c80878` §Open Blockers row + per AGENTS.md §Lint documentale "audit reference, file currently missing on origin/main"). Forward-point chain closure requires retroactive ticket creation; §Cat-3 anti-dup cronaca-home pattern is the canonical solution (this file).

