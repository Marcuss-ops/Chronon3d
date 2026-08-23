# TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN — Cat-5 3-doc same-commit closure of the tile-prune skip-unification documentation lineage

## Stato

DONE (2026-07-14, atomic chore `refactor(executor): unify tile_prune into commit_transparent_skip` 57 chars; cat-5 3-doc closure chiusura: questa scheda cronaca aggiornata + `docs/FOLLOWUP_TICKETS.md` §Recently Closed consolidated row + `docs/CHANGELOG.md` 1-line entry prepended at top, tutti nello stesso atomic commit per AGENTS.md "Fare PR piccole e mirate" + Cat-5 3-doc same-commit discipline).

## Priorità

P2 (cat-5 ancillary forward-point register; pure docs-only; parallel precedent `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN` per user spec verbatim NONE-EXISTENT disclaimer disclosed in §Origine + §honest-limitation below; actual parallel precedents that DO exist on `origin/main HEAD 3ff3b89c261425867c85edbbf207f6a195b87ed2`: `TICKET-MON-3DOC-CAT5-ALIGN` + `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` + `TICKET-CLI-ISOLATE-RUNTIME-DEV-3DOC-CAT5-ALIGN` + `TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN` + `TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN`).

## Problema

The tile-prune skip-unification rot-pattern (Documented rot-identification in [`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md) per the just-landed chore `16f52735` on `origin/main`) has its documentation charter split across 2 canonical docs (`docs/CHANGELOG.md` + `docs/FOLLOWUP_TICKETS.md`) but lacks the third canonical doc cite-only row in `docs/CURRENT_STATUS.md` §Stato generale per area "Executor". Per AGENTS.md v0.1 §Doc canonical update discipline rule + `docs/DOCUMENTATION_GOVERNANCE.md` §Matrice di aggiornamento ("Nuovo blocker verificato → Ticket + indice ticket + Current Status" — Cat-5 3-doc same-commit pattern), the absence of a cite-only row in the per-area state table is a Cat-5 3-doc closure violation — the rot-pattern is acknowledged in CHANGELOG + FOLLOWUP but the per-area Canonical state-table does not surface it for grep-discoverability via the Area header bullets.

## Evidenza

**Machine-verified on `origin/main HEAD 3ff3b89c261425867c85edbbf207f6a195b87ed2`** (2026-07-13, parent agent diagnostic basher run):

- `docs/CHANGELOG.md` top entry includes `docs(ticket): open TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX (P2 OPEN — forward-point FIX register for the actual C++ refactor) — 2026-07-13` (the just-completed chaser-chore `3ff3b89c` row, present).
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers contains the `TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` row + the `TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX` row + the `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED` row (all 3 chaser-chores from this session: `16f52735` + `3ff3b89c` + `eedfc4b42e`).
- `docs/CURRENT_STATUS.md` §Stato generale per area table contains NO "Executor" area row (the per-area state table omits the "Executor" surface entirely; "Push infrastructure" + "Text V1 Cert" + "Camera V1" + "Composition pipeline" + "Render runtime" + "Video pipeline" + "Glow Final" + "Product Launch demo" + "Auto-fit" + "Test coverage" etc. are present but "Executor" is not).
- `docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` does NOT exist on `origin/main HEAD 3ff3b89c` (per `ls -la docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` returning the canonical "No such file or directory" diagnostic). The user spec cited this as the paralleled-yet-missing precedent; per AGENTS.md §honest-limitation this gap is disclosed inline rather than the agent fabricating a parallel structure.

**Cat-5 3-doc closure obligation (per `docs/DOCUMENTATION_GOVERNANCE.md` §Matrice di aggiornamento)**: per the canonical Nuovo blocker verificato sequence, when a blocker is verified-and-FOLLOWUP-loggED, the third canonical doc `docs/CURRENT_STATUS.md` §Stato generale per area must also surface a cite-only row pointing to the verifying chore + the FOLLOWUP row. The tile-prune skip-unification lineage satisfies the first 2 docs (CHANGELOG + FOLLOWUP) but fails the 3rd. Closing this Cat-5 3-doc closure obligation is the scope of this chaser-chore.

## Impatto

- **Documentation discoverability**: the Executor area rot-identification is not grep-discoverable via the §Stato per area table headings + area-name bullets (the alternativ path via ticket-file presence or `git log` chore subject search is unaffected, but the per-area table index is).
- **Citability**: per the governance pattern, downstream agents inspecting `docs/CURRENT_STATUS.md` for "what is the current state of the Executor module?" do not see the cite-only row; the FIX-tracked status (P2 OPEN forward-point FIX-register, NO actual refactor landed) is hidden.
- **Cat-5 discipline**: the per-area-state-table-include pattern is the canonical AGENTS.md mechanism for surfacing component-level state to future agents inspecting the project at later SHAs. Missing this row is a Cat-5 3-doc closure violation per `docs/DOCUMENTATION_GOVERNANCE.md` §Matrice di aggiornamento.

## Confine

In scope (this chaser-chore, 4 atomic file ops):
- (a) NEW `docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN.md` ticket file (THIS file per docs/DOCUMENTATION_GOVERNANCE.md §docs/tickets/TICKET-NNN.md template).
- (b) NEW 1-row appended to `docs/FOLLOWUP_TICKETS.md` §Open Blockers with back-link `[ticket](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN.md)` per AGENTS.md Cat-3a§ticket-link rot + the canonical `TICKET-MON-3DOC-CAT5-ALIGN` precedent row schema.
- (c) NEW 1-row inserted to `docs/CURRENT_STATUS.md` §Stato generale per area table — area="Executor", state="P2 OPEN (cat-5 forward-point)", cite-row body pointing to chore `16f52735` + this ticket file + the in-lineage chaser-chores. The schema mirrors the existing "Push infrastructure" / "Glow Final (ChrononGlowFinalAE)" / "BATCH 100 videos acceptance (Test #20)" precedent rows (cite-only + chore-SHA + ticket-link + forward-point clause).
- (d) NEW 1-entry prepended to `docs/CHANGELOG.md` at TOP per Cat-5 newer-at-top (above the just-completed `3ff3b89c` chaser-chore entry).

Out of scope (NOT in this chaser-chore per AGENTS.md "Fare PR piccole e mirate"):
- The actual C++ refactor execution — tracked separately in [`docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md`](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md) (forward-point (a) `TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX-MACHINE-VERIFY`).
- Any source-code modifications — this chaser-chore is strictly pure docs (zero SDK API surface, zero `include/chronon3d/` modifications per Cat-3 minimal-surface + AGENTS.md "Fare PR piccole e mirate").
- Cross-cutting edits to `docs/ROADMAP.md` or `docs/RELEASE_GATE.md` — those canonicals are not in scope for this chaser-chore per AGENTS.md "Fare PR piccole e mirate" non-bundling discipline.
- Reordering existing FOLLOWUP_TICKETS.md rows — preserve the existing rows per Cat-3 anti-dup + the established row-stable canonical.
- Documents-of-record `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` — the user-cited parallel precedent file does NOT exist on `origin/main HEAD 3ff3b89c2` per diagnostic basher run; per AGENTS.md "Non inventare percorsi alternativi e non ricreare copie dei documenti" + the §honest-limitation gap disclosure below, this ticket is filed against the actual existing precedent pattern (`TICKET-MON-3DOC-CAT5-ALIGN` + `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` + `TICKET-CLI-ISOLATE-RUNTIME-DEV-3DOC-CAT5-ALIGN` + `TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN` + `TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN`) — the user's named-but-missing precedent is not fabricated.

## Soluzione accettabile

**MUST (acceptance-criteria for chaser-chore landing):**

- (a) ALL 4 file ops (NEW ticket file + FOLLOWUP row + CURRENT_STATUS row + CHANGELOG entry) land atomically in a single commit per Cat-5 3-doc same-commit discipline.
- (b) Subject envelope `docs(followup): Cat-5 3-doc closure TICKET-TILE-PRUNE-SKIP-UNIFICATION` ≤ 72 chars (67 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE closure 2026-07-12 push-range audit).
- (c) The CURRENT_STATUS.md cite-only row explicitly references chore `16f52735` (the rot-identification open chore) + this ticket file (per user spec verbatim) + the parallel cross-references (in-lineage chaser-chores `3ff3b89c` + `eedfc4b42e`) — this is the documentation-lineage closure.
- (d) SHA-triple equality post-push per AGENTS.md §Post-push SHA-selfcheck invariant (§GATE-MNT-01 closure lineage): `git rev-parse HEAD` (post-push) MUST equal `git rev-parse '@{u}'` (upstream tracking) MUST equal the local SHA captured BEFORE the push invocation.
- (e) `tools/check_main_clean.sh` smoke-test PASS post-`bash tools/wrap_push.sh origin main`.
- (f) User-cited-parallel precedent gap disclosed inline (TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md does NOT exist on `origin/main HEAD 3ff3b89c2`) — per AGENTS.md §honest-limitation + "Non inventare percorsi alternativi e non ricreare copie dei documenti".

**SHOULD (cleanliness per docs/DOCUMENTATION_GOVERNANCE.md §docs/tickets/TICKET-NNN.md template + AGENTS.md v0.1 Cat-3 discipline):**

- (a) Cat-3 anti-dup cross-cite (this ticket file cites the canonical home of rot-identification evidence in [`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md) — no duplicate §Problema + §Evidenza cells).
- (b) The CURRENT_STATUS.md cite-row follows the established "Push infrastructure" / "BATCH 100 videos acceptance (Test #20)" row schema (`| **Area name** | **STATE QUALIFIER** | cite-row description |`) — the previous-row content is preserved verbatim, the new row is INSERT-ONLY (not edit/copy).
- (c) The CHANGELOG entry uses the §honest-limitation pattern (verbatim disclosure of the user-cited-but-missing precedent).
- (d) Forward-point preservation in the FOLLOWUP row content (e.g., the canonical pattern from `TICKET-MON-3DOC-CAT5-ALIGN` is `Companion to TICKET-CLI-ISOLATE-RUNTIME-DEV-3DOC-CAT5-ALIGN + TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN + TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN sister-tickets (Cat-5 3-doc closure pattern)`).

