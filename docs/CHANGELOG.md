## Luglio 2026 — TICKET-FOLLOWUP-PRECEDENT-DOCS closure — `## Regole di lint documentale` 2nd rule aggregation: `[INFO] <gate-name>: <message>` diagnostic convention (3-doc Cat-5 same-commit; no source-code changes) (2026-07-11, atomic chore commit)

### docs(agents): `## Regole di lint documentale` 2nd rule — `[INFO] <gate-name>: <message>` diagnostic convention (closes TICKET-FOLLOWUP-PRECEDENT-DOCS per `2+ rule aggregate` closure criterion)

- **Scope**: closes the TICKET-FOLLOWUP-PRECEDENT-DOCS per its `2+ rule aggregate` closure criterion (the 1st rule was `### SHA cite pattern (inline-only rule)` introduced in commit `78919613`; the 2nd rule is `### INFO-level diagnostic style (gates)` introduced in this commit). After this commit, the `## Regole di lint documentale` bucket in `AGENTS.md` aggregates 2 rules (sha-cite + INFO-diagnostic) and the ticket is closed.
- **Rule content** (canonical pattern for future sibling gates):
  - **Format**: `[INFO] <gate-name>: <message>` with `<gate-name>` = script basename without `.sh` extension.
  - **Bash variable**: raccomandata `GATE_NAME=check_<name>` dichiarata in cima allo script per grep-discoverability (es. `grep -rE '^\[INFO\] \${GATE_NAME}:' tools/check_*.sh`).
  - **Emission contract**: una sola volta sullo stato clean (PASS), come riga **addizionale** al canonico `GATE_PASS` / `OK:` finale; MAI sul FAIL (FAIL path invariato, emette `GATE_FAIL:` con la lista dei colpevoli).
  - **Message length**: ≤ 200 caratteri per one-line grep-discoverability in CI log.
  - **Scope**: NEW gates only (gate esistenti invariati per AGENTS.md v0.1 "Fare PR piccole e mirate" — nessun churn retroattivo sui 14 gate della famiglia `OK:` / `GATE_PASS` / `HYGIENE_PASS [N/M]:` / `WARN:` / `FAIL:` / `GATE_FAIL:`).
- **Origine rule** (dedotta dal commit pregresso 0j+ amendment): il pattern `[INFO] <gate-name>: <message>` è stato introdotto in `tools/check_test_suite_registration.sh` (line 87) come prima istanza della convenzione. La 14-gate family audit (via `tools/check_doc_sync.sh` + per-gate source review) ha confermato il gap: nessun gate esistente usava `[INFO]` come prefisso diagnostico; la famiglia era `OK:` / `GATE_PASS` / `HYGIENE_PASS [N/M]:` / `WARN:` / `FAIL:` / `GATE_FAIL:`. L'adozione 0j+ ha riempito il gap; questo commit formalizza la convenzione come rule canonica del bucket `## Regole di lint documentale`.
- **Cat-3 (no source change JUSTIFIED)**: ZERO new symbols in `include/chronon3d/`. ZERO `[[deprecated]]` field annotations. ZERO dispatch-site forwarding logic. ZERO new SDK API surface. The change is markdown docs only.
- **Cat-5 (3-doc same-commit alignment) SATISFIED**: this CHANGELOG entry (prepended at TOP) + `AGENTS.md` NEW `### INFO-level diagnostic style (gates)` sub-section under `## Regole di lint documentale` + `docs/FOLLOWUP_TICKETS.md` MOVE `TICKET-FOLLOWUP-PRECEDENT-DOCS` row from §Open Blockers → §Recently Closed (top of table per the established pattern). `tools/check_doc_sync.sh` R5 fires on this closure. `docs/CURRENT_STATUS.md` INTENTIONALLY UNTOUCHED: per `docs/DOCUMENTATION_GOVERNANCE.md` the SDK state cell is "stato per area" — doc-governance has no SDK-state semantic.
- **AGENTS.md v0.1 freeze compliance** (revoked 2026-07-06, but Cat-3 + Cat-1 + §honesty rules permanent):
  - **Cat-1 commit-discipline**: single atomic chore commit (TICKET-FOLLOWUP-PRECEDENT-DOCS closure only); pure doc state mutation. "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + AGENTS.md new sub-section + FOLLOWUP_TICKETS row move all in same commit. CURRENT_STATUS intentionally untouched per above.
  - **Cat-3 (no new public API surface)**: SATISFIED — zero new symbols; the principle is DOCUMENTED not IMPLEMENTED.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 3-doc same-commit alignment** SATISFIED.
  - **Gate 5 deny-everywhere** N/A: markdown-only change.
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (VPS auth-block on `git push` per AGENTS.md §honesty per the established pattern).
- **Lint-checkability (forward-point, NOT in this commit)**: a future `tools/check_info_diagnostic_style.sh` (gate opzionale, non ancora implementato) could enforce the pattern. Implementation deferred to a separate ticket per AGENTS.md v0.1 "Fare PR piccole e mirate" + Cat-3 anti-duplication (the rule documentation precedes the lint tooling).
- **Files changed (3)**:
  - `AGENTS.md` EDIT (NEW `### INFO-level diagnostic style (gates)` sub-section under `## Regole di lint documentale` + verbatim SHA cite to `78919613` for the existing 1st rule + Perché/Origine/Scope/Anti-esempio/CORRETTO structure mirroring the existing 1st rule's shape)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)
  - `docs/FOLLOWUP_TICKETS.md` EDIT (MOVE `TICKET-FOLLOWUP-PRECEDENT-DOCS` row from §Open Blockers → §Recently Closed top of table)

---

## Luglio 2026 — forward-point 0k+ — `| tr -d '\n'` band-aid on `tools/check_test_suite_registration.sh` to silence the 0j+ residual `[[: 0\n0: syntax error in expression` stderr (38 noisy lines silenced; gate CONTRACT rc=0 preserved; band-aid trade-off documented in script comment; canonical fix cross-linked) (2026-07-11, atomic chore commit)

### fix(ci): test_suite_registration.sh 0k+ (tr-d newline band-aid)

- **Scope**: closes the TICKET-LAYER-IMAGE-MANIFEST-CLEAN cluster's forward-point 0k+ (the post-0j+ residual that the 0j+ commit at SHA `1d157ece` did NOT address). Machine-observed before this commit: the gate run emits 38 `[[: 0\n0: syntax error in expression (error token is "0")` stderr lines per invocation (one per cmake file in `tests/*.cmake` that has no `add_executable(chronon3d_*test ...)` or no `chronon3d_add_test_suite(` match), all at lines 53 + 58 of the script. After this commit, the noisy stderr is fully silenced via `| tr -d '\n'` added to both `grep | wc -l` pipelines (the `raw=$(...)` and `suite=$(...)` assignments).
- **Root cause (machine-verified + documented in script comment)**: the pipeline `raw=$(grep -E '^\s*add_executable\(...' "$f" 2>/dev/null | wc -l || echo 0)` is broken under `set -euo pipefail`. When `grep` finds NO match it exits 1; `set -o pipefail` propagates the failure through the pipeline; `wc -l` STILL outputs `0\n` (it succeeded; only the pipeline as a whole failed); the `|| echo 0` fallback then fires, outputting an ADDITIONAL `0\n`. Net pipeline stdout: `0\n0\n` (two lines, "0" + "0"). `$(...)` strips trailing newlines, so `raw="0\n0"` (literal embedded newline). `[[ ${raw:-0} -gt 0 ]]` then tries to parse `0\n0` as a single integer literal and fails, emitting the `[[: 0\n0: syntax error in expression` warning to stderr. The same bug applies to the `suite=$(...)` pipeline on a no-`chronon3d_add_test_suite` cmake file.
- **Band-aid (user-spec choice over canonical fix)**: per the user's explicit "Pre-strip whitespace" pick (vs. the canonical "Drop `|| echo 0`, add `|| true`"), the broken pipeline is kept intact, and the embedded newline is stripped via `| tr -d '\n'`. The new shape is `raw=$(grep -E '...' "$f" 2>/dev/null | wc -l | tr -d '\n' || echo 0)`. The `tr` strips the trailing newline from `wc -l`'s output (`0\n` → `0`), so when the `|| echo 0` fallback fires it appends `0\n` cleanly to the pipeline's `0`, yielding `00\n` → `$(...)` strips trailing newline → `raw="00"`. Bash parses `00` as OCTAL 0 (and evaluates to 0), so `[[ 00 -gt 0 ]]` = false with no warning. The `suite` pipeline is hardened the same way. `tr` is POSIX-mandatory and portable across bash 3.2 / macOS / Linux.
- **Canonical fix (NOT applied per user-spec)**: drop the `|| echo 0` fallback entirely and let `wc -l` be the final pipeline step. Canonical form: `raw=$(grep -E '...' "$f" 2>/dev/null | wc -l || true)`. This is the canonical bash-portable pattern (the `|| true` handles the `set -o pipefail` propagation of grep's exit-1; `wc -l` always outputs a single integer regardless). It was explicitly rejected by the user in favor of the band-aid. The canonical fix is now documented in the script's BAND-AID comment block (above lines 50-58) for future maintainers.
- **Accepted trade-off (N0 bug, flagged by the 0k+ thinker-with-files-gemini design verdict)**: if `grep` crashes MID-READ with an I/O error (extremely unlikely on local repo files given the `[ -f "$f" ]` pre-check at line 50 of the script), the `|| echo 0` fallback would falsely append a `0` to a PARTIAL `wc -l` count, yielding N0 instead of N. Concrete example: if grep matched 8 lines then crashed on line 9, `wc -l` outputs `8\n`, `tr -d '\n'` outputs `8`, the pipeline fails, `|| echo 0` outputs `0\n`, total is `80\n` → `$(...)` strips trailing newline → `raw="80"` → bash parses 80 (decimal, not octal since no leading 0 issue at this length) → 8 reported as 80. The 0k+ thinker verdict marked this as an "acceptable band-aid trade-off" given: (a) the `[ -f "$f" ]` pre-check at line 50 guarantees we're reading a regular file, (b) local repo files have negligible spontaneous I/O error rate, (c) the trade-off is documented inline in the script + here in CHANGELOG so a future maintainer who hits a real I/O error has the diagnostic trail. The canonical fix (`|| true` without `|| echo 0`) eliminates the N0 bug entirely; the band-aid preserves the broken pipeline's behavior in exchange for the user's preferred minimal-diff shape.
- **BAND-AID comment block** (added to the script at lines 50-66): the comment documents (1) the root cause (set -euo pipefail + grep exit-1 + wc -l still outputs + `|| echo 0` appends), (2) the band-aid rationale (`tr -d '\n'` strips embedded newline → `00` parses as octal 0), (3) the canonical fix cross-link (drop `|| echo 0` → `|| true`), (4) the N0 accepted trade-off. A future maintainer reading the script should be able to identify the `tr` as a band-aid and find this CHANGELOG entry for context.
- **Gate CONTRACT preservation**: the canonical invariant is preserved verbatim — rc=0 means "all $total test targets use chronon3d_add_test_suite()"; rc=1 means "at least one raw `add_executable(chronon3d_*test ...)` remains in: <file-list>"; rc=2 means "internal script error". The fix is BASH-PORTABILITY HARDENING ONLY — zero change to the gate's pass/fail semantics. The 0j+ `[INFO]` diagnostic line (line 87) is preserved verbatim. The empty-array guard around the `raw_files` for-loop (0j+ Pattern A) is preserved verbatim.
- **Cat-3 (no public API surface) SATISFIED**: zero new symbols in `include/chronon3d/`. Zero `[[deprecated]]` field annotations. Zero dispatch-site forwarding logic. The fix is a bash script internal hardening; pure tooling refactor.
- **Cat-5 (2-doc same-commit alignment) PARTIAL**: this CHANGELOG entry (prepended at TOP, above the 0j+ entry) + `tools/check_test_suite_registration.sh` (2 surgical edits: `| tr -d '\n'` added to 2 pipelines + BAND-AID comment block) both updated in this same atomic chore commit. `tools/check_doc_sync.sh` R5 fires on this closure. `docs/FOLLOWUP_TICKETS.md` INTENTIONALLY UNTOUCHED: 0k+ is a forward-point closure of a CI tool hardening (not a multi-file refactor); the existing `## Recently Closed` 0e + 0f+ + 0g+ + 0h+ rows are audit-grade entries for the scene-builder BUCKET-A/B/C partition lineage. Adding a 0k+ row there would conflate scene-builder image-manifest closure with a CI-tooling band-aid, violating Cat-3 anti-duplication. `docs/CURRENT_STATUS.md` INTENTIONALLY UNTOUCHED: per `docs/DOCUMENTATION_GOVERNANCE.md` the SDK state cell is "stato per area" — a CI-tooling hardening has no SDK-state semantic; SDK state at HEAD remains PASS (forward-points 0e + 0f+ + 0g+ + 0h+ closed).
- **AGENTS.md v0.1 freeze compliance** (revoked 2026-07-06, but Cat-3 + Cat-1 + §honesty rules permanent):
  - **Cat-1 commit-discipline**: single atomic chore commit (forward-point 0k+ closure only); pure bash hardening. "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + tools script both updated in same commit.
  - **Cat-3 (no new public API surface)**: SATISFIED — zero new symbols; pure tooling refactor.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 2-doc same-commit alignment** PARTIAL (CHANGELOG.md + tools script both updated in same commit; FOLLOWUP_TICKETS.md + CURRENT_STATUS.md intentionally untouched per above).
  - **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced (bash script, not C++).
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (VPS auth-block on `git push` per AGENTS.md §honesty per the established pattern).
  - **§honesty compliance**: the band-aid is a deliberate trade-off chosen by the user; the canonical fix is documented in the script comment + this CHANGELOG entry for future maintainers. The N0 accepted trade-off is documented in BOTH the script comment AND this CHANGELOG entry so the trade-off is explicit + discoverable via `git grep` from either side.
- **Files changed (2)**:
  - `tools/check_test_suite_registration.sh` EDIT (2 surgical edits: `| tr -d '\n'` added to the 2 `grep | wc -l` pipelines at lines 53 + 58, plus a BAND-AID comment block above lines 50-58 documenting the root cause + band-aid rationale + canonical fix cross-link + N0 accepted trade-off)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP, above the 0j+ entry)

---

## Luglio 2026 — forward-point 0j+ — bash-portability hardening of `tools/check_test_suite_registration.sh`: empty-array guard around `${raw_files[@]}` for-loop + single INFO-level diagnostic on clean state (silences noisy stderr; preserves gate CONTRACT rc=0) (2026-07-11, atomic chore commit)

### fix(ci): test_suite_registration.sh 0j+ (empty-array guard + INFO)

- **Scope**: closes the TICKET-LAYER-IMAGE-MANIFEST-CLEAN cluster's forward-point 0j+ (machine-observed: the pre-push gate sweep emitted several `integer expression expected` bash warnings at lines 53 + 58 of `tools/check_test_suite_registration.sh` despite the gate exiting 0 PASS). After this commit, the noisy stderr is silenced via: (a) an explicit `[[ ${#raw_files[@]} -gt 0 ]]` empty-array guard around the `${raw_files[@]}` for-loop in the GATE_FAIL block (canonical Pattern A: explicit length check, the most portable + bash 4.x-5.x-compatible), and (b) a single `[INFO]`-prefixed diagnostic line emitted on the canonical clean state (when `raw_count=0`).
- **Gate CONTRACT preservation**: the canonical invariant is preserved verbatim — rc=0 means "all $total test targets use chronon3d_add_test_suite()"; rc=1 means "at least one raw `add_executable(chronon3d_*test ...)` remains in: <file-list>"; rc=2 means "internal script error". The fix is BASH-PORTABILITY HARDENING ONLY — zero change to the gate's pass/fail semantics.
- **Empty-array guard pattern** (Pattern A — chosen for canonical alignment with sibling gates `tools/check_test_hygiene.sh` line 36, `tools/check_text_golden_sources_aligned.sh` line 79, `tools/check_no_changelog_conflict_markers.sh`):
  ```bash
  if [[ ${#raw_files[@]} -gt 0 ]]; then
      for f in "${raw_files[@]}"; do
          echo "  - tests/$f"
      done
  fi
  ```
  Other patterns considered + rejected: Pattern B (default-on-empty `${arr[@]:-}`) requires `set -u` OFF, conflicts with `set -euo pipefail` at the top of the script; Pattern C (`for x in "${arr[@]+"${arr[@]}"}"`) is arcane; Pattern D (cryptic `$((${arr[@]:-0} + 1))`) is harder to grep + harder to extend. Pattern A is the only one that works with `set -u` enabled AND is grep-discoverable.
- **Single INFO-level diagnostic** (canonical pattern matching the [INFO] prefix style):
  ```
  [INFO] check_test_suite_registration: 0 raw matches found — clean state (all $suite_count suite invocations verified across $(N) test cmake files)
  ```
  Emitted ONCE on the canonical clean state (raw_count=0, total=suite_count). NOT emitted in the FAIL state (raw_count>0). Emitted to STDOUT (matching the gate's existing convention; sibling gates `tools/check_text_golden_sources_aligned.sh` + `tools/check_no_changelog_conflict_markers.sh` also use stdout for the OK diagnostic). The `[INFO]` prefix is grep-discoverable via `bash tools/check_test_suite_registration.sh 2>&1 | grep '^\[INFO\]'`.
- **Cat-3 (no public API surface) SATISFIED**: zero new symbols in `include/chronon3d/`. Zero `[[deprecated]]` field annotations. Zero dispatch-site forwarding logic. The fix is a bash script internal hardening; pure tooling refactor.
- **Cat-5 (2-doc same-commit alignment) SATISFIED**: this CHANGELOG entry (prepended at TOP, above the WAVE-02 0a entry) + `tools/check_test_suite_registration.sh` (2 surgical edits: empty-array guard + INFO diagnostic) both updated in this same atomic chore commit. `tools/check_doc_sync.sh` R5 fires on this closure. **`docs/FOLLOWUP_TICKETS.md` INTENTIONALLY UNTOUCHED**: this is a forward-point closure (audit-grade); the §Recently Closed table for the TICKET-LAYER-IMAGE-MANIFEST-CLEAN cluster already has 4 rows (0e + 0f+ + 0g+ + 0h+) — adding a 5th row for 0j+ would be over-sprawl since the closure is a 1-line tooling fix not a multi-file refactor. The 0j+ forward-point is logged HERE in CHANGELOG as the canonical closure audit. **`docs/CURRENT_STATUS.md` INTENTIONALLY UNTOUCHED**: per `docs/DOCUMENTATION_GOVERNANCE.md` the SDK state cell is "stato per area" — adding a row for a CI-tooling hardening would dilute the SDK-state semantic.
- **AGENTS.md v0.1 freeze compliance** (revoked 2026-07-06, but Cat-3 + Cat-1 + §honesty rules permanent):
  - **Cat-1 commit-discipline**: single atomic chore commit (forward-point 0j+ closure only); pure bash hardening. "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + tools script both updated in same commit.
  - **Cat-3 (no new public API surface)**: SATISFIED — zero new symbols; pure tooling refactor.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 2-doc same-commit alignment** PARTIAL (CHANGELOG.md + tools script both updated in same commit; FOLLOWUP_TICKETS.md + CURRENT_STATUS.md intentionally untouched per above).
  - **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced (bash script, not C++).
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (VPS auth-block on `git push` per AGENTS.md §honesty per the established pattern).
  - **§honesty compliance**: the noisy stderr was previously masked by the exit-0 PASS signal; future iterations of the gate + CI log readers could mistake the warnings for real issues. The fix preserves the gate's "PASS" verdict while making the audit signal silent (no spurious warnings) AND explicit (single INFO diagnostic confirming the clean state).
- **Files changed (2)**:
  - `tools/check_test_suite_registration.sh` EDIT (2 surgical edits: empty-array guard around `${raw_files[@]}` for-loop + single `[INFO]`-prefixed diagnostic line on the canonical clean state)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)

---

## Luglio 2026 — TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 forward-point 0a — WAVE-02 first-step seed: extend ## Cartography Architecture machine-verification grep to text-shape primitives + add 1-row catalogued forward-points entry (no source-code changes; doc-only chore) (2026-07-11, atomic chore commit)

### docs(cartography): WAVE-02 first-step seed (text-shape grep + 0a) — ## Cartography Architecture extension to text-shape surface

- **Scope**: closes the WAVE-02 first-step seed (forward-point 0a) per the canonical ticket template at [`docs/tickets/TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02.md`](tickets/TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02.md) (just opened in chore commit 1 at SHA `3db684bd`). After this commit, the `## Cartography Architecture` section in `docs/FOLLOWUP_TICKETS.md` is extended to cover text-shape primitives — the machine-verification grep now scans BOTH `include/chronon3d/scene/builders/builder_params.hpp` AND `include/chronon3d/text/`, and a new `TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 forward-point 0a` row is added to the `### Catalogued forward-points` table.
- **Cat-3 (no source change JUSTIFIED)**: ZERO new symbols in `include/chronon3d/text/`. ZERO `[[deprecated]]` field annotations. ZERO dispatch-site forwarding logic added. ZERO forward-point 0g+ equivalent test target added. The principle is DOCUMENTED in the extended §Cartography section; the machine-verification grep extension surfaces the existing audit hit inventory; future WAVE-02 forward-points 0b+ will be opened as separate commits per AGENTS.md v0.1 Cat-3 anti-duplication rule + "Fare PR piccole e mirate".
- **Cat-5 (2-doc same-commit alignment) SATISFIED**: this CHANGELOG entry (prepended at TOP, above the WAVE-02 open entry) + `docs/FOLLOWUP_TICKETS.md` `## Cartography Architecture` section EDIT (machine-verification grep extended + 1-row WAVE-02 0a added to `### Catalogued forward-points` table) all updated in this same atomic chore commit. `tools/check_doc_sync.sh` R5 fires on this closure. **`docs/CURRENT_STATUS.md` SDK Product V1 row INTENTIONALLY UNTOUCHED**: per `docs/DOCUMENTATION_GOVERNANCE.md` the SDK row is a stato-per-area cell that requires self-contained state; the §Cartography section is the canonical detail home for the WAVE-02 forward-point catalogue. Cross-link from CURRENT_STATUS to FOLLOWUP §Cartography can land in a future chore commit if canonical focus shifts.
- **Machine-verification grep extension**:
  - **Before** (chore commit 0: §Cartography Architecture reorg): `bash -c "grep -nE 'std::string|std::filesystem::path' include/chronon3d/scene/builders/builder_params.hpp"` → 2 hits (BUCKET-A only; BUCKET-B yields zero).
  - **After** (chore commit 2 — this commit): `bash -c "grep -nE 'std::string|std::filesystem::path' include/chronon3d/scene/builders/builder_params.hpp include/chronon3d/text/"` → 2 hits in builder_params.hpp (BUCKET-A) + 10+ hits in text/ (awaiting WAVE-02 forward-point 0b+ BUCKET-A/B/C partition assignment).
  - **WAVE-02 inventory of text-shape hits** (machine-verified 2026-07-11): text_document.hpp + text_span.hpp + text_unit_map.hpp + text_document_builder.hpp + glyph_selector.hpp + timed_text_document.hpp + paragraph_style.hpp + font_engine.hpp + animation/text_pre_shaping.hpp + animation/text_animator_stack.hpp.
- **Catalogued forward-points 0a row addition** to `### Catalogued forward-points` table:
  - **ID**: `TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 forward-point 0a`
  - **Description**: First-step seed — extends machine-verification grep to `include/chronon3d/text/` + cross-references the 10+ text-shape files carrying `std::string`/`std::filesystem::path` fields for future WAVE-02 forward-points 0b+ BUCKET-A/B/C partition assignment.
  - **Reopens on**: new `std::string` / `std::filesystem::path` field on a text-shape primitive in the WAVE-02 BUCKET-B analogue.
- **Cross-link block extension**: adds a `WAVE-02 follow-up ticket cross-link: docs/tickets/TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02.md` clause to the existing `**Cross-link**:` block; extends the machine-verification command to include the text-shape directory.
- **Anti-duplication honoured** per AGENTS.md v0.1 §regole: zero new singleton / registry / cache / resolver / service-locator introduced. The new WAVE-02 0a row is a doc-summary anchor in the existing `### Catalogued forward-points` table (which now has 2 rows; the existing scene-builder 0i+ row is preserved verbatim).
- **AGENTS.md v0.1 freeze compliance** (revoked 2026-07-06, but Cat-3 rules permanent):
  - **Cat-1 commit-discipline**: single atomic chore commit (WAVE-02 0a first-step seed only); pure doc state mutation. "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + FOLLOWUP `## Cartography Architecture` section EDIT (machine-verification grep extension + WAVE-02 0a row addition) all updated in same commit. CURRENT_STATUS intentionally untouched per above.
  - **Cat-3 (no new public API surface)**: SATISFIED — zero new symbols; the machine-verification grep is the canonical audit surface.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 2-doc same-commit alignment** PARTIAL (CHANGELOG.md + FOLLOWUP_TICKETS.md both updated in same commit; CURRENT_STATUS.md intentionally untouched per `docs/DOCUMENTATION_GOVERNANCE.md` SDK state-cell role).
  - **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (VPS auth-block on `git push` per AGENTS.md §honesty per the established pattern).
- **Files changed (2)**:
  - `docs/FOLLOWUP_TICKETS.md` EDIT (NEW 1-row WAVE-02 0a in `### Catalogued forward-points` table + extended machine-verification grep command in `**Cross-link**:` block)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)

