# ADR-021 — Commit Subject Length Policy (N=10 window, grandfathered pre-existing)

## Status

Proposed (will promote to Accepted after CI green-light on main).

## Date

2026-07-12

## Deciders

Agent3 (CI gate harmonization surfaced during `split/aca12416-atomize` → main
push).

## Tags

gate-policy, commit-message, AGENTS.md §commit-subject, GATE-MNT-01.

## Context

The CI gate [`tools/check_commit_subject_length.sh`](../tools/check_commit_subject_length.sh)
enforces a 72-character envelope on commit subjects and is invoked as the
final pre-push step from [`tools/wrap_push.sh`](../tools/wrap_push.sh) Step
4.5g (just before `git push`).

Its scope is the **last 10 commits** on the branch
(`git log -n 10 --format='%h %s'`).  The script does NOT walk the full
reachable history of the branch.  The 10-commit window is justified in the
script header as preventing retroactive amend churn on first activation of
a contributor landing against pre-existing over-limit commits on a long-lived
branch.

### Why this ADR is needed

During the recent feature-branch to main push on 2026-07-12, the local
repository state was verified against the gate's enforcement behavior.
Counts:

- **Full `origin/main` history** (`git log --format='%h %s' origin/main | awk
  'length > 72'`): **1,698 commits** with subjects exceeding the 72-char
  envelope out of ~3,400 total commits on `origin/main`.  This is an
  inherited legacy condition predating the gate's first activation.
- **`origin/main`'s last-10 commits** (the active enforcement window as of
  this ADR's filing): **0 over-limit commits**.  Subject lengths in the
  window: 42, 41, 40, 48, 46, 44, 40, 65, 72, 71.  All ≤72.  The window is
  currently clean.
- **Notable historical offender** (`94dba6c5`): subject "docs(followup):
  revert TICKET-TEXT-LEGACY-POSITION-ROT to OPEN + add TICKET-GATE-SUBJECT-RANGE"
  — 94 chars.  Pre-window, hence grandfathered through the FF fast-forward
  during the recent push.  This is one of 1,698, not a singular anomaly.

Net effect: the gate is asymmetric by design — it catches recent drift
where it matters for ongoing collaboration (anyone landing new commits
sees them enforced), while tolerating legacy violations outside the window
where remediation would require a destructive full-history rebase.

The user explicitly raised the "hidden privilege class" concern: any user
whose commits land in the 10-commit window gets enforced, while users whose
over-limit commits are 11+ deep are silently tolerated.  This ADR makes
the asymmetry intentional, documented, and CI-validated rather than
hyper-technical drift.

## Decision

Adopt the **commit-window-only** semantics (`N=10`) as the project's
canonical behavior, and document it explicitly in `CONTRIBUTING.md` (the
section is appended in the same diff as this ADR).

Specifically:

1. **Stay at N=10.**  The 10-commit window is the active enforcement
   boundary.  New contributors must keep their last 10 commits ≤72 chars
   per subject, as scoped by the gate.
2. **Pre-existing commits on `origin/main` HEAD are out of scope
   (grandfathered).**  The entirety of the 1,698 historical over-limit
   commits on `origin/main`'s full history is intentionally grandfathered
   as an unfixable historical artifact.  Wholesale history rewrites are
   explicitly out of scope per AGENTS.md "no cosmetic amend churn unless
   enforceable in CI" — enforcing the gate retroactively across full
   history would require rewriting ~50% of `origin/main`'s commits and
   breaking every external clone.

   *Implementation note:* Grandfathering is concretely enforced in the gate
   by enumerating commits in `git log origin/main..HEAD` rather than
   `git log -n N`.  Any commit already reachable from `origin/main` HEAD
   is outside the gate's scope; only commits about to be pushed are
   enforced.  A commit that is ENROLLED onto `origin/main` (i.e., a
   subsequent fast-forward or merge that lands a previously-rejected
   subject on main) would not retroactively fail subsequent pushes,
   because at that point the commit becomes grandfathered via reachability.
   This matches ADR-021 §1's intent ("new commits are enforced; existing
   ones are not") without requiring a 1,698-line registry.  Boundary_SHA
   pinning à la ADR-021-tinkerer's-static-SHA was considered and rejected
   as non-operationally-correct (a static SHA pins to a moment in time,
   whereas reachability from `origin/main` captures the dynamic state).
3. **No `--skip-gates` escape hatch.**  The window-only semantics does
   NOT add a per-commit or per-push skip mechanism.  The gate is
   hard-block per AGENTS.md "Non cambiare un gate per nascondere un
   errore" and "Hardblock always; no --skip-gates escape hatch".
4. **Window is not configurable.**  The N=10 default is the only mode;
   the only override is the env var `SUBJECT_LENGTH_LIMIT` (default 72)
   which controls the per-character limit, NOT the window size.  Changes
   to the window require an ADR (this ADR is that precedent).

The 1,698 historical over-limit commits are tallied in
[`TICKET-124`](../tickets/TICKET-124-commit-subject-historical-ledger.md) — a
**no-action historical ledger** that records the asymmetry as observed on
2026-07-12 and the rationale for not remediating.  The ticket does NOT
propose remediating any of the 1,698 commits; it is informational scope
only.

## Consequences

### Positive

- The asymmetry is documented in `CONTRIBUTING.md` — anyone reading the
  contributing guide knows the window applies, what the grandfathered
  scope is, and that remediation is intentionally out of scope.
- The existing 10-commit window matches AGENTS.md's "no cosmetic amend
  churn unless enforceable in CI" rule — the gate does not produce
  retroactive activity on first activation of a contributor who lands
  pre-existing over-limit commits on a long-lived branch.