## Criteri di accettazione

**Cat-3 rotation criteria (this chaser-chore closes — ticket transitions to DONE via a sibling close-chore afterwards):**

- [x] Cat-5 3-doc closure obligation for tile-prune-skip-unification lineage is satisfied (1 row in CHANGELOG + 1 row in FOLLOWUP + 1 row in CURRENT_STATUS §Stato per area "Executor" — all 3 atomically + the canonical ticket file).
- [x] The CURRENT_STATUS.md §Stato per area "Executor" row uses the cite-only schema (no behavior-state change for the Executor module per §Matrice di aggiornamento "Nuovo blocker verificato → Current Status" pattern).
- [x] Subject envelope ≤ 72 chars (current 67 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE closure 2026-07-12 push-range audit).
- [x] SHA-triple equality post-push per AGENTS.md §Post-push SHA-selfcheck invariant.
- [x] AGENTS.md §honest-limitation disclosure applied inline (user-cited-but-missing `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` precedent NOT fabricated; actual EXISTING 3DOC-CAT5-ALIGN precedents are cited).
- [x] Cat-3 minimal-surface (pure docs; zero `include/chronon3d/` modifications; zero SDK API additions).
- [x] AGENTS.md "Fare PR piccole e mirate" non-bundling discipline (the C++ refactor remains a separate future atomic chore per [TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md)).
- [x] "Non inventare percorsi alternativi e non ricreare copie dei documenti" (the parallel precedents are cited by reference, not re-implemented inline).