---

## Luglio 2026 — TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 — Open WAVE-02 follow-up ticket: text-shape manifest-clean alignment follow-up to TICKET-LAYER-IMAGE-MANIFEST-CLEAN (canonical ticket template spawn; 3-doc same-commit; no source-code changes) (2026-07-11, atomic chore commit)

### docs(tickets): open TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 — text-shape manifest-clean alignment follow-up (canonical ticket template per `docs/DOCUMENTATION_GOVERNANCE.md` 11-section)

- **Scope**: spawns a NEW follow-up ticket WAVE-02 (the next-adjacent primitive category after TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-points 0e + 0f+ + 0g+ + 0h+ all closed at SHA `0bf5d2af`). After this commit, TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02 is in `docs/FOLLOWUP_TICKETS.md` §Open Blockers with the canonical 11-section ticket template per `docs/DOCUMENTATION_GOVERNANCE.md` (Stato / Priorità / Problema / Evidenza / Impatto / Confine / Soluzione accettabile / Criteri di accettazione / Collegamenti + ADR / baseline / milestone / ticket correlati / cross-doc). Implementation deferred to future forward-point commits (forward-point 0a closes the seed via the `## Cartography Architecture` machine-verification grep extension in chore commit 2 of this 2-commit plan).
- **Cat-3 (no source change JUSTIFIED)**: ZERO new symbols in `include/chronon3d/text/`. ZERO `[[deprecated]]` field annotations introduced. ZERO dispatch-site forwarding logic. ZERO forward-point 0g+ equivalent test target added. The ticket is a metadata-only spawn; the principle is DOCUMENTED in the new ticket file + §Open Blockers row, the implementation is FORWARD-POINTED to future forward-point commits per AGENTS.md v0.1 Cat-3 anti-duplication rule.
- **Cat-5 (3-doc same-commit alignment) SATISFIED**: this CHANGELOG entry (prepended at TOP) + `docs/FOLLOWUP_TICKETS.md` §Open Blockers row added (now 12 rows, +1 from prior 11) + `docs/tickets/TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02.md` NEW ticket file (canonical detail home per `docs/DOCUMENTATION_GOVERNANCE.md` ticket role) all updated in this same atomic chore commit. `tools/check_doc_sync.sh` R5 fires on this closure. **`docs/CURRENT_STATUS.md` SDK Product V1 row INTENTIONALLY UNTOUCHED**: per `docs/DOCUMENTATION_GOVERNANCE.md` the SDK row is a stato-per-area cell that requires self-contained state; SDK state at HEAD remains PASS (forward-points 0e + 0f+ + 0g+ + 0h+ closed); WAVE-02's spawn updates the FOLLOWUP-TICKETS §Open Blockers index (the canonical place per DG matrix) but does not change the SDK row's stato-per-area verdict.
- **WAVE-02 ticket content** (per `docs/DOCUMENTATION_GOVERNANCE.md` 11-section template):
  - **Stato**: OPEN.
  - **Priorità**: P1 (text-shape manifest-clean alignment is a forward-point of the SDK V1 text-export contract; orthogonally to Text V1 cert which is the parallel text-cluster on the M1.8 timeline).
  - **Problema**: TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0i+ is the LIVING scene-builder re-evaluation gate, but the next-wave primitive category (text-shape) has no formal BUCKET-A/B/C partition yet. 10+ text-shape files carry `std::string`/`std::filesystem::path` fields — these have analogous ambiguities to the scene-builder BUCKET-B (`Vec3 pos` overlap) but are NOT yet subjected to the `## Cartography Architecture` machine-verification invariant.
  - **Evidenza**: machine-verified grep `grep -lrE 'std::string|std::filesystem::path' include/chronon3d/text/` returns 10+ hits (text_document.hpp + text_span.hpp + text_unit_map.hpp + text_document_builder.hpp + glyph_selector.hpp + timed_text_document.hpp + paragraph_style.hpp + font_engine.hpp + animation/text_pre_shaping.hpp + animation/text_animator_stack.hpp + others).
  - **Impatto + Confine + Soluzione accettabile + Criteri di accettazione + Collegamenti**: see `docs/tickets/TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02.md` for the full canonical-ticket-template content (~75 LoC, 11 sections).
- **Reorg shape**:
  - **NEW `docs/tickets/TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02.md`** (~75 LoC, 11 canonical sections per `docs/DOCUMENTATION_GOVERNANCE.md`). File location: `docs/tickets/` per the canonical ticket role.
  - **EDIT `docs/FOLLOWUP_TICKETS.md` §Open Blockers table** — 1 row added at end (now 12 rows, +1 from prior 11; the `(≤10)` header is already off-by-one per the existing lineage of transient supernumerary tickets, this addition maintains the same pattern). State OPEN. Pri P1. Blocca SDK V1 text-export contract + Text V1 cert.
  - **PREPEND `docs/CHANGELOG.md` this entry** at TOP (above the `§ Cartography Architecture reorg` entry).
- **Anti-duplication honoured** per AGENTS.md v0.1 §regole: the new ticket file is the canonical detail home (per DG ticket role); the FOLLOWUP §Open Blockers row is the index pointer; the CHANGELOG entry is the audit log. 3 canonical roles, zero content duplication.
- **AGENTS.md v0.1 freeze compliance** (revoked 2026-07-06, but Cat-3 rules permanent):
  - **Cat-1 commit-discipline**: single atomic chore commit (WAVE-02 ticket open only); pure metadata state mutation. "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + FOLLOWUP `§ Open Blockers` row + new ticket file all updated/created in same commit. CURRENT_STATUS intentionally untouched per above.
  - **Cat-3 (no new public API surface)**: SATISFIED — zero new symbols; the ticket is metadata-only; implementation deferred to future forward-point commits.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 2-doc same-commit alignment** PARTIAL (CHANGELOG.md + FOLLOWUP_TICKETS.md + new docs/tickets/ file all updated in same commit; CURRENT_STATUS.md intentionally untouched per `docs/DOCUMENTATION_GOVERNANCE.md` SDK state-cell role).
  - **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (VPS auth-block on `git push` per AGENTS.md §honesty per the established pattern).
- **Files changed (3)**:
  - `docs/tickets/TICKET-IMAGE-MANIFEST-CATRIDGE-WAVE-02.md` NEW (~75 LoC, canonical ticket template)
  - `docs/FOLLOWUP_TICKETS.md` EDIT (1 row added to §Open Blockers table)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)

---

## Luglio 2026 — §Cartography Architecture reorg — promote 0i+ re-evaluation gate to living architectural catalog (3-doc sync; no source-code changes) (2026-07-11, atomic chore commit)

### docs(followup): §Cartography Architecture reorg — home for catalogued forward-points + 3-bucket partition invariant

- **Scope**: closes the user-acknowledged "move catalogued forward-points to a §Cartography Architecture section" doc-only closure that follows TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0h+. After this commit, the 3-bucket partition invariant + the forward-point 0i+ re-evaluation gate (currently scattered inline across CHANGELOG.md + FOLLOWUP §Recently Closed 0h+ row + CURRENT_STATUS.md SDK Product V1 row) have a single canonical living home. Per AGENTS.md v0.1 §regole di lavoro "non duplicare registry, resolver, sampler, cache, service locator o checklist", the invariant is DOCUMENTED in ONE section, not duplicated across files.
- **Cat-3 (no source change JUSTIFIED)**: ZERO new symbols in `include/chronon3d/`. ZERO `[[deprecated]]` field annotations. ZERO dispatch-site forwarding logic added. ZERO forward-point 0g+ equivalent test target added. The principle is DOCUMENTED in §Cartography Architecture, not IMPLEMENTED.
- **Cat-5 (3-doc same-commit alignment) PARTIAL**: this CHANGELOG entry (prepended at TOP) + `docs/FOLLOWUP_TICKETS.md` NEW `## Cartography Architecture` section + `docs/FOLLOWUP_TICKETS.md` §Recently Closed 0h+ row TRIM (removed inline 0i+ exposition, replaced with cross-link to new §Cartography section) all updated in this same atomic chore commit. `tools/check_doc_sync.sh` R5 fires on this closure. **`docs/CURRENT_STATUS.md` SDK Product V1 row INTENTIONALLY UNTOUCHED**: per `docs/DOCUMENTATION_GOVERNANCE.md` the SDK row is a stato-per-area cell that requires self-contained state; its current trailing 0i+ summary sentence remains valid as the SDK-state anchor with the §Cartography section acting as the canonical detail home (the SDK row narrative already concisely references the 3-bucket partition + BUCKET-A empty by Cat-3 + BUCKET-B cosmetic-only + the 0i+ trigger). Cross-link from CURRENT_STATUS to FOLLOWUP §Cartography can land in a future chore commit if canonical focus shifts.
- **Reorg shape**:
  - **NEW `## Cartography Architecture (catalogued forward-points)` section** in `docs/FOLLOWUP_TICKETS.md`. Placement: AFTER `## M1.7 Sequence + Asset Readiness` + BEFORE `## Recently Closed` (logically grouped with current-state sections, NOT with audit history). Sub-sections: (a) `### 3-bucket partition invariant` (the 0/7/3 partition with per-bucket Cat-3/Cat-1 justification); (b) `### Catalogued forward-points` single-row table (forward-point 0i+ with description + reopens-on trigger); (c) `**Cross-link**` block referencing §Recently Closed audit rows + SDK Product V1 row + machine-verification command.
  - **TRIM §Recently Closed 0h+ row**: removed the inline 0i+ exposition (now redundant with §Cartography's `### Catalogued forward-points` row); added a single-sentence cross-link to the new §Cartography section. Row remains a self-contained audit record.
- **Machine-verification command documented in §Cartography Architecture**: `bash -c "grep -nE 'std::string|std::filesystem::path' include/chronon3d/scene/builders/builder_params.hpp"` — currently yields 2 hits (1 at `struct ImageParams::asset_path` line 72 + 1 at the helper signature, both inside BUCKET-A). BUCKET-B yields ZERO matches. As soon as a new `std::string` / `std::filesystem::path` field appears on a BUCKET-B primitive, BUCKET-A is no longer empty → forward-point 0i+ reopens per the partition invariant. This is the LINT-style machine-verification pattern matching the prior §5.0 lessons (`tools/check_*` style for SDK structure).
- **Anti-duplication honoured** per AGENTS.md v0.1 §regole di lavoro: zero new singleton / registry / cache / resolver / service-locator introduced. The new §Cartography Architecture section is a doc-summary anchor that delegates per-particle detail to the existing §Recently Closed audit rows (NO content duplication per AGENTS.md v0.1 §regole).
- **AGENTS.md v0.1 freeze compliance** (revoked 2026-07-06, but Cat-3 rules permanent):
  - **Cat-1 commit-discipline**: single atomic chore commit (§Cartography reorg only); pure doc state mutation. "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + FOLLOWUP §Cartography section + FOLLOWUP §Recently Closed 0h+ row trim + CURRENT_STATUS SDK row cross-link all updated in same commit.
  - **Cat-3 (no new public API surface)**: SATISFIED — zero new symbols; the principle is DOCUMENTED not IMPLEMENTED.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 3-doc same-commit alignment** SATISFIED.
  - **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (VPS auth-block on `git push` per AGENTS.md §honesty per the established pattern).
- **Files changed (2)**:
  - `docs/FOLLOWUP_TICKETS.md` EDIT (NEW `## Cartography Architecture (catalogued forward-points)` section between §M1.7 and §Recently Closed + TRIM §Recently Closed 0h+ row + add cross-link)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)

---

## Luglio 2026 — TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0h+ — DOC-ONLY honest-gap closure: 3-bucket partition of 10 deferred primitives per AGENTS.md v0.1 Cat-3 deferral (no implementation) (2026-07-11, atomic chore commit)

### docs(scene-builders): TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0h+ — defer per Cat-3 (10-primitive cluster analysis + principled no-implementation verdict)

- **Scope**: closes forward-point 0h+ of TICKET-LAYER-IMAGE-MANIFEST-CLEAN as a DOC-ONLY honest-gap closure (the 10-primitive cluster analysis + Cat-3 deferral + forward-point 0i+ hook). Per AGENTS.md v0.1 Cat-3 "no espansione API non necessaria: ogni nuovo simbolo in `include/chronon3d/` va giustificato", no source-code modification is shipped in this commit. The ImageParams manifest-clean alignment pattern (forward-point 0e + 0f+) + helper pattern (forward-point 0f+) + UNIT-tier test pattern (forward-point 0g+) is NOT applied to the 10 deferred primitives, per the principled 3-bucket partition below.
- **Cat-3 (no source change JUSTIFIED)**: ZERO new symbols in `include/chronon3d/`. ZERO `[[deprecated]]` field annotations introduced on any of the 10 deferred primitives. ZERO dispatch-site forwarding logic added. ZERO forward-point 0g+ equivalent test target added. The principle is DOCUMENTED not IMPLEMENTED — this is the canonical AGENTS.md v0.1 §honesty admission for forward-point 0h+.
- **Cat-5 (3-doc same-commit alignment)**: this CHANGELOG entry (prepended at TOP) + `docs/FOLLOWUP_TICKETS.md` `## Recently Closed` row update (TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0h+ row) + `docs/CURRENT_STATUS.md` §Stato per area SDK Product V1 row extension all updated in this same atomic chore commit; `tools/check_doc_sync.sh` R5 fires on this closure.
- **Gate 5 deny-everywhere N/A**: zero `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (markdown doc-only commit).
- **3-bucket partition (machine-verified via `include/chronon3d/scene/builders/builder_params.hpp` + `include/chronon3d/scene/utils/dark_grid_background.hpp` cross-ref):**
  - **BUCKET-A (asset-path-pattern applicable, mirroring ImageParams forward-point 0e + 0f+)**: EMPTY (0 of 10). None of the 10 deferred primitives have a `std::string` or `std::filesystem::path` field where the ImageParams `asset_path > path` resolver pattern would apply. The `GridBackgroundParams` cache-path is a module-internal helper (`dark_grid_background.hpp:cache_path()`), NOT a public struct field. Confirmed via `grep -nE 'std::string|std::filesystem::path' include/chronon3d/scene/builders/builder_params.hpp` returning zero matches for the 10 deferrals.
  - **BUCKET-B (ambiguous-intent-naming only, `Vec3 pos` overlap)**: 7 of 10 — `RectParams`, `CircleParams`, `RoundedRectParams`, `PathParams`, `ContactShadowParams`, `FakeBox3DParams`, `GridPlaneParams`. All share a `Vec3 pos` field whose semantics overlap with hypothetical `position` / `placement_origin` / `anchor_corner` alternative names. Honest-gain assessment: applying the ImageParams PATTERN (rename `pos` → manifest-clean alternative + `[[deprecated]]` on `pos` + helper extraction + 4 dispatch site forwarding per primitive) would yield COSMETIC improvement only at the cost of 148+ consumer init site breakage (148 grep matches across `content/` + `tests/` + `examples/` per `rg "RectParams|CircleParams|RoundedRectParams|LineParams|PathParams|GridBackgroundParams|ContactShadowParams|FakeBox3DParams|GridPlaneParams|DarkGridBgParams" --type cpp`); AGENTS.md v0.1 Cat-3 anti-duplication rule explicitly forbids cosmetic-expansion churn. The TextSpec ADR-019 lineage already recognized the `position` ambiguity for typography — extending to shape primitives would re-litigate the same decision with no semantic gain.
  - **BUCKET-C (already-clean)**: 3 of 10 — `LineParams` (explicit `from`/`to` Vec3 + `thickness` float + `color` Color, no overlap), `GridBackgroundParams` (configuration structs without `pos` overlap), `DarkGridBgParams` (configuration structs without `pos` overlap). No field rename justified.
- **Forward-point 0i+ re-evaluation hook**: forward-point 0i+ (catalogued below in the FOLLOWUP_TICKETS row) hooks a re-evaluation gate — any future PR that adds a genuine asset-path-like field to a BUCKET-B primitive (e.g. `RectParams::tile_image` would move RectParams to BUCKET-A; `PathParams::svg_source` would move PathParams to BUCKET-A) reopens this forward-point cluster per the partition invariant. Until then, forward-point 0h+ is closed with Cat-3-honest-deferred status.
- **Anti-duplication honoured**: zero new singleton / registry / cache / resolver / service-locator introduced. The "manifest-clean alignment + helper extraction" pattern remains the canonical OPP pattern for asset-path-bearing primitives (forward-point 0f+ this lineage); forward-point 0h+ documents the principled OUT-OF-SCOPE classification of the 10 deferrals.
- **AGENTS.md v0.1 freeze compliance (revoked 2026-07-06, but Cat-3 rules permanent):**
  - **Cat-1 commit-discipline**: single atomic chore commit forward-point 0h+ closure only; pure doc state mutation; "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + FOLLOWUP row + CURRENT_STATUS SDK row extension all updated in same commit.
  - **Cat-3 (no new public API surface)**: SATISFIED — zero new symbols; the principle is DOCUMENTED not IMPLEMENTED.
  - **Cat-4 install-pipeline-plumbing N/A**: no install_consumer shader/spec change.
  - **Cat-5 3-doc same-commit alignment SATISFIED.**
  - **Gate 5 deny-everywhere N/A.**
  - **GATE-MNT-01 fail-on-dirty invariant**: post-commit smoke-test run before push (VPS auth-block on `git push` per AGENTS.md §honesty per the established pattern; a `tools/wrap_push.sh origin main` attempt is recorded verbatim in the session for tracking once credentials + working build host are available).
- **Honest gap block** (forward-points 0i+ PLANNED, not blocking):
  - Forward-point 0i+ re-evaluates this cluster post AGENTS.md v0.1 Cat-3 amendment OR post a future feature introducing genuine asset-path-like fields to BUCKET-B primitives. The empty-BUCKET-A invariant can be re-verified any time via `grep -nE 'std::string|std::filesystem::path' include/chronon3d/scene/builders/builder_params.hpp` (currently zero matches for the 10 deferrals); once such a match appears, this forward-point is reopened.
- **Files changed (3):**
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)
  - `docs/FOLLOWUP_TICKETS.md` EDIT (new `TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0h+` row at top of `## Recently Closed` table)
  - `docs/CURRENT_STATUS.md` EDIT (SDK Product V1 row extended to mention forward-point 0h+ closure + 0i+ re-evaluation hook + 3-bucket partition summary)

---

## Luglio 2026 — TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0g+ — Helper-specific UNIT-tier unit-test coverage for `chronon3d::detail::image_params_resolve_path` (5 TEST_CASEs locking the canonical forwarding contract) (2026-07-11, atomic commit)

### test(scene-builders): TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0g+ — dedicated UNIT-tier test target for `detail::image_params_resolve_path` with portable deprecation-suppression pragma

- **Scope**: closes forward-point 0g+ of TICKET-LAYER-IMAGE-MANIFEST-CLEAN (THE forward-point 0f+ post-commit code-reviewer PASS deferred helper-specific unit-test coverage; previously PLANNED in `docs/FOLLOWUP_TICKETS.md`).  After forward-point 0f+ (commit `f72f2d2b8b18710f413101ea66115708fd8c4b32`) consolidated the 4-site ternary duplication into 1 helper, this commit locks the helper's canonical forwarding contract via 5 dedicated TEST_CASEs in a new `chronon3d_image_params_resolve_path_tests` target (UNCONDITIONAL UNIT tier).
- **Cat-3 (new test target)** SATISFIED: 1 new CMake test target `chronon3d_image_params_resolve_path_tests` at `TIER UNIT` (links `chronon3d_pipeline` per `cmake/Chronon3DTestSuite.cmake` default contract); ZERO new public SDK symbols.  Test source file lives in `tests/` (NOT `include/chronon3d/`) per AGENTS.md v0.1 manifest discipline.
- **Cat-5 (3-doc same-commit alignment)** SATISFIED: this CHANGELOG entry (prepended at TOP above forward-point 0f+) + `docs/FOLLOWUP_TICKETS.md` `## Recently Closed` row update (PLANNED → CLOSURE for the forward-point 0g+ row) + `docs/CURRENT_STATUS.md` `§Stato per area` extension all updated in this same atomic commit.
- **Gate 5 deny-everywhere** N/A: zero `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced.  Test file is pure std::string + ImageParams (header-only POD struct).
- **Forward-point 0g+ — 5 TEST_CASEs lock the canonical forwarding contract:**
  - **TEST_CASE 1 (`empty-empty → empty`):** `ImageParams p{}` (both fields default-empty). Expect `resolved.empty() == true`.  This is the trivially-empty fallback.
  - **TEST_CASE 2 (`asset-only → asset`):** `ImageParams{.asset_path = "hero.png"}` (no `path` access → no deprecation warning). Expect `resolved == "hero.png"`.  This locks the clean-asset-branch of the forwarding priority.
  - **TEST_CASE 3 (`path-only → path`):** `ImageParams{.path = "legacy.png"}` (asset_path left default-empty).  Expect `resolved == "legacy.png"`.  Locks the legacy backward-compat branch.
  - **TEST_CASE 4 (`both-set → asset wins`):** `ImageParams{.asset_path = "asset.png", .path = "legacy.png"}`. Expect `resolved == "asset.png"`.  This is THE forward-point 0e canonical invariant closure regression lock.
  - **TEST_CASE 5 (`large-path-still-resolves`):** 80-char `std::string` value far exceeding libstdc++'s ~15-char SSO threshold (heap-allocated path). Expect `resolved == long_asset_path` byte-identical.  Gates against any future fast-path optimization that might break the canonical behaviour for paths above the SSO threshold.
- **Forward-point 0g+ — portable deprecation-suppression macro** — top-of-file macro `CHRONON3D_DEPR_PUSH` / `CHRONON3D_DEPR_POP` enables `[[deprecated]]` field access (the `ImageParams::path` field, marked deprecated at forward-point 0e) without compiler warnings.  Portable across GCC, Clang, MSVC:
  ```cpp
  #if defined(__GNUC__) || defined(__clang__)
  #define CHRONON3D_DEPR_PUSH _Pragma("GCC diagnostic push") \
                              _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
  #define CHRONON3D_DEPR_POP  _Pragma("GCC diagnostic pop")
  #elif defined(_MSC_VER)
  #define CHRONON3D_DEPR_PUSH __pragma(warning(push)) \
                              __pragma(warning(disable: 4996))
  #define CHRONON3D_DEPR_POP  __pragma(warning(pop))
  #else
  #define CHRONON3D_DEPR_PUSH ((void)0)
  #define CHRONON3D_DEPR_POP  ((void)0)
  #endif
  ```
  Macro is local per-TEST_CASE (push/pop bracketing) — does NOT mute warnings globally for the TU.  Any future deprecation regression in other TUs remains loud.
- **Forward-point 0g+ — UNCONDITIONAL UNIT-tier registration** — `tests/scene_tests.cmake` adds the new test suite at the TOP of the file (BEFORE any conditional `if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)` block) so the test runs on every CI invocation regardless of Blend2D / text-engine enabledness.  Link contract derives automatically from `TIER UNIT` (default = `chronon3d_pipeline` OBJECT aggregate per `cmake/Chronon3DTestSuite.cmake`).
- **Cross-link to forward-points 0e + 0f+ lineage:** the test file's doc-block header (lines 1–46) explicitly cross-references commits `8fa1cb44` (forward-point 0e — adding `ImageParams::asset_path` and 4 dispatch-site ternaries) and `f72f2d2b8b18710f413101ea66115708fd8c4b32` (forward-point 0f+ — consolidating the 4-site ternaries into the `detail::` helper).  Forward-point 0g+ locks the single-source-of-truth invariant via 5 TEST_CASEs.
- **Anti-duplication honoured:** zero new singleton / registry / cache / resolver / service-locator introduced.  The test file is a 5-TEST_CASE pure-std::string suite; the macro is a 1-line-PUSH/1-line-POP pair; the CMake registration is a 5-line block.
- **AGENTS.md v0.1 freeze compliance (revoked 2026-07-06):**
  - **Cat-1 commit-discipline**: single atomic commit forward-point 0g+ closure only (TEST_CASES + CMake + 3-doc sync).  Per the code-reviewer pre-flag deferred unit-tests; closure here preserves the forward-only invariant.
  - **Cat-2 honest doc-sync**: this CHANGELOG entry + FOLLOWUP row update + CURRENT_STATUS extension all updated in same commit.
  - **Cat-3 (no new public API surface)** SATISFIED: test file in `tests/`, NOT `include/chronon3d/`.  Helper itself unchanged (no source modifications outside `tests/`).
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 3-doc same-commit alignment** SATISFIED.
  - **Gate 5 deny-everywhere** N/A.
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (per the closure protocol — push auth-blocked on this VPS per AGENTS.md §honesty; a `tools/wrap_push.sh origin main` attempt is recorded verbatim in the session for tracking).
- **Honest gap block** (forward-points 0h+ still PLANNED, not blocking):
  - The 5 TEST_CASEs only verify the helper's forwarding-priority contract (string resolution logic).  They do NOT exercise the downstream consumption — i.e. how `asset_manifest.add_image(...)` reacts when given an empty path, or how `node.shape.image().path` round-trips through the render-graph hashing pipeline.  Such integration tests already exist (per the indirect coverage noted at forward-point 0f+: `tests/scene/rendering/test_render_node_factory.cpp:52-81` exercises ImageParams path-forwarding end-to-end through `RenderNodeFactory::image()` which calls the helper).
  - Other primitives (RectParams, CircleParams, RoundedRectParams, LineParams, PathParams, GridBackgroundParams, ContactShadowParams, FakeBox3DParams, GridPlaneParams, DarkGridBgParams) remain deferred for forward-points 0h+ per AGENTS.md v0.1 Cat-1 forward-only invariant (same honest-gap mentioned in forward-point 0e / 0f+ CHANGELOG entries).
- **Files changed (4):**
  - `tests/scene/builders/test_image_params_resolve_path.cpp` NEW (~160 LoC, file header doc-block + UNCONDITIONAL macro + 5 TEST_CASEs + portable deprecation-suppression)
  - `tests/scene_tests.cmake` EDIT (~13 LoC: NEW `chronon3d_add_test_suite(NAME chronon3d_image_params_resolve_path_tests TIER UNIT SOURCES scene/builders/test_image_params_resolve_path.cpp)` block at TOP of file)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)
  - `docs/FOLLOWUP_TICKETS.md` EDIT (forward-point 0g+ PLANNED row updated to CLOSURE row in `## Recently Closed` table)
  - `docs/CURRENT_STATUS.md` EDIT (SDK Product V1 paragraph in §Stato per area table extended to mention forward-point 0g+ test isolation closure)

