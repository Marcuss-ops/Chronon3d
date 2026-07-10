# TICKET-CHANGELOG-CONFLICT-CLEANUP

**Title:** Document and clean up pre-existing CHANGELOG conflict markers
**ID:** TICKET-CHANGELOG-CONFLICT-CLEANUP
**Status:** DONE (2026-07-10)
**Priority:** P1
**Area:** Docs / CI hygiene
**Blocca:** docs/CHANGELOG.md rendering + markdown lint + doc-sync gate

---

## Background

`docs/CHANGELOG.md` contained raw git merge conflict markers (`<<<<<<< HEAD`, `=======`, `>>>>>>> be77fbd5`) for approximately 10 commits in the `main` branch history. These markers were accidentally resolved/removed as a side effect of commit `5efcc301` (TICKET-FASE3-MULTILINGUAL cycle 4, Hangul composition), which resolved the conflict by taking both sides (TICKET-SIMPLICITY entries from HEAD + F3.D entry from `be77fbd5`).

## Root Cause

The conflict markers were introduced in commit `f5551a13` (titled `docs(sync): F3.D closure - CHANGELOG + FOLLOWUP + CURRENT_STATUS`, 2026-07-10). This commit was a failed `git merge` of `be77fbd5` (F3.D closure) into HEAD that was committed verbatim with the conflict markers still in the file. The TICKET-SIMPLICITY entries (added by HEAD between `be77fbd5` and `f5551a13`) conflicted with the F3.D entry, and the merge was committed without resolution.

**Forensic evidence** (machine-verified):
- `git log --all -p -S'<<<<<<< HEAD' -- docs/CHANGELOG.md` → first occurrence in `f5551a13`
- `git log --oneline -10 -- docs/CHANGELOG.md` shows `f5551a13` as the introducing commit
- All HEAD~0 through HEAD~10 had 3 conflict markers (lines 103, 137, 153)
- `be77fbd5` itself did NOT have the markers (it was the incoming side of the failed merge)
- The conflict was TICKET-SIMPLICITY-INSPECT-TEXT-RENDER + TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY-ONLY (HEAD) vs F3.D LayerBuilder forward-point wiring (be77fbd5)

## Impact

- **Duration**: ~10 commits in `main` (from `f5551a13` to `5efcc301`)
- **Severity**: P1 — broke markdown rendering of CHANGELOG.md, broke doc-sync gate expectations, made the CHANGELOG unreadable in raw form
- **Blast radius**: all consumers of `docs/CHANGELOG.md` (docs lint, release notes generation, AI agent context)

## Resolution (commit `5efcc301`)

The immediate textual corruption was resolved as a side effect of the TICKET-FASE3-MULTILINGUAL cycle 4 commit. The resolution: take BOTH sides of the conflict (TICKET-SIMPLICITY entries + F3.D entry) and prepend the Fase 3 + Fase 4 entries. This was done via `sed -i '/^<<<<<<< /d; /^>>>>>>> /d; /^=======$/d' docs/CHANGELOG.md` after verifying that no markdown setext headings used `=======` as an underline.

## Forward Point: CI Gate

To prevent recurrence, a new CI gate `tools/check_no_changelog_conflict_markers.sh` is introduced. The gate greps for `^(<<<<<<< |=======$|>>>>>>> )` in `docs/CHANGELOG.md` and exits 1 if any markers are found. The gate is registered in `tools/wrap_push.sh` Step 4.5d (post-main-clean, pre-`git push`).

## Acceptance Criteria

- [x] Ticket documented at `docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md`
- [x] Root cause identified: commit `f5551a13` (failed `git merge` of `be77fbd5` F3.D, committed with markers)
- [x] CI gate `tools/check_no_changelog_conflict_markers.sh` created and executable
- [x] Gate registered in `tools/wrap_push.sh` Step 4.5d
- [x] Gate returns exit 0 locally on a clean file (verified)
- [x] Gate returns exit 1 with remediation hint if markers are found (verified)
- [x] FOLLOWUP_TICKETS.md §Recently Closed row added
- [x] CHANGELOG.md entry added at the top

## Related Tickets

- `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` — different scope (3 tracked files with committed markers, not just CHANGELOG.md). OPEN. Forward-point: this ticket addresses only the CHANGELOG.md case; the 3-files ticket addresses the broader rot pattern.

## Cross-references

- `docs/CHANGELOG.md` (entry at the top of this commit)
- `docs/FOLLOWUP_TICKETS.md` (§Recently Closed row)
- `tools/check_no_changelog_conflict_markers.sh` (the new gate)
- `tools/wrap_push.sh` Step 4.5d (gate registration)
- Commit `5efcc301` (the resolution)
- Commit `f5551a13` (the introducing commit)
- Commit `be77fbd5` (the incoming side of the failed merge)