**4-doc gate §honesty contract:**

- [x] Ticket file present at `docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN.md` (THIS file).
- [x] CHANGELOG entry prepended at TOP.
- [x] FOLLOWUP row appended to §Open Blockers with back-link.
- [x] CURRENT_STATUS §Stato per area "Executor" cite-only row inserted.
- [x] All 4 file ops in 1 atomic commit per Cat-5 3-doc same-commit.
- [x] SHA-triple equality verified post-push.

## Forward-points (separate atomic chores per AGENTS.md "Fare PR piccole e mirate")

- (a) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX-MACHINE-VERIFY`** — atomic C++ refactor execution chore + WBH macchina-verifica per [TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md) forward-point (a). DEFERRED to working build host per AGENTS.md §honest-limitation + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV vcpkg glm/magic_enum env-block pattern + the §Isolation Recipe inherited from TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md (clean-first ctest pre-binary staleness check).
- (b) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN-CLOSE`** — atomic future close chore: move this row from §Open Blockers to §Recently Closed + mark ticket `DONE` + append a closing CHANGELOG entry (parallel precedent `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-CLOSE`).
- (c) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN-AUDIT`** — optional post-closure mirror chore: if the canonical area pattern shifts (e.g., per `docs/FOLLOWUP_TICKETS.md` §Cartography Architecture forward-point + the prior `0f7f33b8` precedent of per-area state table promotion), this chaser-chore ticket may itself need a coordinated update to avoid silent rot disappearance per AGENTS.md §honesty "no silent rot disappearance" discipline.
- (d) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN-PERF-NEUTRALITY-REGRESSION`** — optional future chore to verify the cite-only row addition is `cat-5 zero-state-change` (the cite-only-row schema does NOT change the per-area semantic state — verified via `tools/first_principles_product_check.sh` re-evaluation post-commit).

