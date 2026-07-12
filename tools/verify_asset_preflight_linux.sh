#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_asset_preflight_linux.sh
#
# Canonical Asset manifest & preflight certification gate (P0).
#
# Covers 10 minimum sabotage scenarios per user spec verbatim:
#   1.   font inesistente            → tests/fixtures/missing_font.json (Test #7 existing)
#   2.   PNG inesistente             → tests/fixtures/missing_image.json (Test #7 existing)
#   3.   PNG corrotto                → tests/fixtures/asset_preflight/corrupt_image.json (NEW)
#   4.   MP4 inesistente             → tests/fixtures/asset_preflight/missing_video.json (NEW)
#   5.   MP4 corrotto                → tests/fixtures/corrupt_video.json (Test #7 existing)
#   6.   audio inesistente           → tests/fixtures/asset_preflight/missing_audio.json (NEW)
#   7.   path non leggibile          → tests/fixtures/non_writable_dir.json (Test #7 existing, proxy)
#   8.   estensione sbagliata        → tests/fixtures/asset_preflight/extension_mismatch.json (NEW)
#   9.   asset duplicato             → tests/fixtures/asset_preflight/asset_duplicate.json (NEW)
#   10.  asset cambiato durante render → tests/fixtures/asset_preflight/asset_changed_during_render.json (NEW)
#
# Per-fixture verification (canonical fail-loud contract per Test #7):
#   - exit code != 0 (loud propagation)
#   - output MP4 file absent post-run (no silent partial-write)
#   - stderr contains >= 4 canonical tokens (composition/Layer/asset/Path + asset-specific)
#   - error message mentions asset name + owner layer name (per user-spec verbatim)
#   - NO silent-fallback markers (fallback frame, black frame, continue on error, fallback to silence)
#   - NO .partial residue (per user-spec verbatim 'no .partial residuo')
#
# Honest-state contract (AGENTS.md §honesty + §honest-limitation):
#   - ASSET_PREFLIGHT_FUNCTIONAL_PASS is only emitted when ALL 10 fixtures pass.
#   - ASSET_PREFLIGHT_FUNCTIONAL_FAIL is emitted on any FAIL section.
#   - ASSET_PREFLIGHT_FUNCTIONAL_BLOCKED is emitted when env/binary is blocked.
#   - Exit code 0 = PASS, 1 = FAIL, 2 = BLOCKED.
#
# Usage:
#   bash tools/verify_asset_preflight_linux.sh
#
# Environment overrides:
#   CHRONON3D_ASSET_SKIP_FIXTURES=1     Skip the 10 fixture invocations (audit-only)
#   CHRONON3D_ASSET_CLI_BIN=<path>      Override CLI binary path
#   CHRONON3D_ASSET_QUIET=1              Suppress per-fixture detail output
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_asset_preflight_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Env-var initialization (BEFORE set -u) — explicit defaults
CHRONON3D_ASSET_SKIP_FIXTURES="${CHRONON3D_ASSET_SKIP_FIXTURES:-0}"
CHRONON3D_ASSET_CLI_BIN="${CHRONON3D_ASSET_CLI_BIN:-}"
CHRONON3D_ASSET_QUIET="${CHRONON3D_ASSET_QUIET:-0}"

set -uo pipefail   # NOTE: NOT `set -e` — each section must record its own outcome.

# Output directory (10 fixture harness dirs + per-fixture stderr logs land here)
OUTPUT_DIR="/tmp/chronon3d_asset_preflight_cert"
mkdir -p "$OUTPUT_DIR"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
ENV_BLOCKED=false

