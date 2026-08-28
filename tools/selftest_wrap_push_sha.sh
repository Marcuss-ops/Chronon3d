#!/usr/bin/env bash
set -euo pipefail

# Static selftest for wrap_push.sh's main-line SHA contract. It does not fetch,
# push, rebase, or mutate git state.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WRAPPER="$ROOT/tools/wrap_push.sh"
fail() { echo "SELFTEST_FAIL: $*" >&2; exit 1; }
pass() { echo "SELFTEST_PASS: $*"; }

[[ -x "$WRAPPER" ]] || fail "wrap_push.sh is not executable"
grep -q 'CHRONON3D_TESTED_SHA' "$WRAPPER" || fail "explicit tested SHA contract missing"
grep -q 'HEAD changed after tests' "$WRAPPER" || fail "stale tested SHA guard missing"
grep -q 'force-push is forbidden on main' "$WRAPPER" || fail "main force-push guard missing"
grep -q 'POSTPUSH_SHA.*TESTED_SHA' "$WRAPPER" || fail "post-push tested SHA equality missing"
grep -q 'POSTPUSH_SHA.*UPSTREAM_SHA' "$WRAPPER" || fail "post-push upstream equality missing"

# Ensure the wrapper never uses a force flag implicitly.
if grep -Eq 'git push[^\n]*(--force|-f)' "$WRAPPER"; then
    fail "wrapper contains an implicit force push"
fi

pass "wrap_push tested SHA == HEAD == origin/main contract is wired"
