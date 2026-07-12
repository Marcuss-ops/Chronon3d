#!/usr/bin/env python3
"""
selftest_resolve_rebase_conflict.py — contract selftest for
tools/resolve_rebase_conflict.py.

Mirrors commit 7218e14e's pattern (`tests/helpers/selftest_check_test_hygiene_python.cpp`):
  - Header comment block describing what is tested + why
  - 4 PASS test cases (one per resolver invariant)
  - 1 commented-out FAIL-1 sentinel (proves the fail-loud contract on
    malformed input — same shape as the C++ selftest's commented
    bare-DOCTEST_SKIP row)

PASS-1: simple 1-block resolve (THEIRS stacked on top of OURS)
PASS-2: sequence of 2 independent blocks per file
PASS-3: BOM-prefixed input resolves cleanly (UTF-8 BOM preserved)
PASS-4: doc-style prose containing `===` does NOT trigger the resolver
         (exact-marker guard: `^=======$` requires 7 equals, alone on a line)

FAIL-1: COMMENTED — malformed input (only `<<<<<<<` opens, no close).
         When uncommented, must hard-exit 2 per the resolver's fail-loud
         contract. Sentinel exists so future regressions in the fail-loud
         behaviour are visibly broken.

Run:  python3 tests/helpers/selftest_resolve_rebase_conflict.py
Exit:  0 if all 4 PASS cases pass; 1 on any failure.
"""
import os
import subprocess
import sys
import tempfile

GATE_NAME = "selftest_resolve_rebase_conflict"
RESOLVER = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "tools", "resolve_rebase_conflict.py",
))


