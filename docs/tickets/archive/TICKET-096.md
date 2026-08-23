# TICKET-096 — Stash debt triage (per-file apply-vs-drop decision for `stash@{0}`)

| Field      | Value                                                                                     |
| ---------- | ----------------------------------------------------------------------------------------- |
| ID         | TICKET-096                                                                                |
| Priorità   | P1                                                                                        |
| Area       | stash hygiene / prior-session debt closure                                                |
| Stato      | Done (DROP both)                                                                          |
| Origin     | user ask "Inspect `stash@{0}: On main: wip-prior-session-debt-items`... Decide per file..." |
| Drops      | `git stash drop stash@{0}` (planned, post-push)                                            |
| Refs       | `docs/FOLLOWUP_TICKETS.md` Recently-closed; `docs/CHANGELOG.md` §§ `hygiene — alphabetize…`, `hygiene — drop non-idempotent manifest helper script…` |

## Background

User surface the stash label as `stash@{0}: On main: wip-prior-session-debt-items` and asked for per-file apply-vs-drop triage. Actual WIP label (from `git stash list`) is:

```
stash@{0}: On main: pre-rebase: cmake alphabetize + helper script
```

i.e. the user's label was approximate; the actual stash is a 2-file hygiene WIP from a prior session and the actual `efd841f0` reference is a snapshot SHA that already lives in `docs/CURRENT_STATUS.md` (gate-audit snapshot, not HEAD — current HEAD is `3e2a2bee`, see below).

## Diagnosis

Two items in `stash@{0}` (verified via `git stash show -p stash@{0}` + `git show stash@{0}^3:tools/c3d_manifest_alphabetize.py`):

| # | Path                                          | Status pre-triage | Diff size | Upstream intent                                              |
| - | --------------------------------------------- | ----------------- | --------- | ------------------------------------------------------------ |
| 1 | `cmake/Chronon3DPublicHeaders.cmake`          | MODIFIED          | 38+, 63-  | alphabetize + retire `TICKET-GATE-10-PHASE-4` placeholder block |
| 2 | `tools/c3d_manifest_alphabetize.py`           | UNTRACKED         | 118 lines | reusable canonical-bucket sort helper for the manifest      |

### Decision per file

| # | File                                          | Verdict | Rationale                                                                                                                                                                                                                                                                                                                                                                                                                          |
| - | --------------------------------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 | `cmake/Chronon3DPublicHeaders.cmake`          | **DROP** | Sequence of intervening commits on `main` already executed the same intent: commit `eed2cc9b` ("fix(hygiene): alphabetize Chronon3DPublicHeaders manifest") and the documented `hygiene — alphabetize Chronon3DPublicHeaders manifest (retro-fixup to 21b9fb5d)` CHANGELOG entry. A naive `git stash apply` against current HEAD would either no-op or conflict on the same hunk block, making apply a no-value churn operation. |
| 2 | `tools/c3d_manifest_alphabetize.py`           | **DROP** | Companion helper was already removed in the documented retro-fixup entry `hygiene — drop non-idempotent manifest helper script (retro-fixup to eed2cc9b)`: it was non-idempotent (`AssertionError: parse fail: bulk=None` on re-run against already-alphabetized manifests). Future alphabetize drift will surface via `tools/check_public_headers.py` instead of a brittle utility. |

Both items are therefore **superseded by intervening commits on `main`**; their net contribution to current `main@3e2a2bee` would be zero diff + non-trivial conflict risk during `git stash apply`. The honest category for the stash is **DROP**.

## Acceptance criteria

1. `docs/tickets/TICKET-096.md` (this file) created.
2. `docs/FOLLOWUP_TICKETS.md` § Recently closed updated with TICKET-096 row.
3. `git stash drop stash@{0}` executed **after** the docs commit lands on `main` (safety: docs are recoverable from `main` before the destructive drop).
4. Post-push verification: `git stash list` empty; `HEAD == origin/main`; `tools/check_doc_sync.sh` PASS; `tools/check_main_clean.sh` PASS.
5. No source-tree prefix re-introduced in `cmake/` or `tools/`.

## Note

- The user's reference to `efd841f0` baseline is approximate: that SHA appears in `docs/CURRENT_STATUS.md` as the `Main@efd841f0 gate-audit historical` snapshot reference (10/11 machine-verified). The actual `main` HEAD at triage time is `3e2a2bee`.
- AGENTS.md v0.1 freeze alignment: documentation-only commit honoring Cat-5 (allineamento documentazione) + Cat-3 (legacy path removal documentation) on the destructive `git stash drop` side. No new public symbols, no new `target_link_libraries`, no new resolvers or registries.
- Companion spec: see also [`docs/CHANGELOG.md`](../CHANGELOG.md) § hygiene — alphabetize… and § hygiene — drop non-idempotent manifest helper script… (the retro-fixup entries that already subsumed this stash's intent).
