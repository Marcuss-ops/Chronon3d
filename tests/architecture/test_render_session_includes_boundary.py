#!/usr/bin/env python3
"""Architecture include-graph boundary guard.

Surfacing known cross-layer deps; docs/refactor-roadmap/07 and 08 track
the TICKETs responsible for each allowed violation.
"""
import os
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

CHECKS = [
    ('include/chronon3d/runtime/render_session.hpp',
     ['backends/software/', 'render_graph/']),
    ('include/chronon3d/render_graph/executor/graph_executor.hpp',
     ['backends/software/']),
    ('include/chronon3d/render_graph/render_graph_context.hpp',
     ['backends/software/']),
]

# substr -> (TICKET, justification). Empty dict => no exceptions.
KNOWN_VIOLATIONS = {
    'include/chronon3d/runtime/render_session.hpp': {
        'scene_hasher.hpp':
            ('TICKET-013', 'scene_hasher leaks into RenderSession; tracks in roadmap WP-8.'),
        'software_session_resources.hpp':
            ('TICKET-014', 'session_resources compositing in render_session.hpp; roadmap WP-8.'),
    },
}


def check_file(filepath, forbidden_substrings, allowlist):
    if not os.path.isfile(filepath):
        print(f"SKIP: {filepath} not found", file=sys.stderr)
        return 0
    errors = 0
    with open(filepath) as f:
        for line_no, line in enumerate(f, 1):
            s = line.strip()
            if not s.startswith("#include"):
                continue
            for forbid in forbidden_substrings:
                if forbid not in s:
                    continue
                ticket = why = None
                for allow_key, (t, w) in allowlist.items():
                    if allow_key in s:
                        ticket, why = t, w
                        break
                if ticket:
                    print(f"INFO: {filepath}:{line_no} -> {forbid!r} ({ticket}: {why})")
                else:
                    print(f"VIOLATION: {filepath}:{line_no} -> {forbid!r}\n  {s}")
                    errors += 1
    return errors


def main():
    rc = 0
    for relpath, forbids in CHECKS:
        allow = KNOWN_VIOLATIONS.get(relpath, {})
        rc |= check_file(os.path.join(REPO_ROOT, relpath), forbids, allow)
    if rc:
        sys.exit(1)
    print("OK: include-graph boundary invariants satisfied (errors=0).")


if __name__ == "__main__":
    main()