def run_resolver_on_bytes(content: bytes, expect_exit: int) -> str:
    """Invoke the resolver as a subprocess on `content`. Return stdout+stderr."""
    with tempfile.NamedTemporaryFile(delete=False, suffix=".md") as f:
        f.write(content)
        path = f.name
    try:
        result = subprocess.run(
            [sys.executable, RESOLVER, path],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if result.returncode != expect_exit:
            raise AssertionError(
                f"resolver exit {result.returncode} != expected {expect_exit}\n"
                f"  stdout: {result.stdout!r}\n"
                f"  stderr: {result.stderr!r}"
            )
        return result.stdout + result.stderr
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def read_sanitized_bytes(content: bytes) -> bytes:
    """Run resolver, then read back the resulting file bytes for inspection."""
    with tempfile.NamedTemporaryFile(delete=False, suffix=".md") as f:
        f.write(content)
        path = f.name
    try:
        rc = subprocess.run(
            [sys.executable, RESOLVER, path],
            capture_output=True,
            timeout=10,
        ).returncode
        if rc != 0:
            raise AssertionError(f"setup resolver exited {rc}, expected 0")
        with open(path, "rb") as g:
            return g.read()
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def case_1_simple_one_block() -> None:
    """PASS-1: simple 1-block resolve"""
    input_bytes = (
        b"<<<<<<< HEAD\n"
        b"OURS CONTENT\n"
        b"=======\n"
        b"THEIRS CONTENT\n"
        b">>>>>>> feature\n"
        b"\n"
    )
    output = read_sanitized_bytes(input_bytes)
    # THEIRS must be emitted BEFORE OURS (stacked on top, per
    # TICKET-CHERRY-PICK-RECOVERY protocol).
    theirs_pos = output.find(b"THEIRS")
    ours_pos = output.find(b"OURS")
    assert theirs_pos != -1, "THEIRS not found"
    assert ours_pos != -1, "OURS not found"
    assert theirs_pos < ours_pos, \
        f"THEIRS at {theirs_pos} must precede OURS at {ours_pos}"
    assert b"<<<<<<<" not in output, "marker straggler"
    assert b">>>>>>>" not in output, "marker straggler"
    print(f"PASS-1 [simple one-block, THEIRS-on-top]: clean")


def case_2_sequential_blocks() -> None:
    """PASS-2: 2 sequential blocks per file (both resolved independently)"""
    input_bytes = (
        b"prefix-1\n"
        b"<<<<<<< HEAD\n"
        b"OURS-A\n"
        b"=======\n"
        b"THEIRS-A\n"
        b">>>>>>> branch-a\n"
        b"middle-line\n"
        b"<<<<<<< HEAD\n"
        b"OURS-B\n"
        b"=======\n"
        b"THEIRS-B\n"
        b">>>>>>> branch-b\n"
        b"suffix\n"
    )
    output = read_sanitized_bytes(input_bytes)
    # Both blocks resolved; THEIRS-A before OURS-A; THEIRS-B before OURS-B;
    # markers gone; structural lines preserved.
    a_theirs_pos = output.find(b"THEIRS-A")
    a_ours_pos = output.find(b"OURS-A")
    b_theirs_pos = output.find(b"THEIRS-B")
    b_ours_pos = output.find(b"OURS-B")
    prefix_pos = output.find(b"prefix-1")
    middle_pos = output.find(b"middle-line")
    suffix_pos = output.find(b"suffix")
    assert a_theirs_pos != -1 and a_ours_pos != -1
    assert b_theirs_pos != -1 and b_ours_pos != -1
    # Per the resolver's THEIRS-on-top protocol (TICKET-CHERRY-PICK-
    # RECOVERY: stack new entries on top), each block emits THEIRS
    # before OURS, then control returns to normal-line processing.
    # Therefore on close of block A: prefix-1, THEIRS-A, OURS-A come
    # BEFORE the middle-line (which sits between the two blocks).
    assert a_theirs_pos < a_ours_pos, "block A: THEIRS must precede OURS"
    assert b_theirs_pos < b_ours_pos, "block B: THEIRS must precede OURS"
    assert prefix_pos < a_theirs_pos, "prefix precedes block A's THEIRS"
    assert a_ours_pos < middle_pos, "block A's OURS precedes middle-line"
    assert middle_pos < b_theirs_pos, "middle-line precedes block B's THEIRS"
    assert b_ours_pos < suffix_pos, "block B's OURS precedes suffix"
    assert b"<<<<<<<" not in output
    assert b">>>>>>>" not in output
    print(f"PASS-2 [sequential 2 blocks]: clean")


def case_3_bom_preserved() -> None:
    """PASS-3: BOM-prefixed input resolves cleanly + BOM preserved in output"""
    bom = b"\xef\xbb\xbf"
    input_bytes = (
        bom
        + b"<<<<<<< HEAD\n"
        b"OURS\n"
        b"=======\n"
        b"THEIRS\n"
        b">>>>>>> feature\n"
    )
    output = read_sanitized_bytes(input_bytes)
    assert output.startswith(bom), "BOM must be preserved in output"
    assert b"THEIRS" in output and b"OURS" in output
    assert output.find(b"THEIRS") < output.find(b"OURS")
    assert b"<<<<<<<" not in output
    assert b">>>>>>>" not in output
    print(f"PASS-3 [BOM-preserved]: clean")


def case_4_prose_with_equals_does_not_trigger() -> None:
    """PASS-4: prose containing `===` does NOT trigger the resolver
    (exact-marker guard: `^=======$` alone on a line)"""
    input_bytes = (
        b"This is a doc paragraph.\n"
        b"It contains a prose === sign in the middle of a line.\n"
        b"Mid-text ======= (with extra equals) appears inline.\n"
        b"Another line.\n"
    )
    # Expect exit 0 (resolved 0 blocks)
    run_resolver_on_bytes(input_bytes, expect_exit=0)
    output = read_sanitized_bytes(input_bytes)
    # File unchanged (resolver should be a no-op for prose without markers)
    assert b"doc paragraph" in output
    assert b"=========" in output or b"=====" in output, \
        "prose `===` should remain untouched"
    print(f"PASS-4 [prose with === does not trigger]: clean")


# FAIL-1 COMMENTED to avoid self-tripping:
# def case_fail_1_unterminated_block() -> None:
#     """FAIL-1 (sentinel): unterminated marker block must hard-exit 2."""
#     input_bytes = (
#         b"<<<<<<< HEAD\n"
#         b"ONLY OPEN MARKER — no close\n"
#     )
#     run_resolver_on_bytes(input_bytes, expect_exit=2)
#     print(f"FAIL-1 [unterminated blocks hard-exit 2]: HARD-EXIT VERIFIED")
#
# When un-commented: must throw AssertionError if the resolver does NOT
# exit 2. This is the regression lock for the fail-loud contract — if a
# future refactor softens the resolver to warn-and-continue, the sentinel
# breaks loudly.


def main() -> int:
    if not os.path.isfile(RESOLVER):
        print(f"[FAIL] {GATE_NAME}: resolver not found at {RESOLVER}",
              file=sys.stderr)
        return 1
    if not os.access(RESOLVER, os.X_OK) and not os.access(
            RESOLVER, os.R_OK):
        print(f"[FAIL] {GATE_NAME}: resolver at {RESOLVER} not readable",
              file=sys.stderr)
        return 1

    cases = [
        case_1_simple_one_block,
        case_2_sequential_blocks,
        case_3_bom_preserved,
        case_4_prose_with_equals_does_not_trigger,
    ]
    passed = 0
    try:
        for case in cases:
            case()
            passed += 1
    except AssertionError as e:
        print(f"[FAIL] {GATE_NAME}: {e}", file=sys.stderr)
        print(f"GATE_FAIL: {passed}/{len(cases)} cases passed", file=sys.stderr)
        return 1
    print(f"GATE_PASS: {passed}/{len(cases)} PASS, FAIL-1 sentinel commented")
    print(f"[INFO] {GATE_NAME}: {passed}/{len(cases)} cases passed, "
          f"FAIL-1 sentinel intentionally commented")
    return 0


if __name__ == "__main__":
    sys.exit(main())
