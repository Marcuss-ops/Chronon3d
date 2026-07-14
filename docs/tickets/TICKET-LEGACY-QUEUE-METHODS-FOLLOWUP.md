# TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP — Audit external consumers for deleted `try_dequeue()` / `enqueue()`

> Stato: **DONE (2026-07-14, source commit `d62c044c10bd3af1bc3a86591254e43870d069a2`, chaser-chore `616d8c5afe03e52bee40ad0c3e4aebadfaeb5481`)** — vacuous-truth audit: 0 productive external-consumer call sites found in `examples/` + `tests/install_consumer/` + `tests/package_consumer/` + `docs/`. Implementation landed via [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL.md). 0 source modifications; audit-only deliverable. The 5 forward-points of TICKET-ARCH-CLEANUP-V0 are now ALL DONE.

## Problema

The P1-19 cleanup migrated the in-tree tests + call sites from `try_dequeue()` / `enqueue()` (non-blocking, test-only) to `push()` / `pop()` / `close()` (production API). The two legacy methods were deleted from the queue's public surface.

**However**, the in-tree audit only covers `src/` + `apps/` + `tests/`. The external consumer audit is missing:
- `examples/` directory: any example consumer (e.g., `examples/getting_started/`, `examples/text_export_consumer/`) that uses the legacy methods
- Downstream SDK users: any `tests/install_consumer/` or `tests/package_consumer/` site that imports the SDK and uses the legacy methods
- Documentation: any `docs/` mention of the legacy methods (the CHANGELOG entries + ADR mentions that say "removed" are fine; anything that says "use try_dequeue" is stale)

If an external consumer still references the deleted methods, the SDK release will break the consumer's build (a Cat-3 SDK ABI break). The P1-19 audit was in-tree only; the cross-cutting external audit was deferred.

## Soluzione Confine

**SINGLE OUTCOME**: audit `examples/` + downstream-consumer paths + docs/ for any lingering reference to the deleted `try_dequeue()` / `enqueue()` methods. If any are found, migrate them to `push()` / `pop()` / `close()`.

Steps:
1. **Audit** `examples/`: `rg "\\.try_dequeue\\(|\\.enqueue\\(" examples/` → 0 matches
2. **Audit** downstream consumers: `rg "\\.try_dequeue\\(|\\.enqueue\\(" tests/install_consumer/ tests/package_consumer/` → 0 matches
3. **Audit** docs/: `rg "try_dequeue|\\.enqueue\\(" docs/` → 0 matches (excluding CHANGELOG entries that say "removed" + the deletion commit message)
4. **Migrate** any found reference: `enqueue(x)` → `push(x)`; `try_dequeue()` (non-blocking) → `try_pop()` if the queue exposes it, else restructure to use `pop()` with a timeout or `try_pop_for(timeout)`
5. **Re-bake** any example output that depends on the migrated example (e.g., `examples/getting_started/output.mp4` may need regeneration if the example was video-related)

## Criteri di accettazione

- [ ] `rg "\\.try_dequeue\\(" examples/ tests/install_consumer/ tests/package_consumer/ docs/` → 0 matches
- [ ] `rg "\\.enqueue\\(" examples/ tests/install_consumer/ tests/package_consumer/ docs/` → 0 matches
- [ ] `rg "try_dequeue" docs/CHANGELOG.md` → matches ONLY in P1-19 commit message + the related ticket body (Cita-Only pattern)
- [ ] `rg "enqueue" docs/CHANGELOG.md` → matches ONLY in unrelated context (e.g., HTTP request enqueue, audio sample enqueue) — NOT in queue-method context
- [ ] If any external consumer is found: migrated to `push()` / `pop()` / `close()` and verified by example output re-bake
- [ ] 0 new public SDK symbol (audit + possible migration, no surface-additive)
- [ ] 0 new singleton/registry/resolver/cache
- [ ] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern. VPS-only verification:
- `rg "\\.try_dequeue\\(" examples/ tests/install_consumer/ tests/package_consumer/ docs/` → 0 matches
- `rg "\\.enqueue\\(" examples/ tests/install_consumer/ tests/package_consumer/ docs/` → 0 matches
- If any matches: migrate + re-run example + verify output identical (or document the diff)

## Cat-3 minimal-surface

- 0 NEW files
- 0 NEW public SDK symbol
- 0–N EDIT external consumer files (where N is the number of found references; expected N=0)
- 0 NEW singleton/registry/resolver/cache

NO surface-additive change. Pure audit + possible-migration per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent ticket**: [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (5th item)
- **Prior chore**: commit (P1-19 `chore(queue): remove legacy try_dequeue/enqueue methods`) — in-tree audit done; this ticket completes the external-consumer audit
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md)
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md)
- **Cat-5 3-doc chaser-chore pattern**: this ticket is opened via the same pattern (1 NEW ticket + 1 CHANGELOG entry + 1 §Open Blockers row)