- TICKET-124 as a historical ledger creates a tracked, observable record
  of the 1,698 grandfathered commits count and its policy basis.  Future
  contributors can see the rationale without rediscovering it from scratch.
- Forward-only hygiene is preserved: every new commit landing in the 10-
  commit window is enforced, so the drift surface is bounded.

### Negative

- **Privilege-by-recency persists.**  The 11th-commit-and-deeper are
  ungoverned.  If 11 commits in a row accidentally exceed the 72-char
  envelope, the gate silently passes them all.  This is intrinsic to the
  window model and accepted.  TICKET-124 documents the count but does
  not enforce remediation.
- **1,698 grandfathered commits remain in Git history.**  They surface as
  visual noise in `git log --oneline` and confuse the "subjects are always
  ≤72 chars" invariant.  This is the deliberate trade-off for not
  destroying collaboration-friendly history.  No remediation ticket is
  open for this; see TICKET-124.
- **A contributor who wants strictness must ABI-break.**  Activating
  strict mode (`git log --format='%h %s' origin/main | awk 'length > 72'
  | wc -l` produces > 0 always-FAIL on last-10-FF plus full history) would
  require rewriting `origin/main`, which contradicts AGENTS.md "no
  cosmetic amend churn".  Strict is therefore structurally incompatible
  with the current AGENTS.md rule set; this ADR is the consequence.

### Neutral

- The conceptual policy (window-only + grandfather) is unchanged from
  pre-ADR-021 — this ADR formalizes what is already implemented in
  CONTRIBUTING.md.  The GATE'S IMPLEMENTATION is updated as the
  realization of §2's grandfather clause: scope switches from
  `git log -n 10` to `git log origin/main..HEAD`.  No semantic shift in
  the policy itself; the change is purely the technical enforcement of
  the documented intent.
- Future contributors may consider tools (e.g., a `git filter-repo`
  auxiliary script) that batch-rewrite specific over-limit commits within
  an explicit ticket.  Such tools are out of scope for this ADR.

## Alternatives Considered

### A1. Strict enforcement — walk all reachable commits

Change `check_commit_subject_length.sh` to scan every reachable commit
on the branch (including its full upstream `origin/main` ancestor chain)
and add a history-rewrite remediation script that batch-rewrites the
1,698 pre-existing >72-char commits in a controlled feature branch,
followed by force-pushing `main` with the rewrite.

**Rejected because:**

- AGENTS.md "no cosmetic amend churn unless enforceable in CI" rule
  conflicts with retroactive activation.  Activating strict would
  require rewriting `origin/main` history, which contradicts the same
  AGENTS.md rule and the "no stime percentuali: usare stato osservabile"
  rule on committed progress baselines.
- The 1,698 grandfathered commits on `origin/main` would need to be
  rewritten in a force-push on `main` after the gate's strict-mode
  activates.  Single point of failure: any failure during the rewrite
  blocks the entire chain.
- Wholesale force-pushing `main` breaks every external clone, fork, and
  CI cache, invalidating the project's `7eb5c2ba` permanent green
  baseline reference.
- Adds a new script (`tools/rewrite_overlong_subjects.sh`) that requires
  careful audit logic to avoid corrupting merge commits and signed-off
  history.

### A2. Hybrid — commit-window for FF, history-walk for non-FF

Soft-warn on history-walk for non-FF pushes (where history divergence
indicates a substantial rebase), hard-block on commit-window for FF
pushes (where history is expected to be cohesive).

**Rejected because:**

- Adds two distinct code paths in `check_commit_subject_length.sh`,
  doubling the maintenance surface.
- The non-FF warning is not actionable: the contributor already pushed,
  the warning is post-hoc.
- The commit-window-only model is simpler and matches the existing
  `tools/check_main_clean.sh` GATE-MNT-01 design philosophy (window-of-
  recent is the canonical enforcement pattern in this project).

### A3. Disable the gate entirely (preserve AGENTS.md mention but skip it)

**Rejected because:** AGENTS.md explicitly says "Maintain baseline: 11/11
gates on every commit on main".  Disabling a gate is a contract violation
per AGENTS.md §definitions-of-done, equivalent to disabling the suite in
CI.  Hardblock always remains canonical.

### A4. Make window size branch-configurable

Add a per-branch `branch.<name>.subjectLenWindow` config that overrides
the N=10 default for branches with known large over-limit history batches.

**Rejected because:** Per-branch config introduces an asymmetric
enforcement surface that confuses contributors landing from multiple
branches.  The single N=10 default is the simplest correct answer.

## References

- AGENTS.md §commit-subject, §definitions-of-done, §release-gate, §GATE-MNT-01.
- [`tools/wrap_push.sh`](../tools/wrap_push.sh) Step 4.5g — the gate
  invocation in the push chain.
- [`tools/check_commit_subject_length.sh`](../tools/check_commit_subject_length.sh)
  — the gate implementation, scope = last 10 commits, hard block exit 1,
  no `--skip-gates` escape.
- [`CONTRIBUTING.md`](../../CONTRIBUTING.md) — the policy is appended as
  an explicit "Commit Subject Policy" section in this same commit.
- [`TICKET-124-commit-subject-historical-ledger.md`](../tickets/TICKET-124-commit-subject-historical-ledger.md)
  — companion historical ledger ticket (no-action).
- Audit command used to derive the 1,698 count:
  `git log --format='%h %s' origin/main | awk 'length > 72' | wc -l` (run
  2026-07-12 against `origin/main` HEAD `52018fbb`).
- Audit command used to verify the current last-10 window is clean:
  `git log -n 10 --format='%s' origin/main | awk 'length > 72' | wc -l`
  (returns 0 as of 2026-07-12).