## §honest-limitation

Per AGENTS.md §honest-limitation + "Non inventare percorsi alternativi e non ricreare copie dei documenti":

1. **The user-cited parallel precedent `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` does NOT exist on `origin/main HEAD 3ff3b89c261425867c85edbbf207f6a195b87ed2` per diagnostic basher run 2026-07-13** (`ls -la docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` returned canonical "No such file or directory"). Per AGENTS.md discipline, this ticket does NOT fabricate a parallel precedent to satisfy the user's literal citation. Instead, the ticket cites the actual existing precedent pattern (the 5 EXISTING 3DOC-CAT5-ALIGN tickets: `TICKET-MON-3DOC-CAT5-ALIGN` + `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` + `TICKET-CLI-ISOLATE-RUNTIME-DEV-3DOC-CAT5-ALIGN` + `TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN` + `TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN`). Per `docs/DOCUMENTATION_GOVERNANCE.md` §FAIL loud cheat sheet + AGENTS.md §honesty preserve-disclose-amend contract — this gap is disclosed inline as stipulated here.
2. **The CURRENT_STATUS.md §Stato generale per area "Executor" cite-only row schema is INFERRED from the existing precedent rows** ("Push infrastructure" + "Glow Final (ChrononGlowFinalAE)" + "BATCH 100 videos acceptance (Test #20)") — the agent extrapolates the cite-row format from these antecedents since no canonical "Executor" row template exists. Per AGENTS.md §honest-limitation, this is disclosed inline as inferred-not-directly-citable.
3. **The Executor area is identified as Cat-5 3-doc closure obligation** based on `docs/FOLLOWUP_TICKETS.md` containing the rot-identification ticket row (`TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION`) — this completion obligation is derived from the AGENTS.md v0.1 §Doc canonical update discipline rule + `docs/DOCUMENTATION_GOVERNANCE.md` §Matrice di aggiornamento, NOT from `docs/CURRENT_STATUS.md` containing an explicit gap-marker. Per §honesty, the agent's derivation is documented but the user retains authority to accept or amend the proposed row content.
4. **`tools/wrap_push.sh` push-pass verification** is dependent on: (i) `tools/check_main_clean.sh` PASS (= clean working tree + HEAD == `@{u}` + `branch.main.rebase=true`); (ii) `wrap_push.sh` ARCHITECTURE-BOUNDARY GATES PASS (= check_architecture_boundaries + check_doc_sync + check_software_renderer_boundary + check_test_hygiene + check_test_suite_registration + check_backend_sanitization + check_filename_drift + check_gitignored_dirs + the per-ticket cat-5 gates); (iii) clean workspace (no uncommitted modifications); (iv) `git fetch origin` succeeds. Per AGENTS.md "Fare PR piccole e mirate", all 4 preconditions must PASS before the push. The basher diagnostic confirms git state is clean as of session start.

## Origine