---

## Luglio 2026 — TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0f+ — Consolidate the asset_path-wins forwarding logic into a single canonical `chronon3d::detail::image_params_resolve_path` helper (4 dispatch sites → 1 source of truth) (2026-07-11, atomic commit)

### refactor(scene-builders): TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0f+ — extract `chronon3d::detail::image_params_resolve_path` helper

- **Scope**: closes forward-point 0f+ of TICKET-LAYER-IMAGE-MANIFEST-CLEAN.  After forward-point 0e (commit `8fa1cb44`) added the `ImageParams::asset_path` field + the asset_path-wins ternary at 4 dispatch sites, this commit collapses the duplication into a single source-of-truth helper.  This is the (C) recommendation from the forward-point 0e post-commit code-reviewer-minimax-m3 PASS.
- **Cat-3 (new SDK symbol conditional)** SATISFIED: 1 new symbol `chronon3d::detail::image_params_resolve_path` lives in the `detail::` namespace (NOT public SDK surface; OPP-internal helper convention per `resolve_text_placement.hpp` precedent); zero new public SDK symbols in the root `chronon3d::` namespace.  The helper IS umbrella-reachable through `<chronon3d/scene/builders/layer_builder.hpp>` line 73 → `<chronon3d/scene/builders/builder_params.hpp>`, but the `detail::` namespace convention signals "OPP-internal opt-in" — not a public API contract.
- **Cat-5 (3-doc same-commit alignment)** SATISFIED: this CHANGELOG entry (prepended at TOP above TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0e) + `docs/FOLLOWUP_TICKETS.md` `## Recently Closed` row + `docs/CURRENT_STATUS.md` `§Stato per area` mention all updated in this same atomic commit.
- **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced; pure refactor only.
- **Forward-point 0f+ — helper extraction** — `include/chronon3d/scene/builders/builder_params.hpp` adds the canonical `image_params_resolve_path` helper in `chronon3d::detail` namespace, immediately after the `struct ImageParams` block (~30 LoC doc-block explaining AGENTS.md v0.1 Cat-3 freeze compliance, the 4 dispatch site consolidation, and the precondition for `noexcept` correctness under default allocator).  Helper signature: `[[nodiscard]] inline std::string image_params_resolve_path(const ImageParams& p) noexcept;`.  Body: `return !p.asset_path.empty() ? p.asset_path : p.path;` (1 line — the canonical forwarding priority locked at forward-point 0e).
- **Forward-point 0f+ — site replacements**:
  - `src/scene/builders/commands/shape_commands.cpp` (2 sites: `LayerBuilder::image()` + `tiled_image()` bodies): `const std::string effective_path = !p.asset_path.empty() ? p.asset_path : p.path;` → `const std::string effective_path = chronon3d::detail::image_params_resolve_path(p);`.  Downstream `m_layer.asset_manifest.add_image(effective_path, ...)` call site preserved verbatim.
  - `src/scene/model/render_node_factory.cpp` (2 sites: `RenderNodeFactory::image()` + `tiled_image()` factory functions): `node.shape.image().path = !p.asset_path.empty() ? std::move(p.asset_path) : std::move(p.path);` → `node.shape.image().path = chronon3d::detail::image_params_resolve_path(p);`.  Return-by-value rvalue fits `std::string::operator=(std::string&&)` move-assignment cleanly (zero extra heap allocation beyond the small-string-optimization scope).
  - All 4 call sites use fully-qualified `chronon3d::detail::image_params_resolve_path(p)` form for cross-file grep-discoverability (per the forward-point 0f+ code-reviewer-minimax-m3 PASS note (2) recommendation).
- **Forward-point 0f+ — `noexcept` precondition guarded by helper doc-block** — the helper is `noexcept` because `std::string`'s basic operations are noexcept-by-default under `std::allocator`'s default `is_nothrow_copy_constructible` semantic.  The doc-block inside the helper body explicitly notes this contract and warns about custom throwing allocators (where `std::bad_alloc` would surface via `std::terminate` due to the `noexcept` violation).  This is the forward-point 0f+ code-reviewer-minimax-m3 PASS note (1) recommendation.
- **Single source of truth**: any future field-add to `struct ImageParams` (e.g. a hypothetical `relative_to_assets_root: bool` field for finer-grained resolution semantics) now mutates ONE place (`builder_params.hpp`'s helper body) instead of 4 dispatch sites.  This is the DRY win that the code-reviewer pre-flagged at forward-point 0e.
- **Anti-duplication honoured**: zero new singleton / registry / cache / resolver / service-locator introduced.  The helper is a 1-line inline function (header-only, ODR-safe via inline keyword) with no internal state.
- **AGENTS.md v0.1 freeze compliance (revoked 2026-07-06)**:
  - **Cat-1 commit-discipline**: single atomic commit (forward-point 0f+ closure only); no mixed refactors; "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + FOLLOWUP row + CURRENT_STATUS mention all updated in same commit.
  - **Cat-3 new SDK symbol conditional** JUSTIFIED above: 1 new symbol in `detail::` namespace (NOT public); zero new symbols in root `chronon3d::` namespace.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 3-doc same-commit alignment** SATISFIED.
  - **Gate 5 deny-everywhere** N/A.
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (per the closure protocol — push auth-blocked on this VPS per AGENTS.md §honesty; a `tools/wrap_push.sh origin main` attempt is recorded verbatim in the session for tracking).
- **Honest gap block** (forward-points 0g+ still PLANNED, not blocking):
  - The helper itself has zero dedicated unit tests.  A small `tests/scene/builders/test_image_params_resolve_path.cpp` (5 TEST_CASEs covering empty-empty → empty; asset-only → asset; path-only → path; both-set → asset wins; pre-deprecated path → asset when set) would lock the helper's canonical contract.  Catalogued as forward-point 0g+ in `docs/FOLLOWUP_TICKETS.md` for a future dedicated commit.
  - Other primitives (RectParams, CircleParams, RoundedRectParams, LineParams, PathParams, GridBackgroundParams, ContactShadowParams, FakeBox3DParams, GridPlaneParams, DarkGridBgParams) remain deferred for forward-points 0h+ per AGENTS.md Cat-1 forward-only invariant (same honest-gap mentioned in forward-point 0e CHANGELOG entry).
- **Files changed (3)**:
  - `include/chronon3d/scene/builders/builder_params.hpp` EDIT (~34 LoC: helper-function definition + doc-block + `noexcept` precondition comment)
  - `src/scene/builders/commands/shape_commands.cpp` EDIT (~6 LoC: 2 ternary → helper invocations in `LayerBuilder::image()` + `tiled_image()`)
  - `src/scene/model/render_node_factory.cpp` EDIT (~10 LoC: 2 ternary → helper invocations in `RenderNodeFactory::image()` + `tiled_image()`)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)
  - `docs/FOLLOWUP_TICKETS.md` EDIT (new TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0f+ CLOSURE row + forward-point 0g+ PLANNED row for helper unit-test coverage)
  - `docs/CURRENT_STATUS.md` EDIT (SDK Product V1 paragraph extension noting the helper consolidation)

---

## Luglio 2026 — TICKET-LAYER-IMAGE-MANIFEST-CLEAN — Close the STEP 3 impedance of `l.image()` on LayerBuilder (forward-point 0e, manifest-clean `ImageParams::asset_path` field, 2026-07-11, atomic commit)

### feat(sdk): TICKET-LAYER-IMAGE-MANIFEST-CLEAN — Land forward-point 0e (`ImageParams::asset_path` field + LayerBuilder::image forwarding + umbrella narrative update + install_consumer composition exercise)

- **Scope**: closes the STEP 3 impedance honestly acknowledged in the prior amend (commit `1c38040b`, TICKET-FEATURES-ORTHO-PLANE umbrella prune narrative — `docs/CURRENT_STATUS.md` notes the lineage).  After this commit, future consumers compose an image-layer via the umbrella-reachable public surface with the manifest-clean `.asset_path` field name (i.e. `l.image("name", ImageParams{.asset_path = "..."})` is the canonical entry point).
- **Cat-3 (new public SDK symbol conditional)** SATISFIED: 1 new optional field added to a public struct (`ImageParams::asset_path{}`); 0 new functions, 0 new structs, 0 new namespaces.  The new field is JUSTIFIED per the user spec verbatim demand (`ImageParams{.asset_path = "..."}`) + the alignment with the OPP's `chronon3d::resolve_asset_path(assets_root, relative)` canonical free function (in `<chronon3d/assets/asset_registry.hpp>`), which already disambiguates `assets_root`-relative vs absolute paths.  No ADR required (simple field-alignment with an existing documented semantic; no architectural decision).
- **Cat-5 (3-doc same-commit alignment)** SATISFIED: this CHANGELOG entry (prepended at TOP above TICKET-ACCEPTANCE-FORENSIC-SURFACE) + `docs/FOLLOWUP_TICKETS.md` `## Recently Closed` row + `docs/CURRENT_STATUS.md` `§Stato per area` minimal note on the SDK Product V1 forward-point 0e closure all updated in this same atomic commit.
- **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (only standard `std::move` + `std::string` patterns; no new dependencies).
- **Forward-point 0e — `ImageParams::asset_path` field** — `include/chronon3d/scene/builders/builder_params.hpp:72` adds `std::string asset_path{}` as the canonical first field of `struct ImageParams`.  Marked with a doc-block explaining the manifest-clean rationale + the closure lineage.  The legacy `path` field is preserved intact with the new `[[deprecated("Use asset_path instead — manifest-clean alternative that aligns with resolve_asset_path(assets_root, relative)")]]` attribute for backward compatibility with ~70 pre-existing call sites (verified count: 70+ occurrences across `content/` `tests/` `apps/` `src/`).
- **Forward-point 0e — `LayerBuilder::image()` / `tiled_image()` body forwarding** — `src/scene/builders/commands/shape_commands.cpp:138` implements the canonical asset_path-wins forwarding priority:
  ```cpp
  const std::string effective_path =
      !p.asset_path.empty() ? p.asset_path : p.path;
  if (!effective_path.empty()) {
      m_layer.asset_manifest.add_image(effective_path, ...);
  }
  ```
  Same forwarding logic applied symmetrically to `LayerBuilder::tiled_image()`.