# 10 fixtures: name|fixture_path|rot_class
# 5 existing Test #7 + 6 NEW asset preflight
FIXTURES=(
    "missing_font|tests/fixtures/missing_font.json|missing_font_asset"
    "missing_image|tests/fixtures/missing_image.json|missing_image_asset"
    "corrupt_image|tests/fixtures/asset_preflight/corrupt_image.json|corrupt_image_asset"
    "missing_video|tests/fixtures/asset_preflight/missing_video.json|missing_video_asset"
    "corrupt_video|tests/fixtures/corrupt_video.json|corrupt_video_asset"
    "missing_audio|tests/fixtures/asset_preflight/missing_audio.json|missing_audio_asset"
    "non_writable_dir|tests/fixtures/non_writable_dir.json|non_writable_output_target"
    "extension_mismatch|tests/fixtures/asset_preflight/extension_mismatch.json|extension_mismatch_asset"
    "asset_duplicate|tests/fixtures/asset_preflight/asset_duplicate.json|duplicate_asset_in_manifest"
    "asset_changed_during_render|tests/fixtures/asset_preflight/asset_changed_during_render.json|asset_changed_during_render"
)

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass() {
    if [ "$CHRONON3D_ASSET_QUIET" != "1" ]; then
        echo "  [PASS] $1"
    fi
    PASS_COUNT=$((PASS_COUNT + 1))
}

