#!/usr/bin/env python3
# ═══════════════════════════════════════════════════════════════════════════
# tests/architecture/test_render_graph_excludes_software_private.py
#
#   P0-5 — Architecture boundary guard (regression-lock).
#
#   Enforces: `src/render_graph/` MUST NOT include any header located
#   under `src/backends/software/include_private/`.  The OPP-side internals
#   of the software backend live under `src/backends/software/include_private/`
#   (per `docs/CORE_OWNERSHIP.md` — OPP-internal headers are gated to
#   `src/*/include_private/` and are NOT consumed by other modules).
#
#   The render graph (`src/render_graph/`, 125 files at baseline time)
#   talks to the software backend strictly through its PUBLIC surface
#   (`include/chronon3d/backends/software/...` — installed SDK headers +
#   `RenderBackend`-typed trait contract).  Any future attempt to
#   `#include` an OPP-internal header from the render graph will trip
#   this guard with exit 1, locking the boundary independently of
#   compiler flags.
#
#   Detection: substring `backends/software/include_private/` on any
#   `#include` line — this covers all current and future path styles
#     * `"../../src/backends/software/include_private/foo.hpp"`  (relative)
#     * `"backends/software/include_private/foo.hpp"`          (sibling)
#     * `<chronon3d/backends/software/include_private/foo.hpp>` (public-style leak)
#
#   Self-test on current main@<head>: PASS (0 violations; the
#   `include_private/` directory does not yet exist, so the rule is a
#   forward-looking sentinel).
#
#   Wired into CTest via `tests/architecture_tests.cmake` as
#   `chronon3d_architecture_render_graph_software_boundary`.
#   Pattern precedent: `test_render_session_includes_boundary.py`.
# ═══════════════════════════════════════════════════════════════════════════

import os
import sys

REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '..', '..'))

GRAPH_DIR = os.path.join(REPO_ROOT, 'src', 'render_graph')
FORBIDDEN_SUBSTR = 'backends/software/include_private/'
# File extensions under `src/render_graph/` subject to the boundary
# (C++ translation units + headers + inline impls).
SCANNED_EXTENSIONS = ('.cpp', '.hpp', '.inl', '.h', '.c')


def main() -> int:
    if not os.path.isdir(GRAPH_DIR):
        # The boundary predicate applies to `src/render_graph/`.  If the
        # directory is absent, the rule cannot fire — surface a SKIP so
        # the absence is loud and inspectable rather than silently green.
        print(f"SKIP: {os.path.relpath(GRAPH_DIR, REPO_ROOT)} not found",
              file=sys.stderr)
        return 0

    errors = 0
    files_scanned = 0
    for root, _dirs, files in os.walk(GRAPH_DIR):
        for filename in files:
            if not filename.endswith(SCANNED_EXTENSIONS):
                continue
            files_scanned += 1
            filepath = os.path.join(root, filename)
            try:
                with open(filepath, 'r', encoding='utf-8') as f:
                    contents = f.read()
            except (OSError, UnicodeDecodeError):
                # Binary / unreadable — skip silently (not an include
                # boundary violation).
                continue
            for line_no, line in enumerate(contents.splitlines(), 1):
                s = line.strip()
                if not s.startswith('#include'):
                    continue
                if FORBIDDEN_SUBSTR not in s:
                    continue
                rel = os.path.relpath(filepath, REPO_ROOT)
                print(f"VIOLATION: {rel}:{line_no} -> {FORBIDDEN_SUBSTR!r}")
                print(f"  {s}")
                errors += 1

    if errors > 0:
        print(
            f"FAIL: {errors} render-graph -> software-include_private "
            f"violation(s) across {files_scanned} scanned files "
            f"in src/render_graph/.",
            file=sys.stderr,
        )
        return 1

    print(
        f"OK: render_graph -> backends/software/include_private boundary "
        f"holds (errors=0; {files_scanned} files scanned).",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
