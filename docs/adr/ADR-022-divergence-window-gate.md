# ADR-022 - Divergence Window Gate (advisory visibility for true-divergence state)

## Status
Accepted (2026-07-12, committed via TICKET-INFRA-F2-DIVERGENCE closure cycle).

## Date
2026-07-12

## Deciders
Agent3 (CI gate harmonization surfaced during the wrap_push F2 push-drain cycle).

## Tags
gate-policy, divergence, AGENTS.md GATE-MNT-01, GATE-MNT-01-EXT, wrap_push, ADR-required.

## Context
The pre-push wrapper `tools/wrap_push.sh` invokes `tools/check_main_clean.sh` which rejects TRUE divergence (no ancestor relation in either direction). The wrapper already has Step 3 (auto-FF detection) and Step 4 (check_main_clean.sh hard-block on TRUE divergence). However neither gate surfaced the divergence STATE itself (LOCAL_AHEAD x REMOTE_AHEAD) BEFORE the hard-block at the gate.

The user-driven F2 push-drain cycle (LOCAL_AHEAD=6, REMOTE_AHEAD=10, MERGE_BASE=0947ce6a, backup-sunset-test-16 tag safe in both chains, 2026-07-12) demonstrated that operators need a gate that emits the divergence state as advisory visibility BEFORE the existing check_main_clean rejects - to allow wrap_push to surface the F2 root-cause state instead of failing opaque.

### Why this ADR is needed
Per AGENTS.md Rule #6 (no nuovi singleton/registry/resolver/cache analog senza ADR), creating `tools/check_push_divergence_window.sh` requires an ADR. The existing 17 ADRs do not formalize any divergence-state-visibility gate; even check_main_clean.sh's ancestor reasoning is implemented directly without an ADR pattern reference. ADR-022 closes this gap and provides the canonical place for future divergence-window-policy decisions.

## Decision
Adopt the advisory semantics for `tools/check_push_divergence_window.sh`:

1. Advisory, NOT hard-block. The gate emits `[INFO]` and `[WARN]` divergence-state visibility but always exits 0. The existing check_main_clean.sh retains its hard-block role.
2. Default thresholds (configurable via env vars):
   - CHRONON3D_DIV_WINDOW_MAX_LOCAL_AHEAD (default 10)
   - CHRONON3D_DIV_WINDOW_MAX_REMOTE_AHEAD (default 10)
   - Setting either env var to 0 disables that side's warning entirely.
3. Wired into wrap_push.sh at Step 4.5h (between 4.5g commit-subject-length and Step 5 git push). Runs AFTER check_main_clean.sh so it does not obscure hard-block diagnostics.
4. Output convention: `[INFO] check_push_divergence_window: LOCAL_AHEAD=N REMOTE_AHEAD=M true_divergence=YES/NO ...` always; threshold breach emits `[WARN]` to stderr. Format follows AGENTS.md INFO-level diagnostic style precedent.
5. No `--skip-gates` escape hatch. The advisory semantics does NOT add a skip env var; the gate is informational and adding skips for an info-only gate would be Cargo-cult.
6. Threshold is configurable. Unlike ADR-021 §4 (window-only commit-subject-length), divergence thresholds ARE env-var configurable per warden.

## Consequences

### Positive
- Divergence state is surfaced before hard-block (Step 4.5h runs after Step 4 check_main_clean so any divergence >0 is visible CUMULATIVELY).
- The 6/10 split that bit the F2 cycle gets logged in future divergences with a [WARN] prefix.
- check_main_clean.sh hard-block behavior is preserved (new gate is purely informational).
- Forward-only filename matches existing Cat-4 ancillary gate convention.

### Negative
- The [WARN] on threshold breach is non-actionable (no automatic remediation). Intrinsic to advisory gates.
- Two new env vars need CONTRIBUTING.md documentation. Deferred to follow-up cycle.
- Threshold defaults (10/10) are arbitrary; future splits >10/10 will warn.

### Neutral
- ADR-022 does not change existing 11/11 gate baseline (additive at Step 4.5h).
- New gate runs on EVERY push attempt; cost is sub-millisecond.

## Alternatives Considered

### A1. Hard-block with strict thresholds
Rejected: AGENTS.md Non cambiare un gate per nascondere un errore + visibility-precedes-enforcement per INFO-level diagnostic style - operators should SEE divergence first then decide.

### A2. Embed in check_main_clean.sh directly
Rejected: check_main_clean.sh is canonically minimal GATE-MNT-01; adding divergence-window logic bloats scope and muddles failure semantics.

### A3. Replace check_main_clean with the new gate
Rejected: AGENTS.md Hardblock always , no --skip-gates escape hatch precedent at GATE-MNT-011 - removing hard-block on TRUE divergence is a §honesty violation.

## References
- AGENTS.md GATE-MNT-01, Non cambiare un gate per nascondere un errore, Regole di lint documentale INFO-level diagnostic style.
- tools/wrap_push.sh Step 4.5h - gate invocation (this commit).
- tools/check_main_clean.sh - hard-block anchor.
- docs/tickets/TICKET-INFRA-F2-DIVERGENCE.md - closure ticket.
- docs/adr/ADR-021-commit-subject-length-policy.md - format precedent.