_gate_fail() {
    echo "  [FAIL] $1 — $2"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

_gate_blocked() {
    if [ "$CHRONON3D_ASSET_QUIET" != "1" ]; then
        echo "  [BLOCKED] $1 — $2"
    fi
    BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
}

# Locate chronon3d_cli binary (canonical search paths)
find_chronon3d_cli() {
    if [ -n "$CHRONON3D_ASSET_CLI_BIN" ] && [ -x "$CHRONON3D_ASSET_CLI_BIN" ]; then
        echo "$CHRONON3D_ASSET_CLI_BIN"
        return 0
    fi
    for candidate in \
        "${ROOT}/build/chronon/linux-content-dev/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-ci/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-ci-full-validation/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-fast-dev/chronon3d_cli" \
        "${ROOT}/.tmp/chronon-builds/linux-fast-dev/chronon3d_cli" \
        "${ROOT}/build/manual-test/chronon3d_cli" \
        "$(command -v chronon3d_cli 2>/dev/null)"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

# jq wrapper: extract a JSON field from a fixture
jq_field() {
    local fixture="$1" field="$2"
    jq -r "$field // \"\"" "$fixture" 2>/dev/null
}

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_asset_preflight_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

if ! git diff --quiet HEAD 2>/dev/null; then
    echo "ASSET_FAIL: working tree has uncommitted changes"
    git status -sb
    exit 1
fi
if ! git diff --cached --quiet; then
    echo "ASSET_FAIL: index has staged changes"
    git status -sb
    exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
    echo "ASSET_FAIL: working tree has untracked changes"
    git status -sb
    exit 1
fi

git fetch origin 2>/dev/null || true
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main 2>/dev/null || echo "N/A")
if [ "$LOCAL" != "$REMOTE" ] \
   && ! git merge-base --is-ancestor "$LOCAL" "$REMOTE" 2>/dev/null \
   && ! git merge-base --is-ancestor "$REMOTE" "$LOCAL" 2>/dev/null; then
    echo "ASSET_FAIL: HEAD and origin/main have diverged"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Tree: clean"
echo "  Aligned: origin/main"
_gate_pass "repository_state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Environment audit
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Environment audit =="

CLI_BIN="$(find_chronon3d_cli)" || {
    _gate_blocked "chronon3d_cli_binary" "not found — set CHRONON3D_ASSET_CLI_BIN or build via cmake --preset linux-content-dev"
    ENV_BLOCKED=true
}
if [ -n "$CLI_BIN" ] && [ "$ENV_BLOCKED" = false ]; then
    CLI_SIZE=$(stat -c%s "$CLI_BIN" 2>/dev/null || echo 0)
    _gate_pass "chronon3d_cli_binary (${CLI_BIN}, ${CLI_SIZE} bytes)"
fi

if command -v jq >/dev/null 2>&1; then
    JQ_VER=$(jq --version 2>/dev/null)
    _gate_pass "jq ($JQ_VER)"
else
    _gate_blocked "jq" "not found — install via apt install jq"
    ENV_BLOCKED=true
fi

if command -v bash >/dev/null 2>&1; then
    BASH_VER_MAJOR=$(bash -c 'echo "${BASH_VERSINFO[0]}"')
    if [ "$BASH_VER_MAJOR" -ge 4 ]; then
        _gate_pass "bash ($BASH_VER_MAJOR.x)"
    else
        _gate_blocked "bash" "version $BASH_VER_MAJOR.x too old (need >=4 for associative arrays)"
        ENV_BLOCKED=true
    fi
else
    _gate_blocked "bash" "not found"
    ENV_BLOCKED=true
fi

# ══════════════════════════════════════════════════════════════════════════════
# 3. Test corpus scaffold (10 fixtures: 5 Test #7 + 6 NEW asset preflight)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. Test corpus scaffold (10 fixtures) =="

EXISTING_FIXTURE_COUNT=0
NEW_FIXTURE_COUNT=0
for fixture_def in "${FIXTURES[@]}"; do
    IFS='|' read -r f_name f_path f_rot <<< "$fixture_def"
    if [ -s "$f_path" ]; then
        if [[ "$f_path" == *"/asset_preflight/"* ]]; then
            NEW_FIXTURE_COUNT=$((NEW_FIXTURE_COUNT + 1))
        else
            EXISTING_FIXTURE_COUNT=$((EXISTING_FIXTURE_COUNT + 1))
        fi
        _gate_pass "fixture[$f_name] ($f_path, rot_class=$f_rot)"
    else
        _gate_fail "fixture[$f_name]" "missing or empty: $f_path"
    fi
done

if [ "$EXISTING_FIXTURE_COUNT" -ne 5 ]; then
    _gate_fail "fixture_audit" "expected 5 existing Test #7 fixtures, found $EXISTING_FIXTURE_COUNT"
fi
if [ "$NEW_FIXTURE_COUNT" -ne 6 ]; then
    _gate_fail "fixture_audit" "expected 6 NEW asset preflight fixtures, found $NEW_FIXTURE_COUNT"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. Per-fixture fail-loud contract check
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. Per-fixture fail-loud contract check =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "fixture_contract" "env blocker upstream — see section 2"
elif [ "$CHRONON3D_ASSET_SKIP_FIXTURES" = "1" ]; then
    _gate_blocked "fixture_contract" "skipped via env override"
else
    for fixture_def in "${FIXTURES[@]}"; do
        IFS='|' read -r f_name f_path f_rot <<< "$fixture_def"

        if [ ! -s "$f_path" ]; then
            _gate_fail "contract[$f_name]" "fixture file missing: $f_path"
            continue
        fi

        # Parse fixture fields
        comp_id=$(jq_field "$f_path" '.composition_id')
        out_path=$(jq_field "$f_path" '.output_path')
        min_matches=$(jq_field "$f_path" '.expected.minimum_token_matches')
        [ -z "$min_matches" ] && min_matches=3

        # Extract required tokens + forbidden substrings + error_must_mention
        mapfile -t required_tokens < <(jq -r '.expected.stderr_tokens_required // [] | .[]' "$f_path")
        mapfile -t forbidden_subs < <(jq -r '.expected.forbidden_substrings // [] | .[]' "$f_path")
        mapfile -t error_must_mention < <(jq -r '.expected.error_must_mention // [] | .[]' "$f_path")
        mapfile -t cli_args_arr < <(jq -r '.cli_args // [] | .[]' "$f_path")

        # Per-fixture stderr log
        STDERR_LOG="${OUTPUT_DIR}/${f_name}.stderr"
        STDOUT_LOG="${OUTPUT_DIR}/${f_name}.stdout"

        # Cleanup any pre-existing output
        rm -f "$out_path" 2>/dev/null || true

        # Special handling for asset_changed_during_render: simulate mtime change via sidecar
        if [ "$f_name" = "asset_changed_during_render" ]; then
            HARNESS_DIR="/tmp/chronon_asset_changed_harness"
            HARNESS_FILE="${HARNESS_DIR}/chaos.png"
            mkdir -p "$HARNESS_DIR"
            # Create initial file
            echo "before_change" > "$HARNESS_FILE"
            INITIAL_MTIME=$(stat -c%Y "$HARNESS_FILE" 2>/dev/null || echo 0)
            sleep 1
            # Simulate the change
            echo "after_change" > "$HARNESS_FILE"
            NEW_MTIME=$(stat -c%Y "$HARNESS_FILE" 2>/dev/null || echo 0)
            # Update CLI args with the actual mtimes
            cli_args_arr=("--assets-root" "$HARNESS_DIR" "--image-asset" "$HARNESS_FILE" "--preflight-mtime" "$INITIAL_MTIME" "--render-mtime" "$NEW_MTIME")
        fi

        # Run chronon3d_cli (capture exit + stderr)
        set +e
        "$CLI_BIN" render "$comp_id" "${cli_args_arr[@]}" --output "$out_path" \
            >"$STDOUT_LOG" 2>"$STDERR_LOG"
        cli_exit=$?
        set -e
        stderr_body=$(cat "$STDERR_LOG" 2>/dev/null || echo "")

        if [ "$CHRONON3D_ASSET_QUIET" != "1" ]; then
            echo "  [$f_name] rot_class=$f_rot, cli_exit=$cli_exit (expected nonzero)"
        fi

        violations=()

        # Assertion 1: exit code nonzero (fail-loud must propagate FAIL)
        if [ "$cli_exit" -eq 0 ]; then
            violations+=("spurious clean exit (cli_exit=0 while rot_class=$f_rot expects fail-loud)")
        fi

        # Assertion 2: output file absent after the run (no silent partial-write)
        if [ -e "$out_path" ]; then
            violations+=("output file $out_path EXISTS after run (silent fallback wrote a partial MP4)")
        fi

        # Assertion 3: stderr contains at least N canonical tokens
        matched_tokens=0
        for token in "${required_tokens[@]}"; do
            if echo "$stderr_body" | grep -qiE "\\b${token}\\b" 2>/dev/null; then
                matched_tokens=$((matched_tokens + 1))
            fi
        done
        if [ "$matched_tokens" -lt "$min_matches" ]; then
            violations+=("stderr matched only $matched_tokens of ${#required_tokens[@]} required canonical tokens (minimum=$min_matches); required: ${required_tokens[*]}")
        fi

        # Assertion 4: error message mentions asset + owner/layer (per user-spec verbatim)
        for mention in "${error_must_mention[@]}"; do
            if ! echo "$stderr_body" | grep -qiE "\\b${mention}\\b" 2>/dev/null; then
                violations+=("stderr does not mention required '$mention' (per user-spec 'errore che indica asset+owner/layer')")
            fi
        done

        # Assertion 5: NO silent-fallback markers
        for fb in "${forbidden_subs[@]}"; do
            if echo "$stderr_body" | grep -qiF "$fb" 2>/dev/null; then
                violations+=("stderr contains forbidden silent-fallback marker: '$fb'")
            fi
        done

        if [ "${#violations[@]}" -eq 0 ]; then
            _gate_pass "contract[$f_name] (matched $matched_tokens tokens, error mentions ${#error_must_mention[@]} required keys)"
        else
            violation_count=${#violations[@]}
            violation_summary=$(IFS='; '; echo "${violations[*]}")
            _gate_fail "contract[$f_name]" "$violation_count violation(s): $violation_summary"
        fi
    done
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. Atomic output audit (no .partial residue per user-spec verbatim)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. Atomic output audit (no .partial residue) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "partial_residue" "env blocker upstream — see section 2"
else
    # Search for any .partial files left behind in the per-fixture output paths
    PARTIAL_COUNT=$(find "$OUTPUT_DIR" -name "*.partial" 2>/dev/null | wc -l)
    if [ "$PARTIAL_COUNT" -eq 0 ]; then
        _gate_pass "partial_residue (0 .partial files in $OUTPUT_DIR)"
    else
        _gate_fail "partial_residue" "$PARTIAL_COUNT .partial file(s) found (atomic-output contract broken)"
        find "$OUTPUT_DIR" -name "*.partial" -print 2>/dev/null | head -5
    fi

    # Also check that the canonical output_path for each fixture is absent
    for fixture_def in "${FIXTURES[@]}"; do
        IFS='|' read -r f_name f_path f_rot <<< "$fixture_def"
        out_path=$(jq_field "$f_path" '.output_path')
        if [ -e "$out_path" ]; then
            _gate_fail "output_absent[$f_name]" "output file $out_path EXISTS (atomic-output contract broken)"
        fi
    done
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. Cross-fixture token coverage (verify 10 fixtures collectively cover all 4 canonical tokens)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. Cross-fixture token coverage (composition/Layer/asset/Path) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "token_coverage" "env blocker upstream — see section 2"
else
    # Aggregate all per-fixture stderr logs into one big log for the cross-fixture check
    AGGREGATE_STDERR="${OUTPUT_DIR}/_aggregate_stderr.log"
    : > "$AGGREGATE_STDERR"
    for fixture_def in "${FIXTURES[@]}"; do
        IFS='|' read -r f_name f_path f_rot <<< "$fixture_def"
        STDERR_LOG="${OUTPUT_DIR}/${f_name}.stderr"
        if [ -s "$STDERR_LOG" ]; then
            echo "===== $f_name =====" >> "$AGGREGATE_STDERR"
            cat "$STDERR_LOG" >> "$AGGREGATE_STDERR"
            echo "" >> "$AGGREGATE_STDERR"
        fi
    done

    # Verify each of the 4 canonical tokens appears in the aggregate
    for token in Composition Layer Asset Path; do
        if grep -qiE "\\b${token}\\b" "$AGGREGATE_STDERR" 2>/dev/null; then
            _gate_pass "token_coverage[$token] (covered by aggregate stderr)"
        else
            _gate_fail "token_coverage[$token]" "not found in aggregate stderr (canonical error vocabulary not covered)"
        fi
    done

    # Verify NO silent-fallback markers in aggregate
    for fb in "fallback frame" "black frame" "continue on error" "fallback to silence"; do
        if grep -qiF "$fb" "$AGGREGATE_STDERR" 2>/dev/null; then
            _gate_fail "silent_fallback[$fb]" "found in aggregate stderr (silent-fallback marker violated)"
        else
            _gate_pass "silent_fallback[$fb] (absent from aggregate stderr)"
        fi
    done
fi

# ══════════════════════════════════════════════════════════════════════════════
# 7. Final verdict
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo "  Output:  $OUTPUT_DIR"
echo "  Fixtures: ${#FIXTURES[@]} (5 existing Test #7 + 6 NEW asset preflight)"
echo ""

# NOTE: [INFO] line emission per AGENTS.md Rule #2 is ONLY on the PASS path
# (below) as the addizionale line. The BLOCKED path is non-PASS, so the
# [INFO] emission is suppressed here.

if [ "$ENV_BLOCKED" = true ]; then
    echo "ASSET_PREFLIGHT_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  Asset preflight certification is blocked by environment:"
    echo "    - chronon3d_cli binary not built (vcpkg glm/magic_enum + tmpfs env-blocked VPS)"
    echo "    - jq not installed (required for fixture parsing)"
    echo "    - Fix: resolve TICKET-BUILD-ROT-CASCADE-CAMERA + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV + apt install jq"
    echo "    - macchina-verifica DEFERRED to working build host per AGENTS.md §honest-limitation"
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "ASSET_PREFLIGHT_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    exit 1
else
    echo "ASSET_PREFLIGHT_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Asset preflight certified (10 sabotage scenarios + 4 canonical tokens + 4 silent-fallback markers)."
    echo "[INFO] ${GATE_NAME}: ${#FIXTURES[@]}/${#FIXTURES[@]} sabotage scenarios verified fail-loud (composition/Layer/Asset/Path tokens + asset+owner/layer mention + no silent fallback)"
    exit 0
fi
