#!/usr/bin/env python3
"""Behavioural tests for `tools/gates/check_steady_state_allocations.sh`.

The gate used to certify the committed baseline no matter which revision it came
from (fixes.md B.4: the baseline pins `source_revision=8bdf0f807` while HEAD is a
different commit).  These tests pin the contract that replaced it:

  * a bound revision passes and is disclosed in the `GATE_PASS` line;
  * a mismatch or an absent revision warns by default (the chain stays green on
    the historical baseline) and fails when the strict env var is set;
  * value/threshold and internal-error exit codes are unchanged.

Pure stdlib, runs on Python 3.9+, wired from `tests/CMakeLists.txt`.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "tools" / "gates" / "check_steady_state_allocations.sh"
REVISION = "abc123def4567890"

GATE_PASS, GATE_FAIL, GATE_INTERNAL = 0, 1, 2


def _report(revision=None, value="0.0"):
    lines = ["schema=chronon3d.phase5.steady-state-allocations.v1"]
    if revision is not None:
        lines.append("source_revision=%s" % revision)
    lines.append("steady_state_allocations_per_frame=%s" % value)
    return "\n".join(lines) + "\n"


def _run(report_text, expected_revision=REVISION, **extra_env):
    """Run the gate on a temp report. `expected_revision=None` lets it use HEAD."""
    with tempfile.TemporaryDirectory() as tmp:
        report = Path(tmp) / "report.txt"
        if report_text is not None:
            report.write_text(report_text, encoding="utf-8")
        env = {
            key: value
            for key, value in os.environ.items()
            if not key.startswith("CHRONON_STEADY_STATE")
        }
        if expected_revision is not None:
            env["CHRONON_STEADY_STATE_REVISION"] = expected_revision
        env["CHRONON_STEADY_STATE_REPORT"] = str(report)
        env.update({key: str(value) for key, value in extra_env.items()})
        proc = subprocess.run(
            ["bash", str(GATE)], capture_output=True, text=True, env=env
        )
        return proc.returncode, proc.stdout, proc.stderr


def _head_revision():
    try:
        proc = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    return proc.stdout.strip() or None


def test_matching_revision_passes_and_is_disclosed():
    code, out, _ = _run(_report(revision="abc123"))
    assert code == GATE_PASS, "expected PASS, got %d" % code
    assert "revision_state=bound" in out, out
    assert "revision=abc123" in out, out


def test_full_sha_also_binds():
    code, out, _ = _run(_report(revision=REVISION))
    assert code == GATE_PASS, "expected PASS, got %d" % code
    assert "revision_state=bound" in out, out


def test_shorter_report_revision_binds_via_prefix():
    code, out, _ = _run(_report(revision="abc12"), expected_revision="abc123def4")
    assert code == GATE_PASS, "expected PASS, got %d" % code
    assert "revision_state=bound" in out, out


def test_revision_mismatch_warns_but_does_not_fail_by_default():
    code, _, err = _run(_report(revision="deadbeefcafe"))
    assert code == GATE_PASS, "a historical baseline must not break the chain"
    assert "GATE_WARN" in err, err
    assert "not evidence about the current revision" in err, err


def test_revision_mismatch_is_fatal_when_required():
    code, _, err = _run(
        _report(revision="deadbeefcafe"), CHRONON_STEADY_STATE_REQUIRE_REVISION=1
    )
    assert code == GATE_FAIL, "expected FAIL, got %d" % code
    assert "GATE_FAIL" in err and "revision_state=mismatch" in err, err


def test_missing_revision_warns_by_default_and_fails_when_required():
    code, out, err = _run(_report(revision=None))
    assert code == GATE_PASS, "expected PASS, got %d" % code
    assert "revision_state=unbound" in out, out
    assert "GATE_WARN" in err, err

    code, _, err = _run(
        _report(revision=None), CHRONON_STEADY_STATE_REQUIRE_REVISION=1
    )
    assert code == GATE_FAIL, "expected FAIL, got %d" % code
    assert "revision_state=unbound" in err, err


def test_head_revision_binds_without_explicit_env():
    head = _head_revision()
    if head is None:
        print("  SKIP: git unavailable — HEAD-default binding not checked")
        return False
    code, out, _ = _run(_report(revision=head), expected_revision=None)
    assert code == GATE_PASS, "expected PASS, got %d" % code
    assert "revision_state=bound" in out, out
    return True


def test_allocation_budget_still_fails_above_threshold():
    code, _, err = _run(_report(revision="abc123", value="3.0"))
    assert code == GATE_FAIL, "expected FAIL, got %d" % code
    assert "value=3.0 threshold=0" in err, err


def test_threshold_override_is_honoured():
    code, out, _ = _run(
        _report(revision="abc123", value="3.0"), CHRONON_STEADY_STATE_THRESHOLD=5
    )
    assert code == GATE_PASS, "expected PASS, got %d" % code
    assert "threshold=5" in out, out


def test_missing_report_is_an_internal_error():
    code, _, err = _run(None)
    assert code == GATE_INTERNAL, "expected exit 2, got %d" % code
    assert "GATE_FAIL_INTERNAL" in err, err


if __name__ == "__main__":
    test_matching_revision_passes_and_is_disclosed()
    test_full_sha_also_binds()
    test_shorter_report_revision_binds_via_prefix()
    test_revision_mismatch_warns_but_does_not_fail_by_default()
    test_revision_mismatch_is_fatal_when_required()
    test_missing_revision_warns_by_default_and_fails_when_required()
    head_bound = test_head_revision_binds_without_explicit_env()
    test_allocation_budget_still_fails_above_threshold()
    test_threshold_override_is_honoured()
    test_missing_report_is_an_internal_error()
    print(
        "steady state allocation gate tests: PASS (HEAD-default binding: %s)"
        % ("ran" if head_bound else "skipped")
    )
    sys.exit(0)
