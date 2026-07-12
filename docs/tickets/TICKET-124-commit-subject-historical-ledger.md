# TICKET-124 — Historical ledger: 1,698 grandfathered over-72-char subjects on origin/main

## Status

**No-action / historical ledger**. This ticket is **informational only** and
does not propose remediation of any of the 1,698 over-limit commits.

## Blockers

None (independent of any active feature work).

## Background

On 2026-07-12, immediately prior to the
`split/aca12416-atomize` → `main` push landing HEAD `52018fbb`, an audit
of `origin/main`'s commit history revealed that the project has accumulated
**1,698 commits** with subjects exceeding the 72-character envelope
enforced by [`tools/check_commit_subject_length.sh`](../tools/check_commit_subject_length.sh).

The gate's enforcement scope is the **last 10 commits** on a branch (per
the script header and the canonical enforced behavior documented in
[`ADR-021`](../adr/ADR-021-commit-subject-length-policy.md)).  None of the
1,698 grandfathered commits are within the active enforcement window
of the current `origin/main` HEAD; they were inherited from a pre-gate
project history (the gate's `wc -l` count was 0 on the last-10 window
of `origin/main` HEAD `52018fbb` as of 2026-07-12).

## Observed state (audit snapshot 2026-07-12)

| Measurement | Value | Source |
|-------------|-------|--------|
| Total commits on `origin/main` | ~3,400 | `git rev-list --count origin/main` |
| Over-72-char commits (full history) | **1,698** | `git log --format='%h %s' origin/main \| awk 'length > 72' \| wc -l` |
| Over-72-char commits (last-10 window) | **0** | `git log -n 10 --format='%s' origin/main \| awk 'length > 72' \| wc -l` |
| Subjects in last-10 window | 42, 41, 40, 48, 46, 44, 40, 65, 72, 71 chars (all ≤72) | `git log -n 10 --format='%s' origin/main` |
| Sample notable offender | `94dba6c5` (94-char subject: "docs(followup): revert TICKET-TEXT-LEGACY-POSITION-ROT to OPEN + add TICKET-GATE-SUBJECT-RANGE") | `git log --format='%h %s' -1 94dba6c5` |

## Why this ticket is no-action (rationale for not remediating)

ADR-021's Decision §2 formalizes the project's position: "The entirety of
the 1,698 historical over-limit commits on `origin/main` is intentionally
grandfathered as an unfixable historical artifact."  The reasoning:

1. **AGENTS.md "no cosmetic amend churn unless enforceable in CI".**
   Retroactively rewriting 1,698 commits across `origin/main` would be
   wholesale cosmetic amend churn.  Even at 1 commit rewritten per
   second, this would be a ~28-minute rebase that breaks every external
   clone, fork, CI cache, and signed-off history entry.

2. **AGENTS.md "non cambiare un gate per nascondere un errore".**
   Switching the gate to strict mode to FORCE remediation would constitute
   changing the gate to hide the asymmetry — exactly what AGENTS.md
   prohibits.

3. **AGENTS.md "no stime percentuali" + permanent baseline `7eb5c2ba`.**
   Wholesale history rewrites invalidate the canonical baseline reference
   for the project's 11/11 gate suite; rewriting forces the baseline to a
   new SHA, which violates the "single permanent baseline" rule.

4. **The current window-only gate correctly bounds ongoing drift.**  Every
   new commit landing on `main` is enforced via the 10-commit window; the
   drift surface is bounded by contributors' adherence to the 72-char
   policy documented in `CONTRIBUTING.md`.  A destructive rebase to
   "clean history" does not change meaningful forward-only behavior.

5. **Forward-only hygiene is enforceable; backward-only hygiene is not.**
   This is a structural asymmetry, not a bug.  The project chooses to
   accept it explicitly via ADR-021 rather than disguise it.

## What this ticket explicitly is NOT

- **NOT a remediation ticket.**  No `tools/rewrite_overlong_subjects.sh`
  is to be authored under this ticket's scope.
- **NOT a blocker** for any feature work.  Forward-only commit hygiene
  remains the operational policy.
- **NOT an audit script imperative.**  The 1,698 count is an informational
  snapshot as of 2026-07-12 against `origin/main` HEAD `52018fbb`.
  Re-auditing the count on other dates is informative but non-blocking.

## What future contributors may consider (out of TICKET scope)

If at some future date the project acquires tooling that batch-rewrites
over-limit commits without breaking external clones (e.g., a CI-driven
mirror-rebase tool isolated to a release branch), a new ticket may
propose remediation of the 1,698 historical violations.  Such a ticket
would require a follow-on ADR amending or superseding ADR-021 §Decision §2.

## Acceptance

This ticket is satisfied when:

- The 1,698 grandfathered count is documented in this file
  (post-2026-07-12 snapshot).
- ADR-021 references this ticket as the historical ledger companion.
- `CONTRIBUTING.md` references the 10-commit window scope and the
  grandfathered 1,698-commits count as canonical policy.

No further action required.

## References

- [`ADR-021`](../adr/ADR-021-commit-subject-length-policy.md) — companion
  ADR formalizing window-only enforcement + 1,698 grandfather policy.
- [`CONTRIBUTING.md`](../../CONTRIBUTING.md) — explicit "Commit Subject
  Policy" section documenting the 72-char limit, N=10 window, and
  grandfathering.
- [`tools/check_commit_subject_length.sh`](../tools/check_commit_subject_length.sh)
  — gate implementation (window-only, hard block exit 1).
- [`tools/wrap_push.sh`](../tools/wrap_push.sh) Step 4.5g — gate
  invocation in the push chain.
- AGENTS.md §commit-subject, §definitions-of-done, §release-gate,
  §GATE-MNT-01.
- `git log --format='%h %s' origin/main | awk 'length > 72' | wc -l` —
  the audit command (returns 1,698 against `origin/main` HEAD
  `52018fbb` as of 2026-07-12).
