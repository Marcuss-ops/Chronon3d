#!/usr/bin/env bash
# check_first_principles_fail_loud.sh — First-Principles Product Check (Test #7).
# Verifies that chronon3d_cli emits fail-loud (loud stderr + non-zero exit + no
# silent fallback) for 5 canonical rotted invocations:
#   - missing_font
#   - missing_image
#   - corrupt_video
#   - invalid_camera
#   - non_writable_dir
#
# Per AGENTS.md:
#   - Cat-3 anti-duplication: one canonical fail-loud gate; no per-fixture scripts.
#   - §honest-limitation: when the chronon3d_cli binary is not built on this host,
#     the script FAIL-LOUDS (does NOT pretend to pass) and emits an exact
#     diagnostic + retry-hint listing the canonical build command.
#   - §INFO-level diagnostic style: `[INFO] <gate-name>: <message>` on PASS
#     sideband to the canonical GATE_PASS emission.
#
# Exit code: 0 on PASS (all 5 fixtures honored fail-loud contract), 1 on FAIL.
set -euo pipefail

GATE_NAME=check_first_principles_fail_loud
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

FIXTURES_DIR="$REPO_ROOT/tests/fixtures"
TMP_STDOUT="$(mktemp -t chronon_fail_loud_stdout.XXXXXX)"
TMP_STDERR="$(mktemp -t chronon_fail_loud_stderr.XXXXXX)"
trap 'rm -f "$TMP_STDOUT" "$TMP_STDERR"' EXIT

echo "== First-Principles Product Check Test #7 (Fail-loud errors) =="

# ─────────── 1. chronon3d_cli binary discovery ───────────
# Canonical build dir convention: <repo_root>/build/manual-test/chronon3d_cli
# Fallback: tools/chronon-linux.sh wrapper (handles build + run lifecycle).
CHRONON_CLI=""
for candidate in \
  "$REPO_ROOT/build/manual-test/chronon3d_cli" \
  "$REPO_ROOT/build/chronon3d_cli" \
  "$REPO_ROOT/build/chronon/linux-content-dev/chronon3d_cli"
do
  if [ -x "$candidate" ]; then
    CHRONON_CLI="$candidate"
    break
  fi
done

if [ -z "$CHRONON_CLI" ]; then
  cat <<EOF
GATE_FAIL: chronon3d_cli binary not found in standard build paths.
  Searched:
    $REPO_ROOT/build/manual-test/chronon3d_cli
    $REPO_ROOT/build/chronon3d_cli
    $REPO_ROOT/build/chronon/linux-content-dev/chronon3d_cli
  Recover the canonical build:
    cmake --preset linux-content-dev
    cmake --build --preset linux-content-dev -j"\$(nproc)" --target chronon3d_cli
  Re-run: bash tools/check_first_principles_fail_loud.sh
EOF
  exit 1
fi
echo "[INFO] $GATE_NAME: using chronon3d_cli at $CHRONON_CLI"

# ─────────── 2. jq discovery (for fixture parsing) ───────────
if ! command -v jq >/dev/null 2>&1; then
  cat <<EOF >&2
GATE_FAIL: jq not found in PATH. Test #7 requires jq for fixture parsing.
  Install: sudo apt install jq  (Debian/Ubuntu)
            brew install jq     (macOS)
EOF
  exit 1
fi

# ─────────── 3. Fixtures verification ───────────
mapfile -t FIXTURES < <(printf '%s\n' \
  "$FIXTURES_DIR/missing_font.json" \
  "$FIXTURES_DIR/missing_image.json" \
  "$FIXTURES_DIR/corrupt_video.json" \
  "$FIXTURES_DIR/invalid_camera.json" \
  "$FIXTURES_DIR/non_writable_dir.json" \
  | sort)

for f in "${FIXTURES[@]}"; do
  if [ ! -s "$f" ]; then
    echo "GATE_FAIL: fixture $f missing or empty" >&2
    exit 1
  fi
  if ! jq -e . "$f" >/dev/null 2>&1; then
    echo "GATE_FAIL: fixture $f is not valid JSON: $(jq -e . "$f" 2>&1 | head -3)" >&2
    exit 1
  fi
done

# ─────────── 4. Per-fixture fail-loud assertion loop ───────────
PASS_COUNT=0
FAIL_COUNT=0
FAIL_SUMMARY=()