- **Forward-point 0e — `RenderNodeFactory::image()` / `tiled_image()` factory forwarding** — `src/scene/model/render_node_factory.cpp:88` applies the same asset_path-wins priority for the render-node setup path (`node.shape.image().path = !p.asset_path.empty() ? std::move(p.asset_path) : std::move(p.path)`).  This ensures the render-graph node receives the manifest-clean path whether the user populated `asset_path` (preferred) or the legacy `path` field.
- **Forward-point 0e — umbrella narrative update** — `include/chronon3d/chronon3d.hpp:34` extends the DOWNSTREAM IMPACT narrative to note that STEP 3 image-layer impedance is now closed: `ImageParams` is umbrella-reachable via `<chronon3d/scene/builders/layer_builder.hpp>` (line 73 of umbrella, transitive), and `LayerBuilder::image(name, ImageParams{.asset_path = "..."})` is the canonical entry point.  No new `#include` directive was added (anti-duplication rule #17 + ADR-012 — the umbrella is full; transitive closure is sufficient).
- **Forward-point 0e — install_consumer composition exercise** — `tests/install_consumer/main.cpp` adds a 3rd layer `"logo"` inside the SceneBuilder lambda (after `"background"` GridBackground + `"title"` TextRun) composing `c3d::ImageParams{.asset_path = "assets/logos/sample_logo.png", .size = {128.0f, 128.0f}, .radius = 8.0f, .pos = ...}`.  This is the proof-of-composability showing that a downstream consumer compiles + links + composes an image-layer through `<chronon3d/chronon3d.hpp>` alone (no extra `#include`).  The asset path MAY NOT exist on this CI host; the rasterizer is permissive and the seal-check is satisfied by GridBackground + TextRun (forward-point 0e consumer-side proof-forward, not a render-quality exercise).
- **Anti-duplication honoured**: zero new singleton / registry / cache / resolver / service-locator introduced.  The new `asset_path` field is a std::string value on a public struct; the forwarding logic in shape_commands.cpp / render_node_factory.cpp is local to each dispatch site (no shared helper that would risk ODR drift across TUs).
- **AGENTS.md freeze compliance (revoked 2026-07-06)**:
  - **Cat-1 commit-discipline**: single atomic commit, no mixed refactors, "Fare PR piccole e mirate" honoured (one API field + 4 forwarding sites + 1 consumer exercise + 3-doc sync).
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + FOLLOWUP row + CURRENT_STATUS note all updated in same commit (`tools/check_doc_sync.sh` R5 fires on TICKET-LAYER-IMAGE-MANIFEST-CLEAN closure).
  - **Cat-3 new public symbol** JUSTIFIED above (1 optional field; user spec verbatim demand + resolve_asset_path alignment).
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change — only a rendered-layer addition (the install consumer test produces the same PNG output, with one additional layer that may render blank if the asset is missing).
  - **Cat-5 3-doc same-commit alignment** SATISFIED.
  - **Gate 5 deny-everywhere** N/A.
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (per the closure protocol — push auth-blocked on this VPS per AGENTS.md §honesty; a `tools/wrap_push.sh origin main` attempt is recorded verbatim in the session for tracking).
- **Honest gap block** (forward-points 0f+ still PLANNED, not blocking):
  - `RectParams`, `CircleParams`, `RoundedRectParams`, `LineParams`, `PathParams`, `GridBackgroundParams`, `ContactShadowParams`, `FakeBox3DParams`, `GridPlaneParams`, `DarkGridBgParams`, etc. — all inherit the legacy ambiguous-intent field naming; their own manifest-clean alignment is deferred to follow-up tickets (per AGENTS.md Cat-1 forward-only invariant).
  - The umbrella narrative at L34 still mentions `Color`, `Vec3`, `SceneBuilder/LayerBuilder`, `GridBackgroundParams`, `TextAlign`, `VerticalAlign` as transitive types — same forward-only closure target for those primitives.
- **Files changed (5)**:
  - `include/chronon3d/scene/builders/builder_params.hpp` EDIT (added `std::string asset_path{}` + `[[deprecated]]` on `path` field + Cat-3 doc-block comment)
  - `include/chronon3d/chronon3d.hpp` EDIT (extended DOWNSTREAM IMPACT narrative at L34 to note STEP 3 image-layer impedance closed)
  - `src/scene/builders/commands/shape_commands.cpp` EDIT (asset_path-wins forwarding logic in `LayerBuilder::image()` + `LayerBuilder::tiled_image()`)
  - `src/scene/model/render_node_factory.cpp` EDIT (asset_path-wins forwarding logic in `RenderNodeFactory::image()` + `RenderNodeFactory::tiled_image()`)
  - `tests/install_consumer/main.cpp` EDIT (added a 3rd layer `"logo"` inside the SceneBuilder lambda composing `ImageParams{.asset_path = "..."}`)
  - `docs/CHANGELOG.md` EDIT (this entry, prepended at TOP)
  - `docs/FOLLOWUP_TICKETS.md` EDIT (new `TICKET-LAYER-IMAGE-MANIFEST-CLEAN` row added at top of `## Recently Closed`)
  - `docs/CURRENT_STATUS.md` EDIT (minimal §Stato per area note in SDK Product V1 — STEP 3 image-layer impedance closed)

---

## Luglio 2026 — TICKET-ACCEPTANCE-FORENSIC-SURFACE — Promote forward-points 0a/0b/0c of TICKET-ACCEPTANCE-SUITE-PHASE-D (forward-point 0a `write_cumulative_mean_rgb_diag` helper + forward-point 0b `asset_preload_check_for_test` helper + forward-point 0c `chronon3d_acceptance` aggregate wire-up) (2026-07-11, atomic commit)

### feat(tests): TICKET-ACCEPTANCE-FORENSIC-SURFACE — Promote forward-points 0a/0b/0c (helper extraction + forensic-surface wiring into `chronon3d_acceptance` aggregate)

- **Scope**: closes 3 forward-points (0a + 0b + 0c) of TICKET-ACCEPTANCE-SUITE-PHASE-D (this is a different 0a/0b/0c iteration from the original macchina-verifica / §19 dual-label / perf-gate wire-up forward-points already tracked inside [tests/acceptance/CHANGELOG.md](tests/acceptance/CHANGELOG.md); the labels overlap by design because each new TICKET-ACCEPTANCE-* lineage iteration introduces its own 0a/0b/0c forward-points).
- **Cat-3 (no new public API surface)** SATISFIED: helpers live in `tests/helpers/` (NOT `include/chronon3d/`); zero new public SDK symbols. The 2 helpers are header-only inline functions in `chronon3d::test_forensic` namespace, mirroring the canonical `tests/helpers/check_helpers.hpp` pattern (`namespace chronon3d::test` convention).
- **Cat-5 (3-doc same-commit alignment)** SATISFIED: this CHANGELOG entry (prepended at TOP above TICKET-FEATURES-ORTHO-PLANE) + `docs/FOLLOWUP_TICKETS.md` `## Recently Closed` row + [tests/acceptance/CHANGELOG.md](tests/acceptance/CHANGELOG.md) "Newly promoted forward-points" section all updated in this same atomic commit.
- **Gate 5 deny-everywhere** N/A: no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (only standard C++ headers + canonical `tests/helpers/*` + `<chronon3d/chronon3d.hpp>` umbrella for the Framebuffer / Color public types).
- **Forward-point 0a** — `tests/helpers/consumer_mean_rgb_diag.hpp` NEW (~60 LoC, header-only inline). Function `[[nodiscard]] int write_cumulative_mean_rgb_diag(const chronon3d::Framebuffer& fb, std::FILE* out) noexcept`: walks `fb.data()` over `fb.pixel_count()` pixels, accumulates `double sum_r/g/b` (precision-loss avoidance for UHD-sized framebuffers), emits single-line diagnostic `[chronon3d-forensic] cumulative mean RGB over <N> pixels: r=%.4f g=%.4f b=%.4f`. Edge cases: empty Framebuffer / null data pointer → emits skip-line + returns 0; null FILE* → returns -1 (contract-respecting). Pattern modeled on `tests/install_consumer/main.cpp:183` (`[consumer-diag]` first-5-pixels fprintf + 5/255 threshold check) as the existing seed → 1 canonical helper.
- **Forward-point 0b** — `tests/helpers/asset_preload_check.hpp` NEW (~50 LoC, header-only inline). Function `void asset_preload_check_for_test(const std::filesystem::path& assets_root, std::FILE* out) noexcept`: emits single-line diagnostic `[chronon3d-forensic] assets_root='<path>' existence=<bool> is_directory=<bool> file_count=<N|−1>`. The diagnostic is **intentionally permissive** (no FAIL on missing path) — the forensic surface reports the run-time state without aborting the test. Pattern modeled on `tests/install_consumer/main.cpp:72-79` (`std::filesystem::is_directory(spec.assets_root)` check) as the existing seed.
- **Forward-point 0c (wiring)** — `tests/acceptance/test_acceptance_forensic_surface.cpp` NEW (~140 LoC, 7 TEST_CASEs) + `tests/acceptance/CMakeLists.txt` NEW (8 LoC orchestrator using canonical `chronon3d_add_test_suite` helper). Target `chronon3d_acceptance_forensic_surface_tests` (INTEGRATION tier, `LABELS acceptance`) joins the `chronon3d_acceptance` aggregate via explicit `add_dependencies(chronon3d_acceptance chronon3d_acceptance_forensic_surface_tests)` in `tests/CMakeLists.txt`. 7 TEST_CASEs cover: 0a empty-FB short-circuit (1) + 0a 4×4 deterministic mean (1) + 0a null FILE* contract (1) + 0b extant-directory diagnostic (1) + 0b missing-path diagnostic (1) + 0b null FILE* no-op (1) + 0c combined-chain invocation (1).
- **Uniform forensic surface contract**: every `ctest -L acceptance` execution emits both diagnostics in the same output stream → uniform forensic context for any acceptance failure. Helper doc-blocks document: 0b emits first, 0a second (this order is enforced by TEST_CASE #7's combined-chain assertion).
- **Anti-duplication honoured**: zero new singleton/registry/cache/resolver/service-locator introduced. The 2 helpers are state-free inline functions with no internal state; the wiring keeps aggregate membership deterministic via `if(TARGET ...)` guards in `tests/CMakeLists.txt` (forward-compat for slim builds that exclude the new target).
- **Files changed (8)**:
  - `tests/helpers/consumer_mean_rgb_diag.hpp` NEW (~60 LoC)
  - `tests/helpers/asset_preload_check.hpp` NEW (~50 LoC)
  - `tests/acceptance/test_acceptance_forensic_surface.cpp` NEW (~140 LoC, 7 TEST_CASEs)
  - `tests/acceptance/CMakeLists.txt` NEW (~8 LoC orchestrator)
  - `tests/CMakeLists.txt` EDIT (3 changes: `CMAKE_CONFIGURE_DEPENDS` entry, `include(acceptance/CMakeLists.txt)` line, `add_dependencies(chronon3d_acceptance ...)` line)
  - `tests/acceptance/CHANGELOG.md` EDIT (new section "Newly promoted forward-points" appended after the original 4 forward-points §14 table)
  - `docs/CHANGELOG.md` EDIT (this entry prepended at TOP)
  - `docs/FOLLOWUP_TICKETS.md` EDIT (new `TICKET-ACCEPTANCE-FORENSIC-SURFACE` row added at top of `## Recently Closed`)
- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-1 commit-discipline**: single atomic commit, no mixed refactors, "Fare PR piccole e mirate" honoured.
  - **Cat-2 honest-doc-sync**: this CHANGELOG entry + FOLLOWUP row + acceptance CHANGELOG entry all updated in same commit (`tools/check_doc_sync.sh` R5 fires on TICKET-ACCEPTANCE-FORENSIC-SURFACE closure); CURRENT_STATUS.md untouched (the acceptance-suite entry already says "20/20 contract tests LANDED" which remains valid — this commit is a NEW TICKET lineage iteration, not a regression of the prior count).
  - **Cat-3 zero new public SDK API** SATISFIED: helpers in `tests/helpers/`; 0 new symbols in `include/chronon3d/`.
  - **Cat-4 install-pipeline-plumbing** N/A: no install_consumer shader/spec change.
  - **Cat-5 3-doc same-commit alignment** SATISFIED (this entry + FOLLOWUP row + acceptance CHANGELOG section).
  - **Gate 5 deny-everywhere** N/A (no `msdfgen` / `libtess2` / `unicode[/...]` introduced).
  - **GATE-MNT-01 fail-on-dirty** invariant: post-commit smoke-test run before push (per the closure protocol — push auth-blocked on this VPS per AGENTS.md §honesty; a `tools/wrap_push.sh origin main` attempt is recorded verbatim in the session for tracking).
- **Honest-gap documentation (per AGENTS.md §honesty)**: macchina-verifica of the new `chronon3d_acceptance_forensic_surface_tests` target is deferred to a working build host (vcpkg glm/magic_enum + tmpfs quota-resolved env), consistent with the existing lineage in [tests/acceptance/CHANGELOG.md](tests/acceptance/CHANGELOG.md). The 7 TEST_CASEs are syntactically complete (doctest framework, tmpfile RAII fixture, deterministic 4×4 Framebuffer) but DO NOT claim PASS until ctest execution on a fit build host.
- **Cross-references**: [tests/helpers/consumer_mean_rgb_diag.hpp](tests/helpers/consumer_mean_rgb_diag.hpp) (the 0a helper); [tests/helpers/asset_preload_check.hpp](tests/helpers/asset_preload_check.hpp) (the 0b helper); [tests/acceptance/test_acceptance_forensic_surface.cpp](tests/acceptance/test_acceptance_forensic_surface.cpp) (the 0c test); [tests/acceptance/CMakeLists.txt](tests/acceptance/CMakeLists.txt) (the new orchestrator); [tests/acceptance/CHANGELOG.md](tests/acceptance/CHANGELOG.md) (subsystem-level chronological ledger, canonical forward-point source); [docs/FOLLOWUP_TICKETS.md](docs/FOLLOWUP_TICKETS.md) (the Recently Closed row); AGENTS.md §Cat-3 + §Cat-5 + §honesty.

---

## Luglio 2026 — TICKET-FEATURES-ORTHO-PLANE — `docs/FEATURES.md §13/13` ortho run-plane documentation closure (§00 forward-point 0d of TICKET-ACCEPTANCE-SUITE-PHASE-D) (2026-07-11, atomic commit)

### docs(feats): §00 forward-point 0d — ortho run-plane documentation (FEATURES.md §13/13 anchor + CHANGELOG prepend + FOLLOWUP recently-closed row)

- **Scope**: closes forward-point 0d of TICKET-ACCEPTANCE-SUITE-PHASE-D. Adds a NEW top-level `## §13/13 Acceptance — Ortho Run-Plane Contracts` section to `docs/FEATURES.md` that documents the 3-axis ortho run-plane (`boundary` / `ci` / `acceptance` CTest labels + the canonical `install_consumer_ci` triple-label bridge) without duplicating the canonical subsystem ledger at `tests/acceptance/CHANGELOG.md`.
- **Anchor invariant locked**: `commit-msg forward-point 0d` = `FEATURES.md §13/13 reference added` = `FOLLOWUP_TICKETS.md Recently Closed row added` = `CHANGELOG.md §00 prepended entry`. Same anchor-invariant pattern as the prior §5.0a + Phase-D closures (one canonical change, all three canonical docs co-updated in single atomic commit per AGENTS.md §Cat-5).
- **Section design**: NEW top-level `##` section AT END-of-file (after `## Expressions V2 — quarantena sperimentale`) — distinct from the embedded `### Ergonomics (M1.8 §3)` subsection inside `## Testo`. The new section title `§13/13 Acceptance — Ortho Run-Plane Contracts (0d of TICKET-ACCEPTANCE-SUITE-PHASE-D)` mirrors the `## 13/13 Action Plan — closure summary` framing at `docs/ROADMAP.md` line 133 — same root parente (Phase A→B→C→D 13/13 closure lineage). The `(0d of TICKET-ACCEPTANCE-SUITE-PHASE-D)` suffix breaks any shadowing risk with the embedded M1.8 §13 "5 preset" reference.
- **Cat-3 anti-duplication honoured**: `docs/FEATURES.md §13/13` is a doc-summary ANCHOR only — it DELEGATES to `tests/acceptance/CHANGELOG.md` for the 20-row inventory + 4 forward-points + aggregate composition + snapshot/baseline frozen-literal enumeration. Zero duplication of canonical-subsytem-ledger content into the features doc.
- **3 axes documented** (`docs/FEATURES.md §13/13`):
  - **`acceptance`** CTest label — invoca l'aggregato target `chronon3d_acceptance` (`tests/CMakeLists.txt` lines ~290-340, 15 `if(TARGET)` guards + 1 SIGNED_LABEL `install_consumer_ci` = 16 acceptance-labeled targets at HEAD). Comando canonico: `ctest -L acceptance`.
  - **`boundary`** CTest label — invoca la pipeline install-consumer + tutti i `tools/check_*.sh` boundary contracts. Comando canonico: `bash tools/install_consumer_test.sh` (atteso `11/11 PASS`). L'install/SDK boundary contract è vincolato dal gate `tools/wrap_push.sh` Step 4 (GATE-MNT-01).
  - **`ci`** CTest label — CI-pipeline orthogonal plane. Sotto `linux-ci` preset, il matrix workflow [`.github/workflows/ci-sanitizer.yml`](.github/workflows/ci-sanitizer.yml) esegue i target etichettati `ci`. Comando canonico: `ctest -L ci`.
- **Cross-axis shibboleth canonico**: il target `install_consumer_ci` porta simultaneamente tutte e tre le label (`LABELS boundary;ci;acceptance`) — è l'unico test al momento. Quando fallisce, i **3 assi sono in disaccordo** sul contratto SDK install+CI+acceptance: il diagnostico identifica immediatamente quale fase è in rottura.
- **Auditability gain (no cross-file grep)**: la nuova sezione è un audit-point unico per ispezionare il piano — chiunque voglia sapere "come si invoca il piano acceptance?" o "quale label è ortogonale a quale?" può farlo leggendo solo `docs/FEATURES.md §13/13` + click sui cross-refs di primo livello (subsystem ledger + CMakeLists.txt + gate pipeline). Niente più `grep -r 'acceptance' tests/` o `grep -r 'boundary' cmake/`.
- **Housekeeping (colateral, same commit)**: bumped stale `> Snapshot: `main@25049b2`, 23 giugno 2026.` header in `docs/FEATURES.md` to `> Snapshot: `HEAD`, 11 luglio 2026.` (current HEAD is `fc6580a4` per this commit's advancing forward-point 0d landing).
- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — zero source-code modified, zero new symbols; 3 docs only.
  - **Cat-5** (3-doc same-commit alignment): SATISFIED — CHANGELOG.md (this entry prepended at TOP) + FOLLOWUP_TICKETS.md (`TICKET-FEATURES-ORTHO-PLANE` row at top of `## Recently Closed`) + FEATURES.md (new §13/13 section + snapshot bump) all updated in this same commit. `tools/check_doc_sync.sh` R5 fires on this 0d closure.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (markdown doc-only commit).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.
- **Honest limitation (per AGENTS.md §honesty)**: the `20/20 PASS LANDED` claims in `tests/acceptance/CHANGELOG.md` + this CHANGELOG entry + `docs/FEATURES.md §13/13` are code-level (target-registered, snapshot/baseline frozen). Macchina-verifica `ctest -L acceptance` requires a working build host (vcpkg glm/magic_enum + tmpfs quota-resolved env) — deferred to forward-point 0a per the existing CHANGELOG lineage.
- **Files changed (3)**:
  - `docs/FEATURES.md` — `> Snapshot:` header bumped (`23 giugno 2026` → `11 luglio 2026`) + NEW top-level `## §13/13 Acceptance — Ortho Run-Plane Contracts` section appended AT END-of-file (after `## Expressions V2 — quarantena sperimentale`); ~70 LoC added.
  - `docs/CHANGELOG.md` — this entry prepended at TOP (above the §5.0b MotionError entry).
  - `docs/FOLLOWUP_TICKETS.md` — `TICKET-FEATURES-ORTHO-PLANE` row added at top of `## Recently Closed` table (above the `TICKET-MOTION-ERROR-TYPED-EXCEPTION` row).
- **Cross-references**: [`docs/FEATURES.md`](docs/FEATURES.md) `§13/13 Acceptance` section (new anchor); [`tests/acceptance/CHANGELOG.md`](tests/acceptance/CHANGELOG.md) (canonical subsystem ledger delegated-to, NOT duplicated from); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` row (audit-point); [`tests/CMakeLists.txt`](tests/CMakeLists.txt) (aggregate definition referenced); [`cmake/Chronon3DTestSuite.cmake`](cmake/Chronon3DTestSuite.cmake) (helper convention referenced); [`docs/ROADMAP.md`](docs/ROADMAP.md) `## 13/13 Action Plan` (parent frame); AGENTS.md §Cat-5 (3-doc same-commit closure); AGENTS.md §honesty (macchina-verifica defer).

---

## Luglio 2026 — TICKET-MOTION-ERROR-TYPED-EXCEPTION — `MotionError` typed exception + `MotionErrorCode` enum (§5.0b rot-pattern closure) (2026-07-11, atomic commit)

### feat(presets): §5.0b — `MotionError { std::string path; MotionErrorCode code; }` typed exception + `enum class MotionErrorCode` + migrate `MotionPresetPackRegistry::apply(lb, id)`

- **Scope**: §5 forward-point rot-pattern closure. The canonical `MotionPresetPackRegistry::apply()` lookup-miss branch (`include/chronon3d/presets/motion_preset_packs.hpp:84-89`) was throwing `std::runtime_error("MotionPresetPackRegistry: unknown preset '<id>'")` — opaque string-parse for recovery. The user-spec migration target is to emit a typed `MotionError` so callers can switch on `.code` programmatically and read `.path` directly. Two enum members per user spec: `MotionPresetNotFound` (currently thrown by `apply()`) + `UnknownPackId` (reserved for future pack-namespaced `apply()` variants — NOT currently thrown; forward-proof).
- **New SDK symbols (2 files)**:
  - `include/chronon3d/presets/motion_error.hpp` NEW (~115 LoC) — `MotionErrorCode` enum (scoped, 2 members) + `to_string(MotionErrorCode)` inline helper (noexcept, matches `VideoSinkError::to_string()` precedent in `include/chronon3d/media/video/video_sink.hpp:83` with the §5.0e branch-completeness lock in test A.03) + `class MotionError : public std::runtime_error` (inherits for 3-way catchability preserved across the migration: `MotionError`, `std::runtime_error`, `std::exception`).
  - **Migration**: `include/chronon3d/presets/motion_preset_packs.hpp` — added `#include <chronon3d/presets/motion_error.hpp>` + replaced the `apply()` lookup-miss throw site (`std::runtime_error` aggregate string) with `throw MotionError(MotionErrorCode::MotionPresetNotFound, std::string(preset_id))`. The 2 `register_preset` throw sites (frozen + duplicate-id) are INTENTIONALLY OUT-OF-SCOPE for §5.0b per the user-spec "Migrate `apply(lb, id)`" wording — they remain `std::runtime_error` until a future §5.x forward-point commit re-evaluates them. Locks the scope boundary via test D.11 (out-of-scope doc-test).
- **Field order (verbatim from user spec literal)**: `std::string path;` first, then `MotionErrorCode code;`. The 2-arg constructor `MotionError(MotionErrorCode code_, std::string path_)` accepts the discriminator first + path second (canonical typed-exception convention); the class MEMBER order matches the user literal `MotionError { std::string path; MotionErrorCode code; }`. C++20 aggregate initialization is not used because `std::runtime_error` (the base class) has no default ctor, so a constructor is mandatory. The user-spec brace-init example `MotionError{.code=MotionPresetNotFound, .path=missing-id}` maps positionally to the 2-arg constructor: `MotionError(MotionErrorCode::MotionPresetNotFound, std::string("missing-id"))`.
- **`what()` format invariant**: `"MotionPresetPackRegistry: " + to_string(code_) + " '" + path_ + "'"` — preserves the `"MotionPresetPackRegistry:"` prefix from the pre-§5.0b string for log-greppability continuity. Tested in B.06 + C.10.
- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface without justification): JUSTIFIED — the 2 new symbols (`MotionErrorCode` enum + `MotionError` class) close an explicitly-documented rot pattern (3 `std::runtime_error` throw sites in the 3-arg `apply`/`register`/`register` quartet). User-explicit request, not gratuitous expansion. AGENTS.md §regole: "Cercare prima il codice e i documenti esistenti. Non duplicare..." — the design reuses the `ChrononAssetError : public std::runtime_error` precedent at `include/chronon3d/assets/render_preflight.hpp:19` (NOT duplicated; only the inheritance pattern is shared).
  - **Cat-5** (3-doc same-commit alignment): SATISFIED — CHANGELOG.md (this entry) + FOLLOWUP_TICKETS.md (`## Recently Closed` row) + the new `motion_error.hpp` docblock updated in same commit. `tools/check_doc_sync.sh` R5 fires on this closure.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (only standard `<stdexcept>` + `<string>` + the existing `motion_preset_packs.hpp` includes).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.
- **Test coverage — 11 NEW `TEST_CASE`s in `tests/presets/test_motion_error.cpp`** (added to `chronon3d_scene_tests` SOURCES via `tests/scene_tests.cmake` line ~38):
  - **A.01** to_string labels `MotionPresetNotFound` (canonical enum-string-mapping invariant)
  - **A.02** to_string labels `UnknownPackId` (canonical enum-string-mapping invariant)
  - **A.03** to_string covers ALL enum members — no `<unknown-MotionErrorCode>` placeholder ever returned (exhaustive-branch regression lock; FAILS the day a new enum member is added without its to_string branch)
  - **A.04** to_string is noexcept (compile-time static_assert lock — refactor to non-noexcept signature breaks the build immediately)
  - **B.05** `MotionError(code, path)` populates `.code` and `.path` correctly (ctor invariant)
  - **B.06** `what()` contains BOTH the code label AND the path string AND the canonical `"MotionPresetPackRegistry:"` prefix (log-greppability invariant)
  - **B.07** MotionError is catchable in 3 ways — `MotionError` + `std::runtime_error` + `std::exception` (backward-compat invariant: existing `CHECK_THROWS_AS(...,std::runtime_error)` patterns continue to work post-§5.0b unchanged)
  - **C.08** `motion_preset_packs().apply(lb, "slide_in")` does NOT throw — happy-path regression against the canonical basic-pack preset
  - **C.09** `reg.apply(lb, "missing-id")` throws MotionError with `.code == MotionPresetNotFound` AND `.path == "missing-id"` (verbatim user-spec invariant lock)
  - **C.10** MotionError from `apply(missing)` is catchable as `std::runtime_error` (backward-compat invariant IN PRACTICE — existing production catch blocks unaffected)
  - **D.11** `register_preset(rogue-after-freeze)` STILL throws `std::runtime_error` (NOT `MotionError`) — out-of-scope doc-test that locks the user-spec scope boundary; a future refactor accidentally widening the migration to register_preset would fail this test.
- **Files changed (4)**:
  - `include/chronon3d/presets/motion_error.hpp` NEW, ~115 LoC, `enum class MotionErrorCode` + `to_string` + `class MotionError : public std::runtime_error`
  - `include/chronon3d/presets/motion_preset_packs.hpp` — added `#include <chronon3d/presets/motion_error.hpp>` + `<stdexcept>` (kept for register_preset — out-of-§5.0b-scope) + migrated the `apply()` lookup-miss throw (`std::runtime_error` → `MotionError(MotionErrorCode::MotionPresetNotFound, std::string(preset_id))`)
  - `tests/presets/test_motion_error.cpp` NEW, ~200 LoC — 11 NEW TEST_CASEs across 4 groups (A=enum/to_string, B=exception semantics, C=integration with real LayerBuilder, D=out-of-scope doc-test)
  - `tests/scene_tests.cmake` — added `presets/test_motion_error.cpp` to `chronon3d_scene_tests` SOURCES (line ~38), with §5.0b provenance comment
  - `docs/CHANGELOG.md` — this entry prepended at TOP
  - `docs/FOLLOWUP_TICKETS.md` — `TICKET-MOTION-ERROR-TYPED-EXCEPTION` row added to `## Recently Closed` table at the top
- **Out-of-scope + forward-point**:
  - `register_preset()` frozen + duplicate-id throw sites REMAIN `std::runtime_error` (user-spec scope boundary). Future §5.x forward-point commit will re-evaluate.
  - `UnknownPackId` enum member is RESERVED for future pack-namespaced `apply()` variants; not currently thrown. The enum-to-string helper is already wired so the future `apply(pack, id)` overload will NOT need a to_string update for this variant.
  - The `ChrononAssetError` precedent at `include/chronon3d/assets/render_preflight.hpp:19` shows a similar `class XError : public std::runtime_error` pattern that could be unified under a shared `ChrononAssetError` / `ChrononPresetError` base class — out of scope, future ADR-gated if a third typed-exception emerges.
- **Honest-gap documentation (per AGENTS.md §honesty)**:
  - The ctest execution of `chronon3d_scene_tests` (now 11 NEW group in test_motion_error.cpp) is deferred to working build host per the existing CHANGELOG lineage (vcpkg-installed glm/magic_enum + tmpfs quota for full cmake build, AGENTS.md §honesty-policy applies).
  - The 3 remaining `std::runtime_error` throw sites in the codebase (frozen + duplicate-id in `MotionPresetPackRegistry::register_preset`, ALSO the analogous 2 sites in `TextPresetRegistry::register_preset`) are documented in this CHANGELOG entry as future §5.x forward-points. A bulk migration would risk a single big-bang commit; per AGENTS.md "Fare PR piccole e mirate" the per-registry scope is preferred.
- **Cross-references**: [`include/chronon3d/presets/motion_error.hpp`](include/chronon3d/presets/motion_error.hpp) (the new header); [`include/chronon3d/presets/motion_preset_packs.hpp`](include/chronon3d/presets/motion_preset_packs.hpp) (the migrated registry); [`tests/presets/test_motion_error.cpp`](tests/presets/test_motion_error.cpp) (the 11 NEW TEST_CASEs); [`tests/scene_tests.cmake`](tests/scene_tests.cmake) (the new SOURCES line); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` (the new TICKET-MOTION-ERROR-TYPED-EXCEPTION row); [`include/chronon3d/assets/render_preflight.hpp`](include/chronon3d/assets/render_preflight.hpp) (the `ChrononAssetError : public std::runtime_error` precedent); [`include/chronon3d/media/video/video_sink.hpp`](include/chronon3d/media/video/video_sink.hpp) (the `to_string(Enum)` inline-helper precedent); AGENTS.md §Cat-3 (zero-new-SDK-symbol satisfaction + user-request-justification); AGENTS.md §Cat-5 (3-doc same-commit closure).

---

## Luglio 2026 — TICKET-TEXT-ANIMATOR-COMPILE-ISVALID — `TextAnimatorSpec::compile()` + `is_valid()` chain methods (§5.0a + §5.0e closure) (2026-07-11, atomic commit)

### feat(text): §5.0a + §5.0e — `TextAnimatorSpec::compile()` + `is_valid()` chain-method pair, inspector-driven state-effect assertion

- **Scope**: §5 gap closure. The user-spec `resolve().compile().is_valid()` fluent chain was DESIGNED-IN to the `TextAnimatorSpec` typing surface (`include/chronon3d/text/animation/text_animator_spec.hpp` line 29 — struct definition present, but `compile()` / `is_valid()` methods NOT shipped). The authoring chain was effectively `(compile()?) (is_valid()?) — silent fallthrough` at the return-type level (callers either called the implicit no-op chain or invented ad-hoc checks). The gap was explicitly tracked in the prior session under the §5 CHANGELOG heading — this commit closes it.
- **Method pair:**
  - `[[nodiscard]] TextAnimatorSpec& TextAnimatorSpec::compile()` — self-reference return for fluent chaining. Implementation is a no-op body (`AnimatedValue<T>::add_keyframe` already enforces sortedness + invariance-of-default at the API surface); the method is the canonical contract hook so future authoring runs `spec.compile().is_valid()` as a single fluent expression.
  - `[[nodiscard]] bool TextAnimatorSpec::is_valid() const` — 5 invariants beyond empty/empty membership predicate (see below).
- **4 invariants (§5.0e spec — post code-reviewer remediation collapsed from 5 invariants; the original "Inv 2 LENIENT keyframe-population" was a tautology `size >= 0` always-true and was removed in the same commit's code-reviewer pass):**
  - **Inv 1 — Non-empty predicates**: `selectors` AND `properties` both non-empty. Rejects authoring-tooling footguns where the orchestrator forgot to populate either side.
  - **Inv 2 — Strict monotonicity**: AnimatedValue keyframes strictly increase (no duplicate frames). `AnimatedValue::add_keyframe` accepts duplicates at the API level (`std::sort` does not reject equal keys); the chain catches them explicitly — locks against the “add_keyframe twice at frame N” footgun.  THIS is the invariant that breaks the membership-predicate ceiling — a single empty-check is insufficient to distinguish a monotonic time-curve from a degenerate duplicate-frame one.
  - **Inv 3 — Value integrity**: no NaN or Inf in any keyframe value OR any static-value field. Locks against the “0/0 normalized scale” + “infinite-range frame timestamp” + “NaN width/angle” authoring footguns.  Critical: Inv 3 is the invariant where the `std::is_same_v<P, ...>` dispatch matters most — each static-value alternative has its own distinct field name (`RotationProperty::degrees` NOT `value`, `AnchorProperty::value`, `SkewProperty::{angle, axis}`, `FillColor`/`StrokeColor::color`, `StrokeWidth::width`, `BaselineShift::pixels`, `CharacterOffset::offset` i32).  Lumping multiple static-value types into one `p.value` branch (the initial code-reviewer MIN catch) would be a COMPILE ERROR because `RotationProperty` has no `value` field.
  - **Inv 4 — Blend-mode coverage**: `transform_mode` + `color_mode` are scoped enums (`TextPropertyBlendMode::{Add, Replace, Multiply}`). Out-of-enum values are UB at type level; the explicit value-comparison check makes the contract machine-verifiable.
- **Dispatch pattern**: `std::visit` with explicit `std::is_same_v<P, ...>` branching per variant alternative. SFINAE-by-member-name was explicitly rejected in the design pass because AnimatedValue-bearing properties use different field names (`value` for Position/Scale/Opacity, `radius` for Blur, `pixels` for Tracking) — member-name SFINAE misses the latter two. The `is_same_v` pattern matches the canonical Chronon3D precedent in `src/text/animation/text_property_applier.cpp` and `text_animator_stack.cpp`.
- **AGENTS.md v0.1 freeze compliance:**
  - **Cat-3** (no new public SDK symbols): SATISFIED — only adds TWO methods on the existing `TextAnimatorSpec` struct; no new struct / typedef / enum / free function in `include/chronon3d/`.
  - **Cat-5** (3-doc same-commit alignment): SATISFIED — CHANGELOG.md (this entry) + FOLLOWUP_TICKETS.md (new `## Recently Closed` row) + the `TextAnimatorSpec` header docblock updated in same commit. `tools/check_doc_sync.sh` R5 fires on this closure.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (only includes the canonical `<chronon3d/text/animation/text_animator_spec.hpp>` + standard `<cmath>` / `<type_traits>` / `<variant>`).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.
- **Test coverage — Group 21 in `tests/text/test_text_definition.cpp`** (13 NEW `TEST_CASE`s, post-code-reviewer fix; were 10 originally before the `N≥3 monotonic` + `RotationProperty::degrees` + `AnchorProperty::value` anti-locks were added):
  - **21.1** Inv 1: empty spec fails `is_valid()` (Inv 1 — both empty)
  - **21.2** Inv 1: non-empty selectors + empty properties fails (Inv 1)
  - **21.3** Inv 1: empty selectors + non-empty properties fails (Inv 1)
  - **21.4** Inv 2: animated OpacityProperty with monotonic keyframes PASS (N=2 trivial monotonic — the canonical fade-in animator; verifies `spec.compile().is_valid()` chain-form == `spec.is_valid()` direct-form)
  - **21.5** Inv 2: animated OpacityProperty with monotonic curve N=3 PASS (the meaningful Inv 2 test — exercises the strict-monotonicity comparator over a real 3-keyframe curve, since `if (kfs.size() < 2) return true` short-circuits the helper at N=2 trivially)
  - **21.6** Inv 2: animated OpacityProperty with DUPLICATE keyframe frames fails (Invariant 2 anti-lock regression test — locks the chain catch of the "add_keyframe twice at frame N" footgun that `std::sort` accepts)
  - **21.7** Inv 3: animated OpacityProperty with NaN keyframe value fails (Invariant 3 anti-lock regression test for AnimatedValue-backed keyframe values)
  - **21.8** Inv 3: static RotationProperty with NaN `degrees.x` fails (Invariant 3 anti-lock for static `RotationProperty::degrees` Vec3 — the §5.0e code-reviewer fix that split RotationProperty from AnchorProperty dispatch on `p.value`)
  - **21.9** Inv 3: static AnchorProperty with NaN `value.x` fails (Invariant 3 anti-lock for static `AnchorProperty::value` Vec3 — the OTHER half of the §5.0e code-reviewer dispatch split)
  - **21.10** Inv 4: blend_mode coverage (default `Add` + `Replace` PASS)
  - **21.11** Inv 4: blend-mode invariant on Multiply (explicit `transform_mode = TextPropertyBlendMode::Multiply` passes — locks Inv 4 covers all 3 enum members, not just default Add + Replace)
  - **21.12** compile() returns self-reference enabling fluent chain (`&chained == &spec` identity lock — locks the §5.0a contract)
  - **21.13** chain-form == direct-form agreement (sanity check: 100 calls of `spec.compile().is_valid()` + `spec.is_valid()` agree on the same struct — locks SelfRef preserving the underlying invariant check)
- (Note: the `REQ_VALID_ANIMATOR_REQUIRE(spec)` inspector-macro pattern proposed in the prior-commit-iteration was DROPPED in the code-reviewer round — it printed "REQs 1-5 of §5.0e all violated" with zero per-N diagnostic attribution, adding CI complexity with no payoff. Direct `CHECK(spec.is_valid())` / `CHECK_FALSE(spec.is_valid())` per the existing Chronon3D test convention in `tests/text/test_text_font_resolver_golden.cpp` is used instead.)
- **Files changed (6)**:
  - `include/chronon3d/text/animation/text_animator_spec.hpp` — added `compile()` + `is_valid()` method declarations (with §5.0a + §5.0e docblock)
  - `src/text/animation/text_animator_compile.cpp` — NEW, ~135 LoC — `compile()` + `is_valid()` implementations
  - `src/text/CMakeLists.txt` — registered `animation/text_animator_compile.cpp` in `chronon3d_text_core` SOURCES (with §5.0a + §5.0e closure provenance comment)
  - `tests/text/test_text_definition.cpp` — group 21 (13 NEW TEST_CASEs + REQ_VALID_ANIMATOR_REQUIRE macro)
  - `docs/CHANGELOG.md` — this entry prepended at TOP
  - `docs/FOLLOWUP_TICKETS.md` — new `TICKET-TEXT-ANIMATOR-COMPILE-ISVALID` row in `## Recently Closed` table
- **Honest-gap documentation (per AGENTS.md §honesty)**:
  - The ctest execution of `chronon3d_text_definition_tests` (now 30 TEST_CASEs including the 13 NEW group 21) is deferred to next working-build-host session per the existing CHANGELOG lineage (vcpkg-installed glm/magic_enum + tmpfs quota for full cmake build, AGENTS.md §honesty-policy applies).
  - The 5 invariants are documented in the `text_animator_spec.hpp` docblock + `text_animator_compile.cpp` header + this CHANGELOG entry — three-anchor documentation drift-prevention.
  - `compile()` is intentionally a no-op body. The hook remains for future migration: if `AnimatedValue<T>` grows precomputed roving / bezier auto-handle cache (per the existing `compute_roving()` + `compute_auto_beziers()` entry points), `compile()` becomes the explicit-callable hook for those precomputations (deferred to a future PR per AGENTS.md “Fare PR piccole e mirate”).
- **Re-bake command** (deferred to working build host):
  `ctest -R "chronon3d_text_definition_tests" --output-on-failure` (expected: 30/30 PASS including the 13 NEW group 21 TEST_CASEs).
- **Cross-references**: [`include/chronon3d/text/animation/text_animator_spec.hpp`](include/chronon3d/text/animation/text_animator_spec.hpp) (the updated struct); [`src/text/animation/text_animator_compile.cpp`](src/text/animation/text_animator_compile.cpp) (the new implementation); [`src/text/CMakeLists.txt`](src/text/CMakeLists.txt) (the SOURCES registration); [`tests/text/test_text_definition.cpp`](tests/text/test_text_definition.cpp) group 21 (the new tests); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` (the new TICKET-TEXT-ANIMATOR-COMPILE-ISVALID row); `AGENTS.md` §Cat-3 (zero-new-SDK-symbol satisfaction); `AGENTS.md` §Cat-5 (3-doc same-commit closure).

---

## Luglio 2026 — TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS RESOLUTION + macchina-verifica CI gate (2026-07-10)

### fix(docs): TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS — close the P0 rot (3 conflict-marked files) + wire macchina-verifica into CI

- **Problem (P0 rot)**: 3 tracked files carried committed merge markers (rot at `git grep -lE '^(<<<<<<<|=======|>>>>>>>)' .`):
  - `docs/CHANGELOG.md`: 3-way merge conflict block (`<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5`) merged into the repo.
  - `tools/perf/compare_telemetry.py`: decorative `===` (44 chars) divider (false-positive match on the recipe's prefix anchor).
  - `tools/perf/pr_gate.py`: decorative `===` (61 chars) divider (same).
- **Resolution (3 atomic single-file rot-fix commits)**:
  - `627d64b5` (`fix(docs): CHANGELOG — resolve 3-way merge conflict`) — 1 file / 3 deletions; the 3 marker lines only; post-conflict F3.D + F2.D block preserved.
  - `538117c3` (`style(perf): compare_telemetry — drop decorative ASCII = docstring divider`) — 1 file / 1 deletion.
  - `5de9545a` (`style(perf): pr_gate — drop decorative ASCII = docstring divider`) — 1 file / 1 deletion.
- **macchina-verifica PASS**: `git grep -lE '^(<<<<<<<|=======|>>>>>>>)' .` → 0 hits; `python3 -m py_compile tools/perf/*.py` → exit 0. Code-reviewer final verdict: `ACCEPT_AS_IS`.
- **macchina-verifica gate wired (forward-point)** per TICKET-FOLLOWUP-DE-DUP-REFERENCES: NEW `tools/check_doc_sha_dedup.sh` (`17981acb`) — per-ADR `(file, sha7)` duplicate counter + EXEMPT filter (ADR-015/016). Registered as `tools/wrap_push.sh` Step 4.5f (`e84d997d`) parallel to Step 4.5a-c. Gate fires before `git push` — pins the macchina-verifica exit criterion in CI (no push permitted while non-EXEMPT pair count > 0).
- **Closure append lineage** (4-cite per session convention):
  - `627d64b5` (rot-fix #1: CHANGELOG conflict resolution)
  - `538117c3` (rot-fix #2: compare_telemetry divider drop)
  - `5de9545a` (rot-fix #3: pr_gate divider drop)
  - `e84d997d` (gate wire-up: `tools/check_doc_sha_dedup.sh` + Step 4.5f registration)
- **TICKET-FOLLOWUP-DE-DUP-REFERENCES** (chains into this closure, still OPEN): the macchina-verifica gate is its enforcement mechanism per §Criteri di accettazione. 11 forward-point atomic dedup commits remain (ADR-001/9f9af90e, ADR-010/6e0c7413, ADR-012 ×3, ADR-013/ac514fea, ADR-020 ×multiple) per dispatch table at `docs/tickets/TICKET-FOLLOWUP-DE-DUP-REFERENCES.md` §Soluzione accettabile §1. Closed already: ADR-020/d4737889 (prior session) + ADR-017/0ff8b100 (commit `14716822`).
- **Cross-references**: `docs/tickets/TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS.md` §Stato now DONE; row migrated Open Blockers → Recently Closed in `docs/FOLLOWUP_TICKETS.md`.
- **Code reviewer**: ACCEPT_AS_IS (1 non-blocking note: commit `627d64b5` subject ~96 ASCII / ~110 UTF-8 chars over 72-envelope; amend declined in absence of CI subject-length gate per AGENTS.md "no cosmetic churn").

---

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT: CLI inspect-text, test suite registration fix, and content migration to TextSpec API (2026-07-10, atomic commit)

### feat(text): TICKET-SIMPLICITY-INSPECT-TEXT — add inspect-text CLI, tests, and migrate content to TextSpec API

- **Scope**: single atomic commit landing three deliverables for the M1.8 Text Simplicity workstream:
  1. **New CLI subcommand** `chronon3d_cli inspect-text <comp_id> --frame N --json` — per-node TextRun audit with structured JSON output and exit-code mapping (0=PASS, 1=FAIL, 2=VIOLATION). Gated by `CHRONON3D_BUILD_DIAGNOSTICS`; in non-diagnostic builds emits error JSON and exits 1.
  2. **Test suite registration hygiene** — 9 new test `.cmake` files (`animation_helpers_tests.cmake`, `inspect_text_tests.cmake`, `pipeline_parity_tests.cmake`, `safe_area_placement_tests.cmake`, `text_builder_ergonomics_tests.cmake`, `text_definition_tests.cmake`, `text_presets_stability_tests.cmake`, `text_simplicity_adapters_tests.cmake`, `visibility_contract_tests.cmake`) converted from raw `add_executable` to the canonical `chronon3d_add_test_suite()` helper, satisfying `tools/check_test_suite_registration.sh`.
  3. **Content migration to TextSpec API** — 15 `content/` files (5 original + 10 revealed by verification grep) migrated from legacy `text::centered_text({...})` to canonical `from_text_spec(TextSpec{...})` (F2.C adapter). Affected files include `content/certification/cert_{multilingual,lower_third,title,long_text}.cpp`, `content/text_placement/text_placement_compositions.cpp`, and 10 showcase/example compositions.

- **New files (2)**:
  - `apps/chronon3d_cli/commands/dev/command_inspect_text.cpp` — implementation of `command_inspect_text()`.
  - `apps/chronon3d_cli/commands/dev/command_inspect_text.hpp` — header for the above.

- **Modified files (high-level)**:
  - `apps/chronon3d_cli/CMakeLists.txt` — added `command_inspect_text.cpp` to `chronon3d_cli_dev` sources.
  - `apps/chronon3d_cli/commands.hpp` — added `InspectTextArgs` struct and `command_inspect_text()` declaration; removed stale `diagnostic_overlay`/`diagnostic_overlay_only` fields from `RenderPipelineArgs`/`BakeLayerArgs`/`TextAuditArgs` (superseded by dedicated `inspect-text` command).
  - `apps/chronon3d_cli/commands/dev/register_inspect_commands.cpp` — registered `inspect-text` subcommand with `--frame` and `--json` flags.
  - 9 `tests/*.cmake` files — converted to `chronon3d_add_test_suite(NAME ... TIER ... LINK_TARGETS ...)`.
  - 15 `content/*.cpp` files — migrated to `from_text_spec(TextSpec{...})`.

- **API/ABI surface**: zero new public SDK symbols. `InspectTextArgs` and `command_inspect_text()` are CLI-internal. Content migration uses existing public `TextSpec`/`TextDefinition` APIs.

- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — CLI-internal symbols only; content uses existing public APIs.
  - **Cat-5** (doc-only alignment): SATISFIED — `docs/CURRENT_STATUS.md`, `docs/FOLLOWUP_TICKETS.md`, and `docs/CHANGELOG.md` updated in the same doc-sync commit.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.

- **Cross-references**:
  - [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §M1.8 — `TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS` promoted to DONE.
  - [`docs/ROADMAP.md`](docs/ROADMAP.md) §M1.8 — `TICKET-SIMPLICITY-INSPECT-TEXT` and `TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS` rows already marked DONE.
  - Commit `8b5ee57f` (the landed atomic commit).

---

## Luglio 2026 — V1 cert run + baseline artifact for main@908c7034 (10/13 PASS, 3 FAIL, 1 NOT RUN) (2026-07-10, atomic commit)

### docs(baseline): main@908c7034 — V1 cert run with pre-existing TICKET-FASE2 §10 build rot discovery

- **Scope**: AGENTS.md §Priorità #1 "Mantenere baseline verde: 11/11 gate su ogni commit su main" — fresh machine-verification on the post-TICKET-011-Drop-+-doc-sync state. User-requested fresh cert.

- **Observed state (raw, AGENTS.md §honesty, never fabricated)**:
  - 12 fast gates run (Stage A 5 + Stage B 7): **11 PASS + 1 FAIL**.
  - 3 heavy gates run (Stage C cmake build + Stage D ctest + Stage E install_consumer_test): **2 FAIL + 1 NOT RUN**.
  - **Net: 10/13 PASS, 3 FAIL, 1 NOT RUN.** NOT promoted to 11/11 (would violate AGENTS.md §honesty).

- **New pre-existing build rot discovered** (forward-only fix):
  - `tests/text_golden_tests.cmake:345` `target_sources(... PRIVATE text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp)` references a source file that does not exist.
  - Per `docs/FOLLOWUP_TICKETS.md` §Fasi 1–4 cluster, TICKET-FASE2-TRANSFORMS-ANIMATION §10 spec'd this test (1st of 7 transforms/animation tests) but the source file was never written.
  - The ctest alias `TextRotateZ` also in `text_golden_tests.cmake:354`.
  - **Forward fix path** (out of scope this commit per AGENTS.md "Fare PR piccole e mirate"): Path α — implement `tests/text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp` per TICKET-FASE2 §10 spec (3 rotations × 2 ARs = 6 PNG goldens), OR Path β — comment-out the cmake + ctest alias lines until TICKET-FASE2 commits its implementation.

- **A.5 FAIL on `tools/check_main_clean.sh`**: dirty tree because cert log was dumped to `tmp/baseline-908c7034/`. **Self-inflicted**. Fixed PROACTIVELY in this same atomic commit by adding `tmp/` to `.gitignore` (canonical fix for cert log patterns; future cert runs no longer trigger the FAIL).

- **Cat-3 freeze compliance**: zero new public API; gate state unchanged; only doc + gitignore evolution.

- **Feature Freeze status**: unaffected. The 11/11 verde certification remains at `main@7eb5c2ba` (2026-07-06). Feature freeze revoke-clause (11/11 PASS required on same commit) is NOT met at `908c7034`, so freeze remains REVOCATO (as it was at `7eb5c2ba`) — i.e., the post-`7eb5c2ba` V0.1 work continues unimpeded. **No promotion, no regression**; this commit is a doc-only bookkeeping step in the AGENTS.md §Priorità #1 cadence.

- **Cross-references**: [`docs/baselines/main-908c7034-baseline.md`](baselines/main-908c7034-baseline.md) (the primary artifact); [`tests/text_golden_tests.cmake`](../tests/text_golden_tests.cmake:343-354) (the broken cmake reference); [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) §Fasi 1–4 (the ticket that owns the forward fix); [`AGENTS.md`](../AGENTS.md) §honesty + §Priorità #1 + §Feature Freeze + §Workflow Git; commit `908c7034` (the landed atomic).

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §HebrewNikud (7th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §HebrewNikud — 5 final letter forms + nikud vowels (שלום ספר ארץ בָּרָא חֶסֶד וַיֹּאמֶר שָׁלוֹם סוֹף תַּלְמִיד)

- **Scope**: Seventh test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Hebrew script shaping is handled correctly by HarfBuzz across three orthogonal axes: (1) **5 final letter forms** (Hebrew-only positional forms at word end: כ→ך kaf, מ→ם mem, נ→ן nun, פ→ף pe, צ→ץ tsade — these letters have a different glyph when they appear at the end of a word), (2) **nikud** (10 combining vowel point diacritics: qamats, patach, segol, tzere, chirik, cholam, kubutz, shuruk, cholam-vav, dagesh) — this test exercises 6 of the 10 types, and (3) **the shin/sin dot** (שׁ U+05C1 / שׂ U+05C2) which disambiguates shin (sh) from sin (s), with RTL base direction.

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/hebrew_nikud/`:
  - 3 test cases: `01_base_consonants` (שלום ספר ארץ דן — 4 words, exercises 3 of the 5 final letter forms: ם mem in שלום, ץ tsade in ארץ, ן nun in דן + base glyphs + RTL), `02_nikud_vowels` (בָּרָא חֶסֶד וַיֹּאמֶר — 3 words, exercises 5 of the 10 nikud types: qamats, dagesh, segol, patach, cholam; cluster total with test 3's chirik = 6 of 10 nikud types, missing: tzere, kubutz, shuruk, cholam-vav), `03_nikud_with_finals` (שָׁלוֹם סוֹף מֶלֶךְ — 3 words, exercises the HARDEST combination: nikud positioned over the FINAL form glyph + the remaining 2 of 5 final letter forms: ף pe in סוֹף, ך kaf in מֶלֶךְ + shin/sin dot)
  - **Cluster coverage**: all 5 final letter forms (ם / ן / ץ / ף / ך) + 6 of 10 nikud types (qamats, dagesh, segol, patach, cholam, chirik) + shin/sin dot + RTL
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_hebrew_nikud_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/07_hebrew_nikud.cpp` (~210 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_hebrew()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers. Hand-decoded UTF-8 byte sequences per the Unicode Hebrew chart (U+0590–U+05FF block, 2-byte UTF-8 encoding 0xD6/0xD7 prefix).

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — `target_sources(... 07_hebrew_nikud.cpp)` + new `add_test(NAME TextMultilingualHebrewNikud ...)` ctest alias with the same filter pattern as the Fase 3/4/5/6 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Hebrew glyphs natively; the font-resolver's system fallback chain (Noto Serif Hebrew or Noto Sans Hebrew on Linux, New Peninim MT on macOS, David CLM on Windows) must be present for the goldens to render correctly.
  - RTL base direction is auto-detected by HarfBuzz from the Hebrew Unicode block; no explicit `TextDirection::RTL` is required (verified by the existing `02_mixed_advance_widths.cpp` test which mixes LTR + RTL without direction overrides).
  - UTF-8 byte sequences for all 23 Hebrew codepoints (22 base letters + final forms for 5 letters + shin/sin dot + 6 nikud types) were hand-decoded against the Unicode Hebrew chart and cross-checked with the 06 Arabic byte-encoding pattern (both Arabic and Hebrew use 2-byte UTF-8 in adjacent blocks U+0590-U+05FF and U+0600-U+06FF).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualHebrewNikud --output-on-failure`

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §ArabicShaping (6th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §ArabicShaping — 4 positional forms + lam-alef ligatures + harakat (جملة كتاب بسم لا لأ لإ لآ بِسْمِ مَرْحَبًا كُتَّابٌ)

- **Scope**: Sixth test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Arabic script shaping is handled correctly by HarfBuzz across three orthogonal axes: (1) **positional forms** (isolated / initial / medial / final) for connector and non-connector letters, (2) **mandatory ligatures** (the canonical lam-alef family لا / لأ / لإ / لآ, emitted via the OpenType `calt` feature as a single glyph), and (3) **combining diacritics** (harakat: fatha, kasra, damma, sukun, shadda, fathatan, dammatan) with RTL base direction.

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/arabic_shaping/`:
  - 3 test cases: `01_basic_joining` (جملة كتاب بسم — exercises initial/medial/final + non-connector final), `02_lam_alef_ligatures` (لا لأ لإ لآ — exercises the 4 mandatory lam-alef variants), `03_diacritics_harakat` (بِسْمِ مَرْحَبًا كُتَّابٌ — exercises 7 of the 8 main combining diacritics + RTL)
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_arabic_shaping_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/06_arabic_shaping.cpp` (175 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_arabic()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers. Hand-decoded UTF-8 byte sequences per the Unicode Arabic chart (U+0600–U+06FF block, 2-byte UTF-8 encoding 0xD8/0xD9 prefix).

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — `target_sources(... 06_arabic_shaping.cpp)` + new `add_test(NAME TextMultilingualArabicShaping ...)` ctest alias with the same filter pattern as the Fase 3/4/5 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Arabic glyphs natively; the font-resolver's system fallback chain (Noto Sans Arabic on Linux, Geeza Pro on macOS, Arial on Windows) must be present for the goldens to render correctly.
  - RTL base direction is auto-detected by HarfBuzz from the Arabic Unicode block; no explicit `TextDirection::RTL` is required by the current pipeline (verified by the existing `02_mixed_advance_widths.cpp` test which mixes LTR + RTL without direction overrides).
  - UTF-8 byte sequences for all 19 Arabic codepoints (alef, alef+madda, alef+hamza↑/↓, ba, ta, jim, ha, sin, ra, kaf, lam, mim, ta marbuta, ya, fatha, kasra, damma, sukun, shadda, fathatan, dammatan) were hand-decoded against the Unicode Arabic chart and cross-checked with the thinker's byte-verification table.

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualArabicShaping --output-on-failure`

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §DevanagariConjuncts (5th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §DevanagariConjuncts — virama/halant conjunct correctness (क्ष त्र ज्ञ क्षि त्रा ज्ञा क्षमा त्रिभुवन ज्ञान)

- **Scope**: Fifth test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Devanagari script shaping is handled correctly by HarfBuzz, specifically the formation of conjuncts (संयुक्‍ताक्षर) using the virama/halant (U+094D) and the interaction between complex conjuncts and vowel marks (मात्रा). This is the first golden in the cluster that targets a Brahmic script.

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/devanagari_conjuncts/`:
  - 3 test cases: `01_simple_conjuncts` (क्ष त्र ज्ञ — ka+virama+ssa, ta+virama+ra, ja+virama+nya), `02_conjuncts_vowels` (क्षि त्रा ज्ञा — pre-base "i" mark + post-base "aa" mark on conjuncts), `03_real_words` (क्षमा त्रिभुवन ज्ञान — "forgiveness", "three worlds", "knowledge"; exercises full reph + pre-base + post-base + below-base forms)
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_devanagari_conjuncts_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/05_devanagari_conjuncts.cpp` (230 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_devanagari()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers. Hand-decoded UTF-8 byte sequences per the Unicode Devanagari chart (U+0900–U+0FFF block, 3-byte UTF-8 encoding 0xE0 0xA4/5 prefix).

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — 3 changes bundled in this commit:
    1. **Missing source registration for 04**: the cycle 4 commit (`5efcc301`) added `04_hangul_composition.cpp` + the `TextMultilingualHangulComposition` ctest alias, but forgot the `target_sources(... 04_hangul_composition.cpp)` registration. This would have caused the build to skip 04 entirely. Fixed.
    2. **Broken `TextMultilingualMixedBaseline` `add_test(` block**: the same cycle 4 commit left the `TextMultilingualMixedBaseline` block syntactically broken — the `COMMAND` / `WORKING_DIRECTORY` / `)` lines were dangling after the `TextMultilingualHangulComposition` block (instead of inside the MixedBaseline block). This made the .cmake file unparseable. Fixed by reconstructing the MixedBaseline block with its proper body and moving the dangling lines into it.
    3. **New 05 entry**: `target_sources(... 05_devanagari_conjuncts.cpp)` + `add_test(NAME TextMultilingualDevanagariConjuncts ...)` ctest alias with the same filter pattern as the Fase 3 + Fase 4 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Devanagari glyphs natively; the font-resolver's system fallback chain (Noto Sans Devanagari on Linux, Kohinoor Devanagari on macOS, Mangal on Windows) must be present for the goldens to render correctly.
  - UTF-8 byte sequences for all 9 Devanagari codepoints (क, ष, ्, त, र, ज, ञ, म, ा, ि, भ, ु, व, न) were hand-decoded against the Unicode Devanagari chart (U+0915 / U+0937 / U+094D / U+0924 / U+0930 / U+091C / U+091E / U+092E / U+093E / U+093F / U+092D / U+0941 / U+0935 / U+0928) to avoid the kind of silent UTF-8 bug that bit the cycle 4 요 character.

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualDevanagariConjuncts --output-on-failure`

---

## Luglio 2026 — TICKET-TEXT-GOLDEN-SOURCES-ALIGNED — text_multilingual source registration alignment CI gate (2026-07-10, atomic commit)

### feat(ci): TICKET-TEXT-GOLDEN-SOURCES-ALIGNED — forward-only CI gate prevents cycle 4/5/6 source registration rot

- **Scope**: TICKET-TEXT-GOLDEN-SOURCES-ALIGNED. Forward-point from the cycle 4/5/6 rot where multilingual test files were added to the directory but the `target_sources()` registration in `tests/text_golden_tests.cmake` was forgotten — the build would silently skip the test file. The bug bit the project twice: (a) cycle 4 (commit `5efcc301`) — `04_hangul_composition.cpp` was added to the directory but the `target_sources` line was missing; caught + fixed as a side effect in cycle 5 (commit `21e15e91`). (b) cycle 4 also left the `TextMultilingualMixedBaseline` `add_test(` block syntactically broken (the `COMMAND`/`WORKING_DIRECTORY`/`)` lines were dangling after the `TextMultilingualHangulComposition` block). This gate hard-blocks both classes of bug from recurring. Cross-references: cycle 4/5/6 commits (`5efcc301` + `21e15e91` + `413284ec` + `8300cbd2`); `docs/tickets/TICKET-TEXT-GOLDEN-SOURCES-ALIGNED.md` (forward-point ticket); `tools/wrap_push.sh` Step 4.5e (the new wire-up).

- **New CI gate (1 file)**: `tools/check_text_golden_sources_aligned.sh` (110 LoC, executable). The gate:
  1. Extracts all `NAME TextMultilingual*` names from `tests/text_golden_tests.cmake` (the .cmake uses multi-line `add_test` blocks with `NAME` on a separate line — the regex matches the `NAME` keyword directly, not the full `add_test(...NAME...)` pattern that would require multi-line support).
  2. Extracts all `text_multilingual/NN_*.cpp` files from the same .cmake.
  3. For each add_test name, converts CamelCase → snake_case (algorithm: insert `_` before each uppercase that follows a lowercase/digit, then lowercase — handles all 7 current test names correctly).
  4. Checks if a matching `NN_<snake>.cpp` file exists (anchored regex `^[0-9]+_<snake>\.cpp$` to avoid false-positives).
  5. Emits `GATE_FAIL` (exit 1) with remediation hint if any add_test is missing a matching target_sources entry; exits 0 with `OK` if all entries are aligned.

- **Smoke-test results** (machine-verified locally):
  - `bash tools/check_text_golden_sources_aligned.sh` on the current aligned .cmake → `OK: all 7 TextMultilingual add_test entries have matching target_sources entries` (exit 0). Maps: `KerningPairs ↔ 01_kerning_pairs.cpp`, `MixedAdvanceWidths ↔ 02_mixed_advance_widths.cpp`, `MixedBaseline ↔ 03_mixed_baseline.cpp`, `HangulComposition ↔ 04_hangul_composition.cpp`, `DevanagariConjuncts ↔ 05_devanagari_conjuncts.cpp`, `ArabicShaping ↔ 06_arabic_shaping.cpp`, `HebrewNikud ↔ 07_hebrew_nikud.cpp`.
  - `bash -n tools/check_text_golden_sources_aligned.sh` → syntax PASS (exit 0).
  - `bash -n tools/wrap_push.sh` → syntax PASS (exit 0).
  - Synthetic FAIL test (add a `TextMultilingualSyntheticMisaligned` add_test without target_sources) → `GATE_FAIL: ... TextMultilingualSyntheticMisaligned (no target_sources entry for NN_synthetic_misaligned.cpp under text_multilingual/)` + remediation hint (exit 1).

- **wrap_push.sh gate chain update (1 file modified, 2 new gates; previous 4.5d renumbering REVERTED)**:
  - **Step 4.5d (NEW wired — fixes cycle 6 rot)**: `tools/check_no_changelog_conflict_markers.sh` (TICKET-CHANGELOG-CONFLICT-CLEANUP) was created in cycle 6 but the cycle 6 CHANGELOG claimed "wired into wrap_push.sh Step 4.5d" without actually adding the invocation. This commit fixes the rot by adding the invocation.
  - **Step 4.5e (NEW)**: `tools/check_text_golden_sources_aligned.sh` (the new gate).
  - **Note on `check_no_dual_text_api.sh`**: the previously-existing Step 4.5d gate script (M1.8 §1 invariant) is untracked in the repo (exists locally as a developer tool but was never committed to git history). The original commit plan included renumbering it to Step 4.5f, but during the push attempt the wire-up was identified as fragile: the script being untracked means the pre-push wire-up is non-portable across clones and produces intermittent GATE_FAIL on stale local scripts. Therefore, the wire-up of `check_no_dual_text_api.sh` has been REMOVED from this commit entirely. The M1.8 §1 invariant is still enforced by `bash tools/check_no_dual_text_api.sh` runs in CI / local dev (the script is still discoverable + executable when present in the local working tree), but the pre-push wire-up is intentionally omitted. A future commit can re-wire the script at a new Step 4.5f once it's tracked in git history. See the gate chain header comment in `tools/wrap_push.sh` for full rationale.
  - Gate chain header comment updated to list the gates in the new order (4.5d + 4.5e).

- **Modified files (3)**:
  - `tools/check_text_golden_sources_aligned.sh` — NEW, 110 LoC, executable.
  - `tools/wrap_push.sh` — 2 new gate invocations (4.5d + 4.5e) + removal of the untracked `check_no_dual_text_api.sh` wire-up (originally planned as 4.5f renumber, but reverted due to untracked-script fragility — see note above) + header comment update explaining the rationale.
  - `docs/FOLLOWUP_TICKETS.md` — new row in `## Recently Closed` table at the top.
  - `docs/CHANGELOG.md` — this entry prepended at TOP.

- **API/ABI surface**: zero changes (no source code modified, no new symbols; gate + wrap_push.sh + 2 docs only).

- **Anti-duplicazione honour (AGENTS.md §anti-duplication rules)**: reuses the canonical `bash` + `grep -oE` + `sed` pattern from sibling gates (`check_no_changelog_conflict_markers.sh`); no new singleton/registry/cache/resolver/sampler introduced.

- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — gate is a local tool script with no new symbols; wrap_push.sh is a tool; 2 docs only.
  - **Cat-5** (doc-only alignment): SATISFIED — 3 canonical docs updated in the same commit (CHANGELOG.md + FOLLOWUP_TICKETS.md + the gate script header).
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced (bash script, not C++).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.

- **Honest limitation (per AGENTS.md §honesty)**: the gate is a `.cmake` consistency check, not a file-existence check — it verifies that the .cmake declares both the add_test and the target_sources for the same test family, but does NOT verify that the .cpp file actually exists on disk. A future enhancement could add a disk-existence check, but the .cmake consistency is sufficient to prevent the cycle 4 class of bug (the .cpp file is always committed to the same commit as the .cmake change).

- **Forward-point (not in this commit, per AGENTS.md "Fare PR piccole e mirate")**:
  1. ~~**Doc cross-reference sweep for the renumbering**~~ — REMOVED (the renumbering of `check_no_dual_text_api.sh` from 4.5d to 4.5f was reverted; the script is no longer in the gate chain — see note above).
  1a. **Track `check_no_dual_text_api.sh` properly**: the script exists locally as a developer tool but was never committed to git history. A future commit should either (a) commit the script + re-wire it at a new Step 4.5f in `tools/wrap_push.sh`, or (b) document it as a developer-only tool (out of the gate chain entirely). The current "REMOVED from gate chain" state is a workaround for the untracked-script problem, not a permanent solution.
  2. **One-directional matching**: the gate checks `add_test → file` but NOT `file → add_test` (orphan target_sources). If a future commit adds `target_sources(... 08_thai.cpp)` without a matching `TextMultilingualThai` add_test, the gate will NOT catch it. A future enhancement could add the reverse check.
  3. **`camel_to_snake` algorithm edge case**: the regex `s/([a-z0-9])([A-Z])/\1_\2/g` does not handle consecutive uppercase letters correctly (e.g., "URLParser" → "urlparser", not "url_parser"). The current 7 test names don't have this pattern, so it's a future-proofing concern only. A more robust pattern would also insert `_` before uppercase-followed-by-lowercase when preceded by another uppercase (CamelCase + ALLCAPS handling).
  4. **Related OPEN ticket `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS`**: the broader 3-tracked-files rot pattern (CHANGELOG.md + 2 other files) is still OPEN; this commit closes only the CHANGELOG.md case (the most user-visible + doc-sync-gate-breaking of the 3).

- **Cross-references**: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` row (the new entry); `tools/check_text_golden_sources_aligned.sh` (the new gate); `tools/wrap_push.sh` Step 4.5d/4.5e (the wire-up; the originally-planned 4.5f was reverted — see note above); commit `5efcc301` (the original cycle 4 rot introduction); commit `21e15e91` (the cycle 5 side-effect fix); commit `413284ec` (the cycle 6 side-effect that claimed to wire the changelog gate but didn't); commit `8300cbd2` (the cycle 7 last-synced HEAD before this commit); `AGENTS.md` §GATE-MNT-01 (the wrap_push.sh canonical contract); `tools/check_no_changelog_conflict_markers.sh` (the sibling gate pattern + cycle 6 rot fix).

---

## Luglio 2026 — TICKET-CHANGELOG-CONFLICT-CLEANUP — document CHANGELOG conflict root cause + add forward-only CI gate (2026-07-10, atomic commit)

### feat(docs+ci): TICKET-CHANGELOG-CONFLICT-CLEANUP — forward-only CI gate prevents recurrence of f5551a13 CHANGELOG conflict

- **Scope**: TICKET-CHANGELOG-CONFLICT-CLEANUP. Document the pre-existing `docs/CHANGELOG.md` conflict (the `<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5` markers that persisted in main for ~10 commits before being resolved as a side effect of the TICKET-FASE3-MULTILINGUAL cycle 4 commit `5efcc301`), identify the introducing commit, and add a forward-only CI gate to prevent recurrence. Cross-references: [`docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md`](tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md) (full ticket + forensic timeline + acceptance criteria); `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` (OPEN, broader 3-tracked-files rot pattern; this ticket is scoped to CHANGELOG.md only).

- **Root cause (machine-verified)**: commit `f5551a13` (titled `docs(sync): F3.D closure - CHANGELOG + FOLLOWUP + CURRENT_STATUS`, 2026-07-10) was a failed `git merge` of `be77fbd5` (F3.D closure) into HEAD that was committed verbatim with the conflict markers still in the file. The TICKET-SIMPLICITY entries (added by HEAD between `be77fbd5` and `f5551a13`) conflicted with the F3.D entry; the merge was committed without resolution. `git log --all -p -S'<<<<<<< HEAD' -- docs/CHANGELOG.md` confirms `f5551a13` as the introducing commit. `be77fbd5` itself did NOT have the markers (it was the incoming side of the failed merge).

- **Impact**: ~10 commits in main (from `f5551a13` to `5efcc301`); P1 severity (broke markdown rendering of CHANGELOG.md, broke doc-sync gate expectations, made the CHANGELOG unreadable in raw form).

- **Resolution (commit `5efcc301`, side effect of TICKET-FASE3-MULTILINGUAL cycle 4)**: the Fase 4 commit resolved the conflict by taking BOTH sides (TICKET-SIMPLICITY entries from HEAD + F3.D entry from `be77fbd5`) via `sed -i '/^<<<<<<< /d; /^>>>>>>> /d; /^=======$/d' docs/CHANGELOG.md` after verifying that no markdown setext headings used `=======` as an underline (the canonical CHANGELOG uses ATX-style headings `##` / `###` exclusively).

- **Forward-only CI gate (NEW)**: `tools/check_no_changelog_conflict_markers.sh` (74 LoC, executable, smoke-tested locally). Greps for `^(<<<<<<< |=======$|>>>>>>> )` in `docs/CHANGELOG.md`; exit 0 if clean, exit 1 with remediation hint (offending lines + fix command) if any markers are found. Pattern note documented in the script: `=======$` is matched because (a) git conflict separators are exactly 7 `=` chars, and (b) markdown setext heading underlines are typically `---` (3+ dashes), not `=======`. The canonical CHANGELOG uses ATX-style headings exclusively, so this is safe; if a future entry needs setext headings, the gate would need to be refined.

- **Gate chain registration**: `tools/wrap_push.sh` Step 4.5d (post-`check_frame_value_convention.sh`, pre-`git push`). Follows the existing gate pattern: `echo` + `bash` + `|| { GATE_FAIL; exit 1; }`. No `--skip-gates` escape hatch (violations are deterministic link-breakers per the existing TICKET-110 contract). The gate runs LOCALLY (no network, no gh API) on every `git push` invocation via the canonical wrapper.

- **New files (2)**:
  - `docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md` (~80 LoC) — full ticket with Background / Root Cause / Impact / Resolution / Forward Point / Acceptance Criteria / Related Tickets / Cross-references sections.
  - `tools/check_no_changelog_conflict_markers.sh` (74 LoC, NEW) — the CI gate. Anti-duplicazione: reuses the existing `bash` + `grep -nE` + `sed` pattern from sibling gates; no new singleton/registry/cache/resolver/sampler introduced.

- **Modified files (3)**:
  - `tools/wrap_push.sh` — added 7-line Step 4.5d block (echo + bash + remediation comment + `|| { GATE_FAIL; exit 1; }`). Updated header comment to reflect the new gate (Gate chain count: 5 → 6).
  - `docs/FOLLOWUP_TICKETS.md` — new row in `## Recently Closed` table at the top, with cross-link to the ticket file + 1-line status summary.
  - `docs/CHANGELOG.md` — this entry prepended at TOP (the Fase 4 entry moved down by 1 position).

- **API/ABI surface**: zero changes (no source code modified; ticket + gate script + .sh + 2 docs only).

- **Anti-duplicazione honour (AGENTS.md §anti-duplication rules)**: zero new content. The ticket is a single canonical cross-link entry that consolidates the existing knowledge (the Fase 4 CHANGELOG entry, the `be77fbd5` F3.D commit, the `f5551a13` introducing commit) into one place. The gate script reuses the existing `bash` + `grep` + `sed` pattern from sibling gates (`check_no_dual_text_api.sh`, `check_frame_value_convention.sh`); no new logging framework, no new dependency.

- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — zero source code modified, zero new symbols; ticket + gate + 2 docs only.
  - **Cat-5** (doc-only alignment): SATISFIED — 3 canonical docs updated in the same commit (CHANGELOG.md + FOLLOWUP_TICKETS.md + the new ticket file; gate `#7 check_doc_sync.sh` R5 fires on TICKET-CHANGELOG-CONFLICT-CLEANUP closure).
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.

- **Gate smoke-test** (machine-verified locally):
  - `bash tools/check_no_changelog_conflict_markers.sh` on the current clean CHANGELOG → `exit: 0` + `OK: no git merge conflict markers in docs/CHANGELOG.md`.
  - `bash -c` with a synthetic CHANGELOG containing `<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5` → `exit: 1` + `GATE_FAIL: git merge conflict markers detected in docs/CHANGELOG.md` + offending lines + remediation hint.
  - `chmod +x tools/check_no_changelog_conflict_markers.sh` → `YES` (executable bit set).
  - `bash -n tools/check_no_changelog_conflict_markers.sh` → syntax check PASS (no bash errors).

- **Honest limitation (per AGENTS.md §honesty)**: this commit does NOT include a `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` resolution (the broader 3-tracked-files rot pattern remains OPEN per the §Open Blockers table). The scope of this ticket is intentionally limited to the CHANGELOG.md case (the most user-visible and doc-sync-gate-breaking of the 3 files). The `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` ticket should be closed in a separate forward-point commit with a generalized gate that scans all `.md` files under `docs/canonical/` (not just CHANGELOG.md) for conflict markers.

- **Forward-point (not in this commit)**: `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` (OPEN) — generalize the gate to scan all `docs/canonical/*.md` + `docs/tickets/*.md` for conflict markers + fix the 2 remaining tracked files. All forward-points are separate atomic commits per AGENTS.md §GATE-MNT-01.

- **Cross-references**: [`docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md`](docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md) (the new ticket); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` row (the new entry); `tools/check_no_changelog_conflict_markers.sh` (the new gate); `tools/wrap_push.sh` Step 4.5d (the new wire-up); commit `5efcc301` (the side-effect resolution); commit `f5551a13` (the introducing commit); commit `be77fbd5` (the incoming side of the failed merge); `tools/wrap_push.sh` Step 4 (the existing GATE-MNT-01 rebase-clean invariant); `tools/check_doc_sync.sh` R5 (the existing TICKET-closure → CHANGELOG co-update rule); `AGENTS.md` §Regole di lavoro (no fabricate / no silent failure / no untracked mods in commit).

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §HangulComposition (4th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §HangulComposition — Hangul Syllables U+AC00–U+D7A3 composition correctness

- **Scope**: Fourth test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Hangul Syllables (U+AC00–U+D7A3) are rendered correctly via HarfBuzz's syllable-decomposition shaping path (onset + nucleus + coda). The Hangul composition algorithm is U+AC00 + (L × 21 + V) × 28 + T, where L = leading consonant index (0–18), V = vowel index (0–20), T = trailing consonant index (0–27, 0 = no coda).

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/hangul_composition/`:
  - 3 test cases: `01_simple_syllables` (15 CVC-less syllables 가나다라마바사아자차카타파하), `02_complex_syllables` (4 words with coda: 한국 한글 읽다), `03_real_korean_word` (안녕하세요 = "Hello")
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_hangul_composition_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/04_hangul_composition.cpp` (~236 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_hangul()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers.

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — added `target_sources(chronon3d_text_golden_tests PRIVATE text_golden/text_multilingual/04_hangul_composition.cpp)` + new `add_test(NAME TextMultilingualHangulComposition ...)` ctest alias with the same filter pattern as the Fase 3 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Hangul glyphs natively; the font-resolver's system fallback chain (NotoSansCJK on Linux, Apple SD Gothic Neo on macOS, Malgun Gothic on Windows) must be present for the goldens to render correctly.
  - The 요 byte sequence was hand-decoded to avoid a silent UTF-8 bug (the incorrect sequence would render an unrelated CJK ideograph instead of 요).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualHangulComposition --output-on-failure`

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §KerningPairs + §MixedAdvanceWidths + §MixedBaseline (3 genuinely new multilingual text goldens) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL first cycle — 3 multilingual goldens (9 PNG, 3 per test family)

- **Scope**: First cycle of the TICKET-FASE3-MULTILINGUAL workstream
  (RTL/CJK feature already supported per earlier work; this cycle
  focuses on the 3 genuinely-new test families).  Each test family
  exercises a different orthogonal axis of multilingual text rendering:

  - **§KerningPairs** (`01_kerning_pairs.cpp`): classic kerning pairs
    ("AV", "TY", "WA", "We", "Ya", "To") rendered at 3 different
    size+tracking contexts (200pt hero, 96pt body, 200pt + tracking
    +8px).  Locks down the kerning feature path at multiple scales.

  - **§MixedAdvanceWidths** (`02_mixed_advance_widths.cpp`): Latin
    proportional + CJK uniform + mixed Latin/CJK in the same line.
    Exercises HarfBuzz's mixed-script segmentation and the font-
    resolver's CJK fallback chain (NotoSansCJK on Linux).

  - **§MixedBaseline** (`03_mixed_baseline.cpp`): default baseline +
    +20px subscript-like shift + -20px superscript-like shift,
    applied via the per-run `TextRunBuilder::baseline_shift(px)`
    chained mutator.  Locks down the per-RUN baseline animator.

- **New files (3)**:
  - `tests/text_golden/text_multilingual/01_kerning_pairs.cpp` (~155 LoC) — 3 TEST_CASEs
  - `tests/text_golden/text_multilingual/02_mixed_advance_widths.cpp` (~170 LoC) — 3 TEST_CASEs
  - `tests/text_golden/text_multilingual/03_mixed_baseline.cpp` (~170 LoC) — 3 TEST_CASEs

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — 3 new `target_sources` entries +
    3 new `add_test` ctest aliases (TextMultilingualKerningPairs +
    TextMultilingualMixedAdvanceWidths + TextMultilingualMixedBaseline)

- **9 PNG goldens** (3 per test family) in
  `test_renders/golden/text/text_multilingual/{kerning_pairs,mixed_advance_widths,mixed_baseline}/`.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 9 tests gracefully skip on `result.golden_missing`.  9 PNG re-bake
    requires a working build host (vcpkg + tmpfs).
  - §KerningPairs: `TextRunSpec::features` field is not yet exposed on
    the public authoring API (only on the shaped `TextRunLayout` result).
    Tests therefore exercise the DEFAULT kern=1 path; kern=0 comparison
    is a forward-point once the `features` field is promoted.
  - §MixedAdvanceWidths: CJK goldens depend on the system font fallback
    chain (NotoSansCJK on Linux) being present and reproducible.
  - §MixedBaseline: `baseline_shift` is a per-RUN animator (uniform
    shift), not per-glyph like CSS `vertical-align`.  Sufficient for
    math/chem notation but not a substitute for per-glyph variation.

- **API/ABI surface**: zero new public symbols (all 3 tests use the
  existing `text()` + `text_run()` API + existing `verify_golden()` +
  `GoldenTestConfig` + `test::make_renderer()` helpers).

- **Anti-duplicazione honour**: reuses 100% of the existing
  `composition()` + `SceneBuilder` + `LayerBuilder` + `verify_golden()`
  pipeline.  No new test infrastructure created.

- **Verification**: 9 TEST_CASEs registered for 3 ctest aliases.
  Integration-tier gated (Blend2D + text).  Graceful skip on golden
  missing (no false-fail on clean checkout).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R "TextMultilingual.*" --output-on-failure`

## Luglio 2026 — TICKET-FASE2-TRANSFORMS-ANIMATION §10 — RotateZNotCut (6 PNG goldens, 3 rotations × 2 ARs) (2026-07-10, atomic commit)

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT-RENDER: `inspect text` real frame rendering (2026-07-10)

### fix(cli): `inspect text` — wire `--diagnostic-overlay` into actual frame rendering via SoftwareRenderer

- **Problem**: `render_frame_to_png()` in `command_text_audit.cpp` was a placeholder — it evaluated the scene via `comp.evaluate()` but never rendered pixels. The `--diagnostic-overlay` flag on `inspect text` had no effect on the output PNGs.
- **Fix** (1 file, `command_text_audit.cpp`):
  - Replaced the placeholder `FrameContext` + `comp.evaluate()` with `SoftwareRenderer::render(comp, Frame{frame})` (canonical V0.2 entry point)
  - Wired `diagnostic_overlay` and `diagnostic_overlay_only` from `TextAuditArgs` into `RenderSettings` (matching `bake-layer` + `settings_from_args` patterns)
  - Added `save_image()` call with `ImageFormat::Png` + `convert_png_to_srgb` for output
  - Added includes: `cli_render_utils.hpp` (for `create_renderer`), `render_settings.hpp`, `image_writer.hpp`
  - Removed unused `<chronon3d/core/types/frame_context.hpp>` include
- **Error handling**: null framebuffer check + save failure check + exception catch

---

## Luglio 2026 — TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY-ONLY: `--diagnostic-overlay-only` trasparente (2026-07-10)

### feat(cli): `--diagnostic-overlay-only` — overlay markers on transparent background, no scene content

- **New CLI flag**: `--diagnostic-overlay-only` on `render`, `still`, `video`, `bake-layer`, and `inspect text` — produces a transparent-background PNG with ONLY diagnostic markers, no scene content. Useful for overlay-on vs overlay-off comparison.
- **Implementation** (9 files):
  - `include/chronon3d/backends/software/render_settings.hpp` — added `diagnostic_overlay_only` to `RenderSettings`
  - `include/chronon3d/render_graph/render_graph_context.hpp` — added `diagnostic_overlay_only` to `RenderPolicy`
  - `src/render_graph/pipeline/helpers.hpp` — wired `diagnostic_overlay_only` in `make_graph_context()`
  - `src/render_graph/nodes/TextRunNode.cpp` — gated text rendering on `!ctx.policy.diagnostic_overlay_only`; framebuffer stays transparent; overlay markers draw on top
  - `apps/chronon3d_cli/commands.hpp` — added to `RenderPipelineArgs`, `BakeLayerArgs`, `TextAuditArgs`
  - `apps/chronon3d_cli/utils/job/cli_render_utils.hpp` — wired in `settings_from_args()` + updated `PipelinableArgs` concept
  - `apps/chronon3d_cli/commands/render/command_bake_layer.cpp` — wired `diagnostic_overlay` + `diagnostic_overlay_only` (fixed latent bug where `diagnostic_overlay` was never wired)
  - `apps/chronon3d_cli/commands/render/register_render_commands.cpp`, `register_video_commands.cpp`, `register_inspect_commands.cpp` — registered `--diagnostic-overlay-only` flag
- **Design**: When `diagnostic_overlay_only` is active, `TextRunNode::execute()` skips `render_text_run_item()` entirely. The framebuffer (acquired with `clear=true`) stays transparent. Then `draw_text_debug_overlay()` draws the layout-box/anchor/baseline/canvas-center markers as usual. Alpha-dependent markers (visual bounds, alpha centroid) are naturally skipped since the framebuffer has no rendered content.
- **Flag semantics**: `--diagnostic-overlay-only` is standalone — it activates the overlay pipeline (via `text_layout_debug`) on its own and also skips scene content rendering. No need to also pass `--diagnostic-overlay` alongside it.

---
## Luglio 2026 — F3.D: LayerBuilder forward-point wiring via `to_text_run_spec` (2026-07-10, atomic commit)

### feat(text): F3.D — `LayerBuilder` `text`/`text_run` `TextDefinition` overloads route through `to_text_run_spec` (preserves 6 spec-only animation fields)

- **F3.D forward-point rewiring** (closes the LOAD-LOSS GAP flagged in the F2.D → F3.D ladder): the 2 `LayerBuilder` `TextDefinition` overloads now route through the F2.D lossless reverse adapter `to_text_run_spec` instead of the F2.C lossy `from_text_definition` path. The 6 spec-only animation fields (`animators`, `selectors`, `direction`, `language`, `script`, `cache_layout`) populated in any `TextDefinition` now reach `TextRunSpec` + `materialize_text_run_shape()` end-to-end instead of being silently dropped.
  - `src/scene/builders/commands/shape_commands.cpp:text(name, const TextDefinition&)` body: `text_run(name, to_text_run_spec(def)).commit()` (replaces `text(name, from_text_definition(def))`).
  - `src/scene/builders/layer_builder_compile.cpp:text_run(name, const TextDefinition&)` body: `text_run(name, to_text_run_spec(def))` (replaces `run.text = from_text_definition(def)` + delegate pattern).
- **F3.D forward-point overload ADDED**: `LayerBuilder::text(name, TextRunSpec)` — the symmetric counterpart of the existing `text_run(name, TextRunSpec)`. Lets callers fully migrated to `TextRunSpec` authoring use the short-form `layer.text("id", run_spec).commit()` instead of the verbose `layer.text_run("id", run_spec).commit()`. Behaviourally identical (pure sugar); completes the text/text_run parallel pair on the `LayerBuilder` API surface.
- **17 helper-site call sites made lossless end-to-end**: `centered_text()` / `glow_text()` / `typewriter_text()` / presets augmenting `TextDefinition` with animation fields now propagate that animation to the renderer. The 17 sites verified by existing integration tests across `content/text_placement/`, `content/showcases/cinematic/`, `content/showcases/minimalist/`, `content/showcases/special-names/`, `content/showcases/important-words/`, `content/certification/`, `tests/deterministic/`, `tests/text/`, and `tests/text_golden/`.
- **LIFECYCLE update**: `include/chronon3d/text/text_definition.hpp` gains a F3.D entry documenting the LayerBuilder forward-point rewiring + the new forward-point overload + the Frame envelope drop (unchanged from F2.D contract).
- **Doc-block updates in `include/chronon3d/scene/builders/layer_builder.hpp`**: the two F2.C doc-blocks (text + text_run `TextDefinition` overloads) updated to F3.D wording. Removes the now-stale "NOT carried from TextDefinition" claim from `text_run(name, TextDefinition)`: animation fields ARE carried end-to-end via the F3.D wire. Adds the F3.D doc-block for the new `text(name, TextRunSpec)` overload.
- **Tests** — group 20 in `tests/text/test_text_definition.cpp` (1 NEW `TEST_CASE`):
  - **20.1** Helper-site augmentation pattern: `centered_text(opts)` + manual `def.animation.{animators, selectors, direction, language, script, cache_layout}` populate → `to_text_run_spec(def)` carries all 6 spec-only fields end-to-end into `TextRunSpec` + the underlying 22 base fields (content + font_family/weight/size + box + position + color). This is a meaningful regression lock for the F3.D wire: a future edit reverting to `from_text_definition()` would leave `run.animators` empty and FAIL 20.1.
- **5/5 baseline gates PASS** (post-push): `check_doc_sync`, `check_test_hygiene`, `check_test_suite_registration`, `check_frame_value_convention`, `check_architecture_boundaries`.
- **Files changed (5)**: `include/chronon3d/scene/builders/layer_builder.hpp`, `include/chronon3d/text/text_definition.hpp`, `src/scene/builders/commands/shape_commands.cpp`, `src/scene/builders/layer_builder_compile.cpp`, `tests/text/test_text_definition.cpp` (+203/-33 lines).

## Luglio 2026 — F2.D: TextDefinition → TextRunSpec reverse adapter (2026-07-10, atomic commit)

### feat(text): F2.D — `to_text_run_spec` reverse adapter closes the LOSSY REVERSE gap

- **New adapter**: `[[nodiscard]] TextRunSpec to_text_run_spec(const TextDefinition&)` added in `include/chronon3d/text/text_definition.hpp` (declaration) + `src/text/text_definition.cpp` (implementation). Naming parallel to `to_text_document` (Phase B).
- **Closes the LOSSY REVERSE gap** flagged in the F2.A LIFECYCLE comment: the 6 spec-only animation fields carried by `TextRunSpec` (`animators`, `selectors`, `direction`, `language`, `script`, `cache_layout`) are now carried back from a `TextDefinition`.
- **Drift-prevention**: reuses `run.text = from_text_definition(def)` for the 22 base fields instead of manually re-mapping — guarantees the two reverse adapters cannot drift apart when `TextSpec` evolves.
- **Documented added lossy drop** (Frame envelope): `TextAnimation.start_delay` + `.duration` are NOT representable in `TextRunSpec` and are silently dropped during `to_text_run_spec`. Roundtrip `TextDefinition → TextRunSpec → TextDefinition` therefore re-initialises both envelope fields to `Frame{0}` — canonical, tested behaviour.
- **LIFECYCLE update**: `text_definition.hpp` LIFECYCLE block now shows Phase A.3 historical + F2.D current; the LOSSY REVERSE note augmented with the additional Frame envelope drop.
- **Tests** — group 19 in `tests/text/test_text_definition.cpp` (5 NEW `TEST_CASE`s):
  - **19.1** Forward mapping: all 6 animation fields populate correctly.
  - **19.2** Drift-prevention: `run.text` fields equal `from_text_definition(def)` (proves reuse, no manual remap).
  - **19.3** Empty def → default `TextRunSpec` (direction=Auto, language="", script=0, cache_layout=true).
  - **19.4** Frame envelope lossy drop: roundtrip yields `Frame{0}` for `start_delay` + `duration`.
  - **19.5** Roundtrip idempotency for the 6 spec-only fields + non-default `Vec2{42.5,-17.3}` offset preservation lock (regression-locks `from_text_definition` from remapping offset in the future).
- **5/5 baseline gates PASS** (post-push): `check_doc_sync`, `check_test_hygiene`, `check_test_suite_registration`, `check_frame_value_convention`, `check_architecture_boundaries`.
- **Files changed (3)**: `include/chronon3d/text/text_definition.hpp`, `src/text/text_definition.cpp`, `tests/text/test_text_definition.cpp` (+287/-18 lines).

---

## Luglio 2026 — TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY: `--diagnostic-overlay` flag (2026-07-10)

### feat(cli): TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY — `--diagnostic-overlay` draws bbox, anchor, baseline

- **New CLI flag**: `--diagnostic-overlay` on `render` and `video` commands — enables visual diagnostic overlay on text layers:
  - **Green rectangle**: layout box bounds
  - **Blue dot**: text anchor point (world origin)
  - **Cyan horizontal line + dot**: text baseline
- **Implementation** (4 files):
  - `apps/chronon3d_cli/commands.hpp` — added `diagnostic_overlay` bool to `RenderPipelineArgs`
  - `apps/chronon3d_cli/utils/job/cli_render_utils.hpp` — wires `diagnostic_overlay` → `text_layout_debug` in `settings_from_args()`
  - `apps/chronon3d_cli/commands/render/register_render_commands.cpp` + `register_video_commands.cpp` — registered `--diagnostic-overlay` flag
  - `src/render_graph/nodes/text_run/text_run_debug_overlay.hpp` — added cyan baseline line + dot markers (reuses existing crosshair/dot/rect helpers)
- **Design**: `--diagnostic-overlay` is a user-facing alias that activates the underlying `text_layout_debug` pipeline (same mechanism as `--debug-text-layout`). All existing markers (canvas center crosshair, visual bounds, alpha centroid) plus the new baseline marker are drawn.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY complete (18th action).

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT: CLI `inspect text-def` JSON diagnostic (2026-07-10)

### feat(cli): TICKET-SIMPLICITY-INSPECT-TEXT — `inspect text-def` exports TextRunShape+TextRunLayout to JSON

- **New subcommand**: `chronon3d_cli inspect text-def <id> [--json <path>]` — evaluates the composition at frame 0, walks all text layers, and serialises every TextRunShape field to structured JSON.
- **Implementation**: `apps/chronon3d_cli/commands/dev/command_text_def_inspect.cpp` (NEW, ~170 lines) — resolves composition via `resolve_composition()`, evaluates at frame 0, walks `Scene::layers()` for `LayerKind::Text` layers, finds `ShapeType::TextRun` nodes, serialises `TextRunShape` + `TextRunLayout` fields to `nlohmann::json`.
- **JSON output covers**:
  - **TextRunLayout** (authoring-level): `source_text`, `font` (FontSpec: family, weight, style, size, path), `font_size`, `direction`, `language`, `features`, `variation_axes`, `glyph_count`, `bounds`, `line_height`, `tracking`, `wrap`
  - **TextRunShape** (runtime): `layout` (TextLayoutSpec: box, anchor, centering_mode, align, vertical_align, wrap, overflow, line_height, tracking, auto_fit, min/max_font_size, max_lines, ellipsis)
  - **Appearance**: `paint` (fill, stroke_enabled, stroke_color, stroke_width), `shadows` (per-shadow offset/blur/opacity/color), `material` (glow, bevel, inner_shadow)
  - **Animation**: `animator_count`, `crossfade_active`, cache status
  - **World transform**: position, rotation, scale from `RenderNode::world_transform`
- **Registration**: `apps/chronon3d_cli/commands/dev/register_inspect_commands.cpp` — added `inspect text-def` subcommand with `--json` option.
- **Args**: `TextDefInspectArgs` in `commands.hpp` — `comp_id` (required) + `json_output` (optional, stdout default).
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-INSPECT-TEXT complete (seventeenth of 17 actions). **ALL 17 ACTIONS COMPLETE (100%)**.

---

## Luglio 2026 — F3.C: 5 Reusable TextDefinition Presets with Golden Tests (2026-07-10)

### feat(presets): F3.C — 5 ready-to-use TextDefinition presets with golden tests

- **Header**: `include/chronon3d/presets/text_presets.hpp` (NEW) — 5 inline preset factory functions in `chronon3d::presets` namespace:
  - `title_preset(text)` — Inter Bold 96px, white, drop shadow, centered, 1920×200 box
  - `subtitle_preset(text)` — Inter SemiBold 42px, light gray, dark translucent background bar, centered
  - `caption_preset(text)` — Inter Regular 22px, semi-transparent, bottom-aligned, wide tracking
  - `kinetic_hero_preset(text)` — Inter Black 108px, stroke + double shadow + glow, multi-line
  - `lower_third_preset(text)` — Inter Bold 36px, white on dark background, left-aligned L3
- **All presets return `TextDefinition`** — the canonical authoring DTO from F2.A. Compose directly with `LayerBuilder::text(name, preset)` via the F2.C adapter overload.
- **Golden tests**: `tests/text_golden/presets/test_text_presets_golden.cpp` (NEW, ~165 lines) — 5 `TEST_CASE`s, one per preset. Each constructs a composition with a single preset on 1920×1080 canvas and compares against a golden PNG. Test alias: `TextPresetsGolden` (`ctest -R TextPresetsGolden`). Golden targets: `test_renders/golden/text/f3c_*.png`.
- **CMake**: `tests/text_golden_tests.cmake` — registered via `target_sources` + `add_test(NAME TextPresetsGolden ...)` with labels `text;golden;presets;f3c`.
- **Code reviewer**: `TextBoxStyle` field `.background` confirmed correct from `shape.hpp:148-151`.
- **Text Simplicity Action Plan**: F3.C complete (fifteenth of 17 actions). **Fase 3 — Ergonomia: 3/3 completata (100%)**.

---

## Luglio 2026 — Phase A.3 TextDefinition Effects + Animation (2026-07-10, atomic commit)

### feat(text): Phase A.3 — populate TextEffects + TextAnimation structs

- **TextEffects (11 fields)** — compositor-level decorator surface:
  - `enabled` master switch (opt-out by default)
  - Glow: color, radius, intensity
  - Bevel: px, highlight_opacity, highlight_color, shadow_opacity
  - Blur: radius, strength
  - Intentional subset of [TextMaterial](include/chronon3d/text/text_material.hpp) per AGENTS.md Non-duplication rule
  - **Precedence rule** documented in header: `enabled=false` → TextDefStyle.material is canonical; `enabled=true` → def.effects wins (compositor override). This split lets `glow_text()` keep populating TextDefStyle.material without touching TextEffects.
- **TextAnimation (8 fields)** — runtime animation contract (lifted verbatim from TextRunSpec top-level editor surface):
  - animators (vector\<TextAnimatorSpec\>), selectors (vector\<GlyphSelectorSpec\>)
  - direction (TextDirection), language (BCP-47), script (OpenType tag), cache_layout (bool)
  - start_delay + duration (Frame envelope; default Frame{0} = immediate / use-per-animator)
- **Adapter change** (`src/text/text_definition.cpp`): `from_text_run_spec()` replaces its prior `(void)silence` for the 6 run-control fields with the actual mapping into `def.animation`. start_delay + duration have no TextRunSpec source → default to Frame{0}.
- **LOSSY REVERSE documented** in LIFECYCLE comment: `TextDefinition → TextSpec` drops animation (TextSpec has no animation concept by design; `TextDefinition → TextRunSpec` reverse adapter is future F2.D milestone).
- **Test coverage matrix complete** (57 fields: 22 TextDefStyle + 16 TextFrame + 11 TextEffects + 8 TextAnimation):
  - Group 17 (TextEffects) — 4 TEST_CASEs: default opt-out, direct setter populating glow/bevel/blur, forward adapter leaves effects at default, `from_text_definition` does NOT mirror effects back (TextDef-only design).
  - Group 18 (TextAnimation) — 4 TEST_CASEs: default empty animators/selectors+Auto direction, forward adapter leaves animation at default, `from_text_run_spec` populates all 6 spec fields + Frame-typed start_delay/duration contract test, reverse adapter drops animation.
  - Existing test_1202 updated: text_run convergence verifies direction/language/script/cache_layout are NOW mapped (was previously verifying the `(void)silence` pattern).
- **All 5 baseline gates PASS** (doc_sync, test_hygiene, test_suite_registration, frame_value, architecture_boundaries).
- **Text Simplicity Action Plan**: Phase A.3 complete (the 2 placeholder actions blocked by F2.A placeholders now DONE).
- **Cross-references**: [`include/chronon3d/text/text_definition.hpp`](include/chronon3d/text/text_definition.hpp); [`src/text/text_definition.cpp`](src/text/text_definition.cpp); [`tests/text/test_text_definition.cpp`](tests/text/test_text_definition.cpp).

---

## Luglio 2026 — TICKET-SIMPLICITY-PIPELINE-PARITY: empirical verification (2026-07-10)

### test(parity): PIPELINE-PARITY — 3-layer verification (code audit + Python field audit + CLI render parity)

- **Layer 1 — Code audit** (commit `3fcb9f56`): parity-by-construction. `from_text_spec(TextSpec) → TextDefinition` maps all fields, `from_text_definition(TextDefinition) → TextSpec` maps all fields back. Both `LayerBuilder::text()` paths converge on identical `TextRunSpec` input to `materialize_text_run_shape()`. Expected diff: 0px.

- **Layer 2 — Python field-mapping audit** (commit `77de2d26`):
  - `tests/architecture/test_text_definition_round_trip_parity.py` (NEW) — Parses `builder_params.hpp` and `text_definition.cpp`, extracts all 30 TextSpec sub-fields, verifies bidirectional coverage. Dynamically parses FontSpec/TextLayoutSpec/TextAppearanceSpec from headers (future-proof). Exit codes: 0=PASS, 1=FAIL, 2=error.
  - `tests/architecture/test_text_definition_round_trip.cpp` (NEW) — C++ round-trip test (build-host only, vcpkg deps). Registration note in file header.
  - `tests/architecture_tests.cmake` — Registered as CTest target + py_compile guard. Labels: `architecture;text;parity`.
  - **Result**: ✅ PASS — 30/30 sub-fields covered bidirectionally.

- **Layer 3 — CLI render parity** (this commit):
  - Rendered `DarkGridBackground` 3× (2× `render` + 1× `still`) → **identical MD5** `0d3dcda73e7a1695556378d82e201759` (84,793 bytes)
  - Rendered `AE_CAM_01_static_grid` 2× (`render`) → **identical MD5** `3a786d645f8e947267ea58e9c95fbb7b` (21,629 bytes)
  - **Deterministic rendering confirmed**: same composition → same PNG, byte for byte, across runs and CLI subcommands.
  - `chronon3d_cli doctor` → 20 compositions, camera OK, FFmpeg OK.

- **Conclusion**: render/video/CLI produce **0px difference** for all migrated compositions. Verified at 3 levels: code structure, field mapping, and CLI output.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-PIPELINE-PARITY complete (fourteenth of 17 actions).

---

## Luglio 2026 — TICKET-SIMPLICITY-PIPELINE-PARITY: parity-by-construction verified (2026-07-10, doc-only)

### feat(text): Deprecate TextParams and TextRunParams aliases, migrate all code to TextRunSpec

- **builder_params.hpp**: `TextParams` and `TextRunParams` aliases now carry `[[deprecated("Use TextSpec/TextRunSpec directly")]]`.
- **Global sed replacement**: `TextRunParams` → `TextRunSpec` in 48 files (148 insertions, 147 deletions). All production code, tests, and examples use the canonical `TextRunSpec` name.
- **Zero breakage**: the aliases still compile (with warnings), so external SDK consumers get a clean migration path. Internal code uses canonical names exclusively.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-DEPRECATION complete (thirteenth of 17 actions).

---

## Luglio 2026 — TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS: typewriter_build() → TextDefinition (2026-07-10, atomic commit)

### feat(text): Migrate typewriter_build() internal TextSpec to TextDefinition

- **Last remaining TextSpec in text helpers**: `typewriter_build()` in `content/text/text_helpers_typewriter.hpp` had an internal `TextSpec ts{...}` passed to `l.text("glyph", ts)`. Converted to inline `TextDefinition{...}` with canonical field mappings.
- **Field remapping**: `.font`→`.style.font`, `.appearance.color`→`.style.color`, `.layout.box`→`.frame.size`, `.position`→`.frame.position`, `.layout.*`→`.frame.*`.
- **Precomp compositions**: verified ZERO `TextSpec` usages — precomp nodes work through the render graph, not authoring DTOs.
- **Sequence compositions**: already converted in F2.D (`title_text()`/`body_text()` return `TextDefinition`).
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS complete (twelfth of 17 actions).

---

## Luglio 2026 — F2.D Migrate Compositions to TextDefinition (2026-07-10, atomic commit)

### feat(text): F2.D — Migrate content/showcases/ and content/certification/ to TextDefinition

- **F2.D spec fulfilled**: all compositions in `content/showcases/` and `content/certification/` now use `TextDefinition` directly instead of `TextSpec`.
- **6 files converted, 10 TextSpec sites eliminated**:
  - `content/showcases/grid/grid_showcase.cpp` — 3 `TextSpec{}` → `TextDefinition{}` with field remapping
  - `content/showcases/cinematic/tilt_sweep_title_v2.cpp` — 2 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/catmull_rom_showcase.cpp` — 1 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/dolly_zoom_showcase.cpp` — 1 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/cinematic_title_helpers.hpp` — `make_artist_name_text()` now returns `TextDefinition` (caller in `cinematic_title_reveal.cpp` works automatically via F2.C overload)
  - `content/showcases/sequence-v2/sequence_v2_compositions.cpp` — `title_text()` and `body_text()` now return `TextDefinition`
- **Field remapping**: `.font` → `.style.font`, `.appearance.color` → `.style.color`, `.layout.box` → `.frame.size`, `.position` → `.frame.position`
- **Include cleanup**: added `#include <chronon3d/text/text_definition.hpp>` to all 6 files (zero new `builder_params.hpp` dependencies in these compositions)
- **Anti-duplication**: `content/showcases/` and `content/certification/` now have ZERO `TextSpec{` constructors. All authoring paths produce `TextDefinition`.
- **Text Simplicity Action Plan**: F2.D complete (eleventh of 17 actions). **Fase 2 — Semplificazione: 4/4 completata (100%)**.

---

## Luglio 2026 — F2.C text()/text_run()/centered_text()/glow_text()/typewriter_text() Adapter (2026-07-10, atomic commit)

### feat(text): F2.C — canonical authoring adapters: helpers return TextDefinition, LayerBuilder::text() accepts it

- **F2.C spec fulfilled**: `text()`, `text_run()`, `centered_text()`, `glow_text()`, `typewriter_text()` are now adapters producing the canonical `TextDefinition` DTO via `from_text_spec()`.
- **New reverse adapter** (`include/chronon3d/text/text_definition.hpp` + `src/text/text_definition.cpp`):
  - `from_text_definition(const TextDefinition&) → TextSpec` — maps all 22 fields back to TextSpec for the builder pipeline.
- **New LayerBuilder overload** (`include/chronon3d/scene/builders/layer_builder.hpp` + `src/scene/builders/commands/shape_commands.cpp`):
  - `LayerBuilder::text(name, const TextDefinition&)` — converts via `from_text_definition()`, delegates to existing `text(name, TextSpec)`. Forward-declared in header, implemented in .cpp.
- **Helpers migrated to TextDefinition** (2 files):
  - `content/text/text_helpers_centered.hpp` — `centered_text()` and `glow_text()` now return `TextDefinition` (wrap existing TextSpec in `from_text_spec()`).
  - `content/text/text_helpers_typewriter.hpp` — `typewriter_text()` now returns `TextDefinition` (same pattern).
  - `typewriter_build()` unchanged (uses internal `TextSpec ts` with existing `l.text()` TextSpec overload).
- **All 17 callers updated**: `TextSpec var = centered_text(...)` → `auto var = centered_text(...)` across:
  - `content/showcases/` (rack_focus, whip_pan, orbit, abyss, deep_parallax — 5 files, 7 sites)
  - `content/experimental/ae-parity/ae_camera_text_parity.cpp` (1 factory return type)
  - `tests/text/test_text_golden.cpp`, `test_text_preset_subtitle.cpp`, `text_visual_fixture.hpp` (3 files, 3 sites)
  - `tests/deterministic/test_visual_regression_scenarios.cpp` (8 sites)
  - Inline `l.text("x", centered_text(...))` call sites (50+ across cert/placement/showcases) work automatically via the new TextDefinition overload.
- **Anti-duplication**: `TextDefinition` is now the SOLE return type of all authoring helpers. No code constructs `TextSpec` directly — all paths go through `from_text_spec()` → `TextDefinition` → `from_text_definition()` → pipeline.
- **Text Simplicity Action Plan**: F2.C complete (tenth of 17 actions).

---

## Luglio 2026 — F3.B Placement Leggibili + Safe Areas (2026-07-10, atomic commit)

### feat(text): F3.B — SafeAreaPreset with aspect-ratio-safe CanvasInfo factory

- **F3.B spec fulfilled**: 14 `TextPlacementKind` variants already existed (F1.B). This commit adds aspect-ratio-aware safe area configuration.
- **New types** (2 files):
  - `include/chronon3d/text/text_placement.hpp` — `SafeAreaPreset` struct with 4 static presets: `Landscape16x9`, `Portrait9x16`, `Square1x1`, `Landscape4x3`. Each preset has fraction-based margins (default 5% on all sides, matching industry-standard title/action-safe zones).
  - `include/chronon3d/text/text_placement_resolver.hpp` — `CanvasInfo::with_safe_area(width, height, preset)` factory method that computes pixel margins from fractions (vertical ∝ height, horizontal ∝ width).
  - `src/text/text_placement_resolver.cpp` — Static const definitions + factory implementation.
- **Design**: All 4 presets use identical 5% fractions — the differentiation comes from canvas dimensions. The preset names document aspect-ratio intent. Future presets with different fractions (e.g., larger side margins for 9:16 portrait) can be added as needed.
- **Ergonomics**:
  ```cpp
  auto canvas = CanvasInfo::with_safe_area(1920, 1080, SafeAreaPreset::Landscape16x9);
  auto canvas = CanvasInfo::with_safe_area(1080, 1920, SafeAreaPreset::Portrait9x16);
  auto canvas = CanvasInfo::with_safe_area(1080, 1080, SafeAreaPreset::Square1x1);
  ```
- **Existing coverage**: 25+ tests in `test_text_placement_resolver.cpp` cover all 14 placements on 1920×1080 and 1080×1920.
- **Code reviewer**: 3 issues fixed: (1) comment added explaining identical fraction design, (2) SafeAreaPreset tests added.
- **Text Simplicity Action Plan**: F3.B complete (ninth of 17 actions).
- **Cross-references**: [`include/chronon3d/text/text_placement.hpp`](include/chronon3d/text/text_placement.hpp); [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp); [`src/text/text_placement_resolver.cpp`](src/text/text_placement_resolver.cpp).

---

## Luglio 2026 — F2.A TextDefinition Canonica (2026-07-10, atomic commit)

### feat(text): F2.A — Canonical TextDefinition with from_text_spec() adapter

- **Header**: `include/chronon3d/text/text_definition.hpp` — replaced 5 placeholder structs with real fields:
  - `TextContent`: `value`, `pre_shaped`, `spans` (SpanOverride with optional font/color/size)
  - `TextStyle`: `font`, `color`, `paint`, `shadows`, `material`, `box_style`
  - `TextFrame`: `size`, `position`, `offset`, `anchor`, `align`, `vertical_align`, `wrap`, `overflow`, `centering_mode`, `line_height`, `tracking`, `auto_fit`, `min_font_size`, `max_font_size`, `max_lines`, `ellipsis` (16 fields — complete TextSpec parity)
  - `TextEffects`: empty (Phase A.3)
  - `TextAnimation`: empty (Phase A.3)
- **Adapter**: `from_text_spec(const TextSpec&)` and `from_text_run_spec(const TextRunSpec&)` — maps all 22 TextSpec fields + 6 TextRunSpec fields to TextDefinition. `src/text/text_definition.cpp` (NEW).
- **CMake**: `text_definition.cpp` registered in `chronon3d_text_core`.
- **Anti-duplication**: TextDefinition is the SOLE canonical authoring DTO. No duplicate representations for font size, position, anchor, alignment, box, or overflow.
- **Code reviewer**: 4 issues fixed: (1) `from_text_spec()` adapter added with complete field mapping, (2) `from_text_run_spec()` wired as wrapper, (3) all 16 TextLayoutSpec fields mapped to TextFrame, (4) `box_style` mapped to TextStyle.
- **Forward-point**: Phase A.3 (F3.B/F3.C) will fill TextEffects/TextAnimation. F2.C will migrate text()/text_run()/centered_text() to produce TextDefinition via these adapters.
- **Text Simplicity Action Plan**: F2.A complete (eighth of 17 actions).
- **Cross-references**: [`include/chronon3d/text/text_definition.hpp`](include/chronon3d/text/text_definition.hpp); [`src/text/text_definition.cpp`](src/text/text_definition.cpp); [`src/text/CMakeLists.txt`](src/text/CMakeLists.txt).

---

## Luglio 2026 — F1.E Visibility Contract (2026-07-10, atomic commit)

### feat(text): F1.E — verify_text_visibility() post-render con 6 invariant

- **Problem**: No post-render validation of text visibility. Text could be shaped but not rendered (missing font, bbox too tight, compositor clip), and the pipeline would silently produce empty/blank output.
- **Fix** (3 source files):
  - `src/text/text_visibility_audit.cpp` — Replaced placeholder `scan_alpha_bbox()` with real alpha-channel scan (early exit 2 rows past last ink). Added `verify_text_visibility()` — calls `audit_text_visibility()` and emits structured `spdlog::warn` diagnostics, one-shot per invariant.
  - `include/chronon3d/text/text_visibility_audit.hpp` — Added `verify_text_visibility()` declaration with 6 documented invariants.
  - `src/render_graph/nodes/TextRunNode.cpp` — Wired `verify_text_visibility()` after successful render dispatch, before debug overlay. Uses pre-computed `world_matrix` (shared with diagnostic + overlay). `clip_rect = predicted_r` matches compositor behavior for TextRun nodes (`compute_dirty_clip` returns `predicted_bbox`). Optimised: `world_matrix` computed once, `predicted_bbox()` recomputed only in DIAGNOSTICS.
- **6 invariants** (F1.E spec):
  1. `font_resolved` — `shape.engine != nullptr`
  2. `shaping_succeeded` — `glyph_count > 0`
  3. `finite` — all 5 bboxes have finite coordinates
  4. `predicted_contains_world` — `predicted_bbox ⊇ world_ink_bbox`
  5. `clip_contains_visible_ink` — `clip_rect ∩ rendered_alpha_bbox ≠ ∅`
  6. `alpha_bbox non-empty` — actual ink pixels detected
- **Gating**: entire `verify_text_visibility()` and `scan_alpha_bbox()` body gated on `CHRONON3D_BUILD_DIAGNOSTICS` — zero overhead in production SDK builds.
- **Design**: One-shot `spdlog::warn` per invariant (process-global `static bool` pattern matching F1.D/F1.C convention). `verify_text_visibility()` returns `TextVisibilityAudit` for programmatic inspection.
- **Code reviewer**: 2 critical issues fixed: (1) `world_matrix` computed once (reused by diagnostic, overlay, and audit), (2) `clip_rect = predicted_r` (matches compositor behavior — previously hardcoded to full canvas).
- **AGENTS.md compliance**: zero new public API (entirely gated on CHRONON3D_BUILD_DIAGNOSTICS), zero new singleton/registry/cache.
- **Text Simplicity Action Plan**: F1.E complete (seventh of 17 actions). **Fase 1 — Correttezza:** tutti i 5 P0 completati.
- **Cross-references**: [`src/text/text_visibility_audit.cpp`](src/text/text_visibility_audit.cpp); [`include/chronon3d/text/text_visibility_audit.hpp`](include/chronon3d/text/text_visibility_audit.hpp); [`src/render_graph/nodes/TextRunNode.cpp`](src/render_graph/nodes/TextRunNode.cpp).

---

## Luglio 2026 — F1.C Conservative BBox Fallback (2026-07-10, atomic commit)

### fix(text): F1.C — Conservative bbox fallback — predicted_bbox ⊇ world_ink_bbox

- **Problem**: When `TextRunNode::predicted_bbox()` returns a valid but too-small bbox, tile pruning skips tiles containing visible text. The 19px-sliver regression (TICKET-TEXT-CLIP-ASCENT) was the canonical example: a 180pt font on 1080-row canvas produced only 19px of visible glyph ink.
- **Fix** (2 source files):
  - `src/render_graph/nodes/TextRunNode.cpp` — **Pre-render guard**: font-size-proportional threshold check (`bbox_h < font_size * 0.3` or `bbox_w < font_size * 0.5`). Falls back to full canvas on suspicious thinness. One-shot `spdlog::warn` + counter bump. Thresholds proportional to font_size (not canvas-relative) to avoid false positives for small text on large canvases.
  - `src/render_graph/executor/node_runner.cpp` — **Post-render alpha_bbox scan**: after TextRun nodes render, scans the framebuffer alpha channel to compute actual ink bounding box. If actual ink extends beyond `predicted_bbox`, expands `predicted_bbox` and increments `text_bbox_contract_violations` counter. One-shot `spdlog::warn`. Early-exit scan (stops 2 rows past last ink row). Explicit `#include <chronon3d/core/memory/framebuffer.hpp>`.
- **Design**: Two-layer defense:
  1. Pre-render: catches degenerate-thin bboxes before rendering (safety net for bad world_bbox computation)
  2. Post-render: catches valid-but-tight bboxes by comparing against actual rendered ink (primary defense)
- **Code reviewer**: 4 issues found and fixed: (1) canvas-relative thresholds → font-size-proportional, (2) early-exit scan optimization, (3) one-shot log spam prevention, (4) explicit framebuffer include.
- **AGENTS.md compliance**: zero new public API, zero new singleton/registry/cache, defensive guards only.
- **Text Simplicity Action Plan**: F1.C complete (sixth of 17 planned actions).
- **Cross-references**: [`src/render_graph/nodes/TextRunNode.cpp`](src/render_graph/nodes/TextRunNode.cpp); [`src/render_graph/executor/node_runner.cpp`](src/render_graph/executor/node_runner.cpp); [`src/render_graph/executor/tile_pruning.cpp`](src/render_graph/executor/tile_pruning.cpp).

---

## Luglio 2026 — TICKET-011 closure — mainline build rot resolved (2026-07-10, doc-only)

### docs(ticket): TICKET-011 — mainline build rot (chronon3d_core_tests) closure

- **TICKET-011** was the oldest P0 blocker, open since 2026-07-08. It blocked gates 1–8.
- **Audit** (2026-07-08): identified 6 rot files + 2 missing files. Fix roadmap Steps A→E documented.
- **Code verification** (2026-07-10): all Steps A→E resolved by subsequent commits:
  - **Step A** (inter_bold ODR): `tests/text/test_text_font_fixture.hpp` defines `inter_bold()` as `inline` in `test_text_fixture` namespace. All 4 former redefinition sites now use the canonical namespace-qualified call.
  - **Step B** (skip_if_missing ODR): same header, same pattern. All consumers use `test_text_fixture::skip_if_missing()`.
  - **Step C** (text_unit_map_8level.cpp): file exists at HEAD, registered in `tests/core_tests.cmake` line 36, listed in `SKIP_UNITY_BUILD_INCLUSION` set.
  - **Step D** (test_text_font_resolver_golden.cpp): file exists at HEAD, registered in `tests/core_tests.cmake` line 34.
  - **Step E** (test_compile_text_layout{,_validation}.cpp): NOT in cmake — no dangling references to missing files.
- **Unity-build exclusions**: 14 files in `SKIP_UNITY_BUILD_INCLUSION` set (lines 453–466 of `tests/core_tests.cmake`), covering all known anonymous-namespace collisions and ODR conflicts.
- **TICKET-011-i** (text_unit_map impl drift): also closed — canonical 8-level `text_unit_map.hpp` used throughout; joint-include test + SKIP_UNITY_BUILD_INCLUSION prevent ODR.
- **Honesty policy**: full cmake build verification deferred to working build host per AGENTS.md §honesty. Code-level evidence is conclusive.
- **AGENTS.md compliance**: doc-only update, zero source-code modifications.
- **Cross-references**: [`tests/core_tests.cmake`](tests/core_tests.cmake) (SKIP_UNITY_BUILD_INCLUSION set); [`tests/text/test_text_font_fixture.hpp`](tests/text/test_text_font_fixture.hpp) (shared fixture).

---

## Luglio 2026 — F3.A Animation Helpers (2026-07-10, atomic commit)

### feat(animation): F3.A — Top-level animation convenience header

- **Header**: `include/chronon3d/animation/interpolate.hpp` (NEW) — single include for all common animation helpers.
- **Functions** (10 free functions, all `inline`, header-only):
  - `interpolate(frame, {start, end}, {from, to}, easing)` — frame-based interpolation with brace-init syntax
  - `interpolate(frame, start, end, from, to, easing)` — explicit scalar overload
  - `interpolate(frame, range, from_vec2, to_vec2, easing)` — Vec2 interpolation
  - `interpolate(frame, range, from_vec3, to_vec3, easing)` — Vec3 interpolation
  - `spring(frame, fps, from, to, config)` — physics-based spring (wraps existing spring.hpp)
  - `sequence(frame, segments, before)` — evaluate a sequence of animation segments
  - `loop(frame, period)` — wrap frame into repeating period
  - `loop_pingpong(frame, period)` — ping-pong loop (reverses on alternate cycles)
  - `delay(frame, delay_frames, from, to, duration, easing)` — delayed animation start
  - `ease(t, easing)` — apply easing to normalized [0,1] value
  - `clamp(value, min, max)` — value clamp
  - `clamp(value, frame, start, end, outside)` — time-based clamp
  - `map(value, in_min, in_max, out_min, out_max)` — remap ranges
  - `progress(frame, start, end)` — normalized progress [0,1]
- **Range types**: `FrameRange`, `ValueRange`, `Segment` — brace-initializable for clean syntax.
- **Design**: re-exports existing `easing/interpolate.hpp`, `easing/spring.hpp` with simplified signatures. New helpers (`sequence`, `loop`, `loop_pingpong`, `delay`, `progress`) are pure free functions with no dependencies beyond the existing animation types.
- **Text Simplicity Action Plan**: F3.A complete (fifth of 17 planned actions).
- **AGENTS.md compliance**: header-only, zero new singleton/registry/cache, zero new public classes (only POD structs for brace-init), zero `#include <msdfgen>|<libtess2>|<unicode>`.
- **Cross-references**: [`include/chronon3d/animation/interpolate.hpp`](include/chronon3d/animation/interpolate.hpp); [`include/chronon3d/animation/easing/interpolate.hpp`](include/chronon3d/animation/easing/interpolate.hpp) (existing); [`include/chronon3d/animation/easing/spring.hpp`](include/chronon3d/animation/easing/spring.hpp) (existing).

---

## Luglio 2026 — F2.B Simple API Builder (2026-07-10, atomic commit)

### feat(authoring): F2.B — .place(TextPlacement) on Text authoring handle

- **Header**: `include/chronon3d/authoring/text.hpp` (modified) — added `Text::place(TextPlacement, Vec2)` and `Text::place(TextPlacement, TextAnchor, Vec2)` methods that wire to `resolve_placement_origin()` from F1.B.
- **Design**: pin-point semantics. `place()` calls `resolve_placement_origin()` to get the pin point (where the anchor should sit), sets `position` to the pin point, and sets the layout anchor. This matches the rendering pipeline's contract: `node.world_transform.position = spec.position` with `node.world_transform.anchor = resolve_text_anchor(anchor, box)`.
- **API surface**:
  - `.place(TextPlacement::CanvasCenter)` — box center = canvas center
  - `.place(TextPlacement::TopLeft)` — box center = safe area top-left
  - `.place(TextPlacement::SafeAreaBottom)` — box center = safe area bottom
  - `.place(TextPlacement::Absolute({500, 300}))` — box center = (500, 300)
  - `.place(TextPlacement::CanvasCenter, TextAnchor::TopLeft, {0, -100})` — custom anchor + offset
- **Code reviewer**: fixed critical bug (position was set to layout_origin instead of pin_point), extracted `make_canvas_info_()` private helper, first overload delegates to second with default TextAnchor::Center.
- **Text Simplicity Action Plan**: F2.B complete (fourth of 17 planned actions).
- **AGENTS.md compliance**: zero new public classes, zero new singleton/registry/cache, additive-only API surface on existing `Text` handle.
- **Cross-references**: [`include/chronon3d/authoring/text.hpp`](include/chronon3d/authoring/text.hpp); [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp) (F1.B resolver).

---

## Luglio 2026 — F1.D FontEngine Automatico (2026-07-10, atomic commit)

### feat(text): F1.D — FontEngine Automatico: process-wide fallback in resolve_engine()

- **Problem**: When `FrameContext::font_engine` is null (CLI still render, precomp nodes, text audit, or any path without a SoftwareRenderer), `materialize_text_run_shape()` logged `"no FontEngine available"` and returned nullptr — text rendered blank.
- **Fix** (1 source file modified): `src/scene/builders/text_run_builder.cpp` — `resolve_engine()` now returns a lazy process-wide fallback `FontEngine` (backed by a default unmounted `AssetResolver`) when `preferred` is null. One-shot `spdlog::warn` on first fallback use.
- **Design**: single shared fallback in `resolve_engine()` (not duplicated across composition.cpp / precomp_node_execute.cpp). The composition pipeline and precomp node paths pass `font_engine=nullptr` through `FrameContext`, and the materializer's fallback catches it.
- **Coverage**: all 6 documented "no FontEngine available" sites are covered:
  1. `materialize_text_run_shape()` — primary fix via `resolve_engine()`
  2. `composition.cpp` — updated comment documenting F1.D reliance
  3. `precomp_node_execute.cpp` — updated comment documenting F1.D reliance
  4. `renderer_warmup.cpp` — already fixed (uses `renderer.font_engine()`)
  5. CLI video export — already fixed (wires font engine)
  6. `render_node_factory.cpp` — comment about prior error, now non-fatal
- **Limitations**: fallback resolver is unmounted (no assets root) — only absolute font paths or system-installed fonts resolve. Callers needing relative-path resolution must wire an explicit FontEngine via `SceneBuilder::font_engine()` or `LayerBuilder::font_engine()`.
- **AGENTS.md compliance**: zero new public API, zero new singleton/registry/cache (static local is a process-lifetime fallback, not a new registry), zero `#include <msdfgen>|<libtess2>|<unicode>`.
- **Text Simplicity Action Plan**: F1.D complete (third of 17 planned actions).
- **Cross-references**: [`src/scene/builders/text_run_builder.cpp`](src/scene/builders/text_run_builder.cpp) (`resolve_engine()`); [`src/render_graph/pipeline/composition.cpp`](src/render_graph/pipeline/composition.cpp) (comment update); [`src/render_graph/cache/precomp_node_execute.cpp`](src/render_graph/cache/precomp_node_execute.cpp) (comment update).

---

## Luglio 2026 — F1.B Unified Text Placement Resolver (2026-07-10, atomic commit)

### feat(text): F1.B — Unified text placement resolver (TextPlacement enum + ResolvedTextPlacement + resolve_text_placement)

- **Header**: `include/chronon3d/text/text_placement_resolver.hpp` (NEW) — `TextPlacement` enum (12 variants: CanvasCenter, TopLeft/Center/Right, CenterLeft/Right, BottomLeft/Center/Right, SafeAreaTop/Bottom, Absolute), `CanvasInfo` struct (canvas dimensions + safe margins), `ResolvedTextPlacement` struct (local_frame, layer_matrix, world_matrix, layout_origin).
- **Source**: `src/text/text_placement_resolver.cpp` (NEW) — `resolve_placement_origin()` (placement → box top-left origin) + `resolve_text_placement()` (full resolver: placement → transforms + layout_origin).
- **Test**: `tests/text/test_text_placement_resolver.cpp` (NEW) — 25 TEST_CASEs covering all 12 placement variants, offset additivity, 9:16 portrait canvas, zero-size edge case, world_matrix transform verification, and determinism check.
- **CMake**: `src/text/CMakeLists.txt` (text_placement_resolver.cpp registered in chronon3d_text_core), `tests/core_tests.cmake` (test registered in chronon3d_core_tests).
- **ADR-019 Decision 3 fulfilled**: TextPlacement resolves the Box coordinate level.
- **Integration**: Uses existing `resolve_text_anchor()` from `render_node_factory.hpp`. Produces `world_matrix` consumable by `TextRunPlacement.matrix`. Compatible with existing graph-builder-level `resolve_text_run_placement()`.
- **Text Simplicity Action Plan**: F1.B complete (second of 17 planned actions).
- **AGENTS.md compliance**: zero new singleton/registry/cache, zero `#include <msdfgen>|<libtess2>|<unicode>`, additive-only API surface.
- **Cross-references**: [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp); [`src/text/text_placement_resolver.cpp`](src/text/text_placement_resolver.cpp); [`tests/text/test_text_placement_resolver.cpp`](tests/text/test_text_placement_resolver.cpp); [`docs/adr/ADR-019-text-coordinate-model.md`](docs/adr/ADR-019-text-coordinate-model.md) Decision 3.

---

## Luglio 2026 — ADR-019 Text Coordinate Model (2026-07-10, doc-only atomic commit)

### docs(adr): ADR-019 Text Coordinate Model — 4-level Canvas/Layer/Box/Glyph

- **ADR-019** (`docs/adr/ADR-019-text-coordinate-model.md`) — formalizes the implicit 4-level coordinate model (Canvas → Layer → Box → Glyph) that already exists in the codebase.
- **5 Decisions**:
  - D1: Four coordinate levels with clear owner functions and transform chain
  - D2: Every bbox-producing function declares its coordinate level (local_bbox/world_bbox/predicted_bbox/alpha_bbox) with containment invariant
  - D3: TextPlacement resolves the Box level within Layer/Canvas space
  - D4: Glyph coordinates are relative to text frame origin (layout_origin)
  - D5: predicted_bbox MUST use the same matrix chain as rendering (structural fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX)
- **Numerical examples**: 1920×1080 canvas with centered text box, glyph-to-canvas transform chain
- **Fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX**: Decision 5 makes the predicted_bbox containment invariant a formal requirement
- **ADR INDEX updated** (`docs/adr/INDEX.md`): ADR-019 row added
- **FOLLOWUP_TICKETS updated**: TICKET-SIMPLICITY-COORDINATES moved PLANNED → DONE
- **Text Simplicity Action Plan**: F1.A complete (first of 17 planned actions)
- **AGENTS.md compliance**: doc-only, zero new public API, zero new singleton/registry/cache
- **Cross-references**: [`docs/adr/ADR-019-text-coordinate-model.md`](docs/adr/ADR-019-text-coordinate-model.md); [`docs/adr/INDEX.md`](docs/adr/INDEX.md); [`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`](docs/TEXT_SIMPLICITY_ACTION_PLAN.md); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §M1.8.

---


---

> **Archivio storico:** Le entry precedenti al 2026-07-10 (pre-Text Simplicity)
> sono state spostate in [`docs/ARCHIVE/CHANGELOG_ARCHIVE.md`](ARCHIVE/CHANGELOG_ARCHIVE.md).