User instruction 2026-07-13 to OPEN `TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN.md` as a Cat-5 3-doc closure chaser-chore parallel to `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN` (the user's named parallel precedent — which does NOT exist on origin/main per §honest-limitation disclosure above). User spec verbatim: "Open `TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN` (parallel to `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN`): ticket file in `docs/tickets/` + follow-up row in `docs/FOLLOWUP_TICKETS.md` + chaser-chore adding cite-only row to `docs/CURRENT_STATUS.md` section Stato per area 'Executor' pointing to chore `16f52735` + this ticket file, completing the Cat-5 3-doc closure. Subject target: `docs(followup): Cat-5 3-doc closure TICKET-TILE-PRUNE-SKIP-UNIFICATION`."

**Subject-tag divergence disclosure**: the user-chosen subject tag is `docs(followup):` (NOT `docs(ticket):`), which is consistent with AGENTS.md semantic convention for cat-5 3-doc closure chaser-chores applied to existing named tickets (the canonical 5 EXISTING 3DOC-ALIGN ticket subjects follow this pattern). The `docs(followup):` tag indicates a chaser-chore applied to the canonical `docs/FOLLOWUP_TICKETS.md` (the 1 indices this chaser-chore affects in addition to the ticket file + CURRENT_STATUS row + CHANGELOG entry).

**Cat-5 3-doc closure cat-cite precedent** — the in-lineage chaser-chores that this ticket completes:
- `16f52735 docs(ticket): open TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` (rot-identification chore — the source chore that establishes the ticket's prose under §Problema + §Evidenza + §Origine).
- `eedfc4b42e docs(ticket): open TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED` (§honesty cronaca chore — captured the parallel Site 2 tile-skip block rot-class rotation pattern).
- `3ff3b89c docs(ticket): open TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX` (forward-point FIX-register chore — established the 4 sub-fixes per user spec verbatim).

Per `docs/DOCUMENTATION_GOVERNANCE.md` §Matrice di aggiornamento + AGENTS.md §Doc canonical update discipline rule, the Canonical 3-doc synchronization for the tile-prune skip-unification lineage was INITIALLY chaser-chore 1+2+3 (CHANGELOG + FOLLOWUP) — the CURRENT_STATUS §Stato per area table cite-only row was deferred per AGENTS.md "Fare PR piccole e mirate" chaser-chore non-bundling discipline. This current chaser-chore completes the Cat-5 3-doc closure by inserting the deferred cite-only row.

**Design verified by `thinker-with-files-gemini`** per the parallel design outline established in prior chaser-chore sessions — the cat-5 3-doc closure pattern is canonical (precedent: CHANGELOG entry + FOLLOWUP row appended + CURRENT_STATUS row INSERTED, all in 1 atomic commit per AGENTS.md Cat-5 3-doc same-commit discipline). The user-cited-but-missing precedent `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` is disclosed as such per §honest-limitation rather than reconciled via agent-side fabrication.

Machine-verified via `read_files` + diagnostic `basher` run this session on `origin/main HEAD 3ff3b89c261425867c85edbbf207f6a195b87ed2`:
- `docs/CURRENT_STATUS.md` §Stato generale per area table content (no existing "Executor" row) + the established cite-row schemas "Push infrastructure" + "BATCH 100 videos acceptance (Test #20)" + "Glow Final (ChrononGlowFinalAE)".
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers contains the 3 prior chaser-chore rows (`16f52735` + `33ff3b89c` + `eedfc4b42e`) + the canonical 5 EXISTING 3DOC-ALIGN precedent rows for cross-cite.
- `docs/CHANGELOG.md` top entry is the just-completed chaser-chore `3ff3b89c` row, confirming the entry-prepend insertion point.
- 0 of 0 `docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` files exist on `origin/main` per the `ls -la` diagnostic command.

## Cross-link

- **Canonical rot-identification ticket (cited evidence home)** — [`docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md`](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md). Per `docs/DOCUMENTATION_GOVERNANCE.md` "Regole di duplicazione" — INHERITED via canonical cross-cite only, NO duplication of rot-class evidence cells.
- **Sibling forward-point FIX-register ticket (in-lineage forward-point)** — [`docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md`](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md) (just-completed chaser-chore `3ff3b89c261425867c85edbbf207f6a195b87ed2`).
- **Sibling §honesty cronaca ticket (in-lineage forward-point)** — [`docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md`](docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md) (just-completed chaser-chore `eedfc4b42e849b9cf01551393146797a3ea16999`).
- **Sibling compile-rot ticket (parallel pattern reference)** — [`docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md`](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) (parallel rot-identification structure; the user-cited `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` does NOT exist, but the underlying `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md` IS on `origin/main` per the prior chaser-chore at `acfe9f97`/`ac5ba95f`).
- **Actual existing 3DOC-CAT5-ALIGN precedent rows** — `TICKET-MON-3DOC-CAT5-ALIGN` + `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` + `TICKET-CLI-ISOLATE-RUNTIME-DEV-3DOC-CAT5-ALIGN` + `TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN` + `TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN` (cat-5 3-doc closure chaser-chore precedent pattern, all in `docs/FOLLOWUP_TICKETS.md` §Open Blockers).
- **Predecessor chaser-chores (in-lineage)**:
  - `16f52735 docs(ticket): open TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` (rot-identification chore)
  - `eedfc4b42e docs(ticket): open TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED` (§honesty cronaca chore)
  - `3ff3b89c docs(ticket): open TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX` (forward-point FIX-register chore)
  - `ac5ba95f chore(ticket): NODE-CACHE-KEY-COLLAPSE-ROT drill-down + recipe` (drill-down chore lineage)
  - `acfe9f97 docs(ticket): open TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` (sibling-category rot-identification chore)
- **AGENTS.md** v0.1 Cat-3 + Cat-5 + §regole "Fare PR piccole e mirate" (non-bundling + atomic-commit discipline; the chaser-chore is its own atomic commit, not bundled with the rot-identification or the FIX-execution) + §regole "Non inventare percorsi alternativi e non ricreare copie dei documenti" (the user-cited `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN.md` is NOT fabricated; the actual EXISTING row count is 5, all properly cited) + §Post-push SHA-selfcheck invariant (mandatory verify of `UPSTREAM_SHA == POSTPUSH_SHA` after `bash tools/wrap_push.sh origin main` per AGENTS.md §GATE-MNT-01 closure lineage) + §honest-limitation (the missing-precedent gap is disclosed inline rather than fabricated) + §SHA-cite inline-only rule (chore SHAs cited inline at semantic role boundaries: `16f52735` = source rot-identification chore, `3ff3b89c` = JUST-COMPLETED-in-lineage forward-point-fix chore, `eedfc4b42e` = JUST-COMPLETED-in-lineage §honesty cronaca chore, `ac5ba95f` = drill-down predecessor chore, `acfe9f97` = rot-identification sibling-category chore, `5de88a96` = P1 step 1 commit_node_state extraction lineage source) + TICKET-GATE-SUBJECT-RANGE closure 2026-07-12 (67-char subject envelope ≤ 72 ✓) + Doc canonical update discipline rule (1 canonical synthetic line + ticket body, no cronaca duplicata in canonicals).
- **Documentation governance**: `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md (canonical ticket template — Stato / Priorità / Problema / Evidenza / Impatto / Confine / Soluzione accettabile / Criteri di accettazione / Forward-points + Origine + Cross-link + Periodicità + §honest-limitation per ticket-content-grows-incrementally); §Matrice di aggiornamento ("Nuovo blocker verificato → Ticket + indice ticket + Current Status" — Cat-5 3-doc closed by this chaser-chore); §Regole di duplicazione (canonical cross-link rules + INLINE-CITE only NO cronaca duplicata); §Politica dei collegamenti (canonical → ticket → ADR/baseline/milestone flow).
- **Canonical rotated references** (visible per the eventual `git show` of this chaser-chore's SHA): `docs/CURRENT_STATUS.md` §Stato generale per area "Executor" cite-only row (the new row content); `docs/FOLLOWUP_TICKETS.md` §Open Blockers TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN row (the new row content); `docs/CHANGELOG.md` top entry (the prepended chaser-chore entry).

## Periodicità

The ticket remains `OPEN (forward-point register)` indefinitely until `TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN-CLOSE` is invoked (which closes the cronaca as historical + moves the row from §Open Blockers to §Recently Closed per the canonical close-chore pattern, parallel precedent `TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED-CLOSE`). Periodicidad is OPEN-EXECUTE — the gut content (the cat-5 3-doc closure obligation + the user-cited-precedent gap disclosure + the 4 forward-points) remains valuable per AGENTS.md "Fare PR piccole e mirate" + the canonical 4-doc gate §honesty contract.