for fixture in "${FIXTURES[@]}"; do
  fixture_name="$(basename "$fixture" .json)"
  echo "  --- fixture: $fixture_name ---"

  # Parse fields (case-insensitive lookup; defensive defaults)
  comp_id="$(jq -r '.composition_id // "UnknownFixture"' "$fixture")"
  out_path="$(jq -r '.output_path // "/tmp/should-not-exist.mp4"' "$fixture")"
  rot_class="$(jq -r '.rot_class // "unknown_rot"' "$fixture")"
  expected_nonzero="$(jq -r '.expected.exit_code_nonzero // true' "$fixture")"
  expected_output_absent="$(jq -r '.expected.output_file_absent // .output_path // "/tmp/should-not-exist.mp4"' "$fixture")"
  expected_min_matches="$(jq -r '.expected.minimum_token_matches // 3' "$fixture")"
  mapfile -t required_tokens < <(jq -r '.expected.stderr_tokens_required // [] | .[]' "$fixture")
  mapfile -t forbidden_subs < <(jq -r '.expected.forbidden_substrings // [] | .[]' "$fixture")
  mapfile -t cli_args_arr < <(jq -r '.cli_args // [] | .[]' "$fixture")

  # Cleanup any pre-existing output (the contract is "must be absent post-run").
  # Use sudo-less rm; if we lack permission the file should already be unwritable.
  rm -f "$out_path" 2>/dev/null || true

  # Run the chronon3d_cli (capture exit + stderr).
  set +e
  "$CHRONON_CLI" render "$comp_id" "${cli_args_arr[@]}" --output "$out_path" \
    >"$TMP_STDOUT" 2>"$TMP_STDERR"
  cli_exit=$?
  set -e
  stderr_body="$(cat "$TMP_STDERR")"
  stdout_body="$(cat "$TMP_STDOUT")"
  echo "    composition_id: $comp_id"
  echo "    rot_class:      $rot_class"
  echo "    cli_args:       ${cli_args_arr[*]:-<none>}"
  echo "    cli_exit:       $cli_exit  (expected nonzero=$expected_nonzero)"
  echo "    output_path:    $out_path"
  [ -n "$stderr_body" ] && echo "    stderr (first 200 chars): $(printf '%s' "$stderr_body" | head -c 200)"

  violations=()

  # Assertion 1: exit code nonzero (fail-loud must propagate FAIL).
  if [ "$expected_nonzero" = "true" ] && [ "$cli_exit" -eq 0 ]; then
    violations+=("spurious clean exit (cli_exit=0 while rot_class=$rot_class expects fail-loud)")
  fi

  # Assertion 2: output file absent after the run.
  if [ -e "$expected_output_absent" ]; then
    violations+=("output file $expected_output_absent EXISTS after run (silent fallback wrote a partial MP4)")
  fi

  # Assertion 3: stderr contains at least N canonical tokens (case-insensitive).
  matched_tokens=0
  for token in "${required_tokens[@]}"; do
    if printf '%s' "$stderr_body" | grep -qiE "\\b${token}\\b" 2>/dev/null; then
      matched_tokens=$((matched_tokens + 1))
    fi
  done
  if [ "$matched_tokens" -lt "$expected_min_matches" ]; then
    violations+=("stderr matched only $matched_tokens of ${#required_tokens[@]} required canonical tokens (minimum=$expected_min_matches); required list: ${required_tokens[*]}")
  fi

  # Assertion 4: NO silent-fallback markers.
  for fb in "${forbidden_subs[@]}"; do
    if printf '%s' "$stderr_body" | grep -qiF "$fb" 2>/dev/null; then
      violations+=("stderr contains forbidden silent-fallback marker: '$fb'")
    fi
  done

  if [ "${#violations[@]}" -eq 0 ]; then
    echo "    PASS (matched $matched_tokens canonical tokens; exit=$cli_exit)"
    PASS_COUNT=$((PASS_COUNT + 1))
  else
    echo "    FAIL: ${#violations[@]} violation(s):"
    for v in "${violations[@]}"; do
      echo "      - $v"
    done
    FAIL_SUMMARY+=("$fixture_name: ${#violations[@]} violations")
    FAIL_COUNT=$((FAIL_COUNT + 1))
  fi
done

# ─────────── 5. Aggregate gate verdict ───────────
echo
echo "==================================================================="
echo "fixtures PASS/FAIL summary: PASS=$PASS_COUNT  FAIL=$FAIL_COUNT  (total=${#FIXTURES[@]})"
echo

if [ "$FAIL_COUNT" -gt 0 ]; then
  echo "GATE_FAIL: $FAIL_COUNT/${#FIXTURES[@]} fixture(s) violated the fail-loud contract."
  for s in "${FAIL_SUMMARY[@]}"; do
    echo "  - $s"
  done
  exit 1
fi

echo "GATE_PASS: all ${#FIXTURES[@]} fixtures honored the canonical fail-loud contract"
echo "  - exit code != 0 (loud error propagation)"
echo "  - output MP4 file absent (no silent partial-write)"
echo "  - stderr contains ≥ ${required_min_matches}=$expected_min_matches canonical error vocabulary tokens (composition/layer/asset/path)"
echo "  - stderr contains NO silent-fallback markers (no 'fallback frame', 'black frame', 'continue on error')"
echo "[INFO] ${GATE_NAME}: ${#FIXTURES[@]}/${#FIXTURES[@]} fixtures verified fail-loud (composition/layer/asset/path tokens matched, no silent fallback)"
exit 0
