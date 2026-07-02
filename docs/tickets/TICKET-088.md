# TICKET-088 \u2014 audit_v3 phantom-path triage (TICKET-GATE-10-PHASE-4-FULL escalation)

| Field      | Value |
|------------|-------|
| ID         | TICKET-088 |
| Priorit\u00e0   | P2    |
| Area       | audit_v3 phantom-path triage (TICKET-GATE-10-PHASE-4-FULL) |
| Stato      | TRACKED |
| Wrapper    | `include/chronon3d/math/glm/gtc/type_ptr.hpp` |
| Upstream   | `` |
| Categoria  | I. Third-party Vendoring Policy (`docs/ARCHITECTURE_AUDIT.md`) |

## Diagnosi

`/tmp/audit_v3.py` flagged `include/chronon3d/math/glm/gtc/type_ptr.hpp` as missing from `cmake/Chronon3DPublicHeaders.cmake`.
Filesystem-existence check: ABSENT.  The path is an upstream-vendored submodule (``),
referenced by OPP-side code via `#include <math/glm/gtc/type_ptr.hpp>` resolve.

## Triage decision

**Category (iii) \u2014 document as third-party vendoring policy in `docs/ARCHITECTURE_AUDIT.md`.**

Rationale:

* Path doesn't exist in `include/chronon3d/`; cannot move (i) nor install via vcpkg (ii).
* `vcpkg.json` already declares `` (or feature opt-in); include resolution is canonical upstream.
* Adding to public manifest would be a categorical error (treating an upstream as Chronon-owned).
* .

## Acceptance criteria

* `docs/ARCHITECTURE_AUDIT.md` contains a row in \u201cI. Third-party Vendoring Policy\u201d referencing this TICKET.
* `audit_v3.py` no longer reports this path as missing (false positive suppression \u2014 TICKET-095 audit, separate).
* `tools/install_consumer_test.sh` Phase 4 does not regress on this subheader.

## Note

* Sub-ticket of `TICKET-GATE-10-PHASE-4-FULL`.  Distinct from TICKET-111 (OPP-side text rot) and TICKET-080 (install_consumer_test env precond).
