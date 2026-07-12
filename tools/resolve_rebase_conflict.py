#!/usr/bin/env python3
"""
resolve_rebase_conflict.py — structural resolver for git rebase conflict
markers in docs/ markdown files.

Purpose (per AGENTS.md §honesty + TICKET-CHERRY-PICK-RECOVERY protocol):
  - Manual merge: keep BOTH --ours + --theirs blocks
  - Stack the new entries at the top (incoming commit on top of HEAD)
  - NEVER silently drop content (forbidden: `git checkout --ours/--theirs`)

Per-file strategy:
  - Line-by-line state machine (0=normal, 1=ours, 2=theirs)
  - Open marker: `^<<<<<<< (.+)$` (captures the branch/label)
  - Middle marker: `^=======$` (exact 7 equals)
  - Close marker: `^>>>>>>> (.+)$` (captures the branch/label)
  - On close: emit THEIRS first (incoming commit = new entry on top),
    then OURS (existing accumulation). Resets block state, increments
    block counter.
  - BOM (UTF-8) and CRLF preserved byte-for-byte.

Fail-loud (per GATE-MNT-01 spirit):
  - Hard-exit 2 (NOT 1 which is "violation") on:
      * Unterminated marker block (e.g. file ends mid-block)
      * Remaining `<<<<<<<` / `>>>>>>>` text after processing
  - Diagnostic line + column of each remaining marker on stderr.
  - File is NOT modified on failure (caller can re-attempt).

Usage:
  tools/resolve_rebase_conflict.py FILE [FILE...]
Exit codes:
  0 = clean (all marker blocks resolved, no remaining markers)
  2 = fail-loud (unmerged marker, unterminated block, or stragger)

Promotion scope (TICKET-RESOLVE-REBASE-CONFLICT):
  - Was at /tmp/resolve_docs.py (one-shot handcraft)
  - Refinements per code-reviewer (NEEDS FIXES verdict):
      * Exact-marker regex (false-positive guard for prose like
        `<<<<<<< HEAD` written informally in Cat-5 closure rationales)
      * Fail-loud contract (warnings were swallow-and-continue)
      * Redundant double-checks removed
      * Per-file processing (1+ files independently)
  - Regression lock: tests/helpers/selftest_resolve_rebase_conflict.py
    runs 4 PASS + 1 commented FAIL-1 sentinel (pattern mirrors
    7218e14e's tests/helpers/selftest_check_test_hygiene_python.cpp)
"""
import re
import sys


RX_OPEN = re.compile(r"^<<<<<<< (.+?)\s*?$")
RX_MIDDLE = re.compile(r"^=======\s*?$")
RX_CLOSE = re.compile(r"^>>>>>>> (.+?)\s*?$")

GATE_NAME = "resolve_rebase_conflict"


def detect_eol(raw: bytes) -> str:
    """Return '\\r\\n' if CRLF present, else '\\n'."""
    return "\r\n" if b"\r\n" in raw else "\n"


def resolve_file(path: str) -> int:
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except FileNotFoundError:
        print(f"[WARN] {GATE_NAME}: {path} not found; skipping",
              file=sys.stderr)
        return 0

    has_bom = raw.startswith(b"\xef\xbb\xbf")
    if has_bom:
        raw = raw[3:]
    eol = detect_eol(raw)

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        # Preserve original bytes; we can't process as text safely.
        print(f"[FAIL] {GATE_NAME}: {path} is not valid UTF-8 — "
              f"refusing to rewrite binary content", file=sys.stderr)
        return 2

    lines = text.split("\n")  # normalized to LF for parsing
    trailing_newline = text.endswith("\n")

    out: list = []
    ours: list = []
    theirs: list = []
    state = 0  # 0=normal, 1=inside ours, 2=inside theirs
    block_count = 0
    line_no_normalized = 0  # for diagnostics

    # Diagnostic counters for fail-loud
    remaining_open_lines: list = []
    remaining_close_lines: list = []

    for raw_line in lines:
        line_no_normalized += 1
        # Strip the (potential) trailing CR so regex matches cleanly
        line = raw_line.rstrip("\r")

        if state == 0:
            m = RX_OPEN.match(line)
            if m:
                state = 1
                ours, theirs = [], []
                continue
            # post-resolution scan: any leftover marker text in the
            # output buffer (collected from previous blocks) is a fail.
            if raw_line.startswith("<<<<<<<") or raw_line.startswith(">>>>>>>"):
                if raw_line.startswith("<<<<<<<"):
                    remaining_open_lines.append(line_no_normalized)
                else:
                    remaining_close_lines.append(line_no_normalized)
            out.append(raw_line)
        elif state == 1:
            if RX_MIDDLE.match(line):
                state = 2
                continue
            ours.append(raw_line)
        elif state == 2:
            if RX_CLOSE.match(line):
                # THEIRS on top (incoming commit = new entry on top)
                out.extend(theirs)
                out.extend(ours)
                ours, theirs = [], []
                state = 0
                block_count += 1
                continue
            theirs.append(raw_line)

    # Fail-loud on unclosed block
    if state != 0:
        print(f"[FAIL] {GATE_NAME}: {path} has unterminated marker "
              f"block at line {line_no_normalized} (state={state})",
              file=sys.stderr)
        # Don't write the partial output — keep the original file intact.
        return 2

    # Fail-loud on stragger markers emitted during the post-resolution scan
    if remaining_open_lines or remaining_close_lines:
        if remaining_open_lines:
            print(f"[FAIL] {GATE_NAME}: {path} has unmerged open "
                  f"marker(s) at line(s) {remaining_open_lines}",
                  file=sys.stderr)
        if remaining_close_lines:
            print(f"[FAIL] {GATE_NAME}: {path} has unmerged close "
                  f"marker(s) at line(s) {remaining_close_lines}",
                  file=sys.stderr)
        return 2

    # Assemble output preserving original EOL convention
    assembled = eol.join(out)
    if trailing_newline:
        assembled += eol

    # Final byte-level sanity
    if b"<<<<<<<" in assembled.encode("utf-8") or \
       b">>>>>>>" in assembled.encode("utf-8"):
        print(f"[FAIL] {GATE_NAME}: {path} byte-level scan still "
              f"finds marker bytes after processing — refusing to save",
              file=sys.stderr)
        return 2

    final = (b"\xef\xbb\xbf" if has_bom else b"") + assembled.encode("utf-8")
    with open(path, "wb") as f:
        f.write(final)

    print(f"[INFO] {GATE_NAME}: {path} resolved {block_count} block(s) "
          f"clean (eol={'CRLF' if eol == chr(13) + chr(10) else 'LF'}, "
          f"bom={'yes' if has_bom else 'no'})")
    return 0


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        if len(sys.argv) < 2:
            print(f"usage: {sys.argv[0]} FILE [FILE...]", file=sys.stderr)
            return 2
        print(f"{GATE_NAME}: structural rebase conflict resolver.\n"
              f"  Per-file processing; exits 0 on clean, 2 on stragglers.")
        return 0
    rc = 0
    for path in sys.argv[1:]:
        rc |= resolve_file(path)
    return 0 if rc == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
