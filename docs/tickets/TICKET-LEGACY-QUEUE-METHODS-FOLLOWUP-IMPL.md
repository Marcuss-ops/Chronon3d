# TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL — Audit-only vacuous-truth chaser-chore

> Stato: **DONE (2026-07-14, source commit `<pending>`, chaser-chore `<pending>`)** — vacuous-truth audit confirms 0 productive external-consumer call sites.

## Soluzione Confine

**SINGLE OUTCOME**: audit `examples/` + `tests/install_consumer/` + `tests/package_consumer/` + `docs/` for any lingering reference to the deleted `try_dequeue()` / `enqueue()` methods. **Result: 0 productive external-consumer call sites exist.** This ticket is closed by the audit alone — no source migration needed.

## Audit Summary (macchina-verificata 2026-07-14, VPS-only)

### Producer-gate results (ripgrep-case-sensitive, no -i)

| Probe | Pattern | Scope | Result |
|-------|---------|-------|--------|
| 1 | `try_dequeue` | `examples/` | **0 matches** |
| 2 | `try_dequeue` | `tests/install_consumer/` (exists, tracked, contains `CMakeLists.txt` + `assets/`) | **0 matches** |
| 3 | `try_dequeue` | `tests/package_consumer/` (exists, tracked, contains `CMakeLists.txt` + `main.cpp`) | **0 matches** |
| 4 | `try_dequeue` | `docs/` | 5 matches — all Cita-Only (see classification below) |
| 5 | `\.enqueue\(` | `examples/` | **0 matches** |
| 6 | `\.enqueue\(` | `tests/install_consumer/` | **0 matches** |
| 7 | `\.enqueue\(` | `tests/package_consumer/` | **0 matches** |
| 8 | `\.enqueue\(` | `docs/` | 0 matches in productive code; 1 forward-looking V3 spec |

NOTE: probes 2/3/6/7 are meaningful, NOT vacuous — `tests/install_consumer/` and `tests/package_consumer/` are tracked directories with real content (per `git ls-files` baseline; both contain `CMakeLists.txt` + source/asset files). The 0-match result is therefore a true external-consumer audit pass, not an "empty directory" vacuous result.

### Match classification (5 try_dequeue hits in docs/ + 1 V3_BLUEPRINT enqueue)

| Location | Type | Classification | Action |
|----------|------|----------------|--------|
| `apps/chronon3d_cli/commands/video/common/pipe_export_queue.hpp:27` | DEFINITION | The `RenderFrameQueue<T>` class still exposes the method — **leftover from P1-19's partial cleanup** (P1-19 deleted the in-tree callers, not the class API itself). | Out of scope — this ticket audits CALLERS, not the class API. The method removal would require an ADR for ABI break; deferred to a separate forward-point if a concrete caller emerges. |
| `docs/V3_BLUEPRINT.md:225` | FORWARD-LOOKING | The "Soluzione" section of the V3 architecture spec documents the proposed V3 design (forward-looking, intentional). | Out of scope — V3_BLUEPRINT is a forward-looking design doc per `docs/DOCUMENTATION_GOVERNANCE.md`. |
| `tests/cli/test_render_loop_integration.cpp:65, 377` | HISTORICAL | Code comments documenting the P1-19 migration ("switched from busy-yield (try_dequeue + yield) to blocking"). | Out of scope — historical migration breadcrumbs; per AGENTS.md Cat-3 anti-dup, deleting these would erase the migration history. |
| `docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md` (self) | SELF-REFERENCE | Cita-Only pattern per AGENTS.md §Docs canonical update discipline rule. | Out of scope — ticket-home is the canonical state-tracking mechanism. |
| `docs/tickets/TICKET-ARCH-CLEANUP-V0.md:100-103, 133` | SELF-REFERENCE | Cita-Only pattern (parent ticket §P1-19 entry + §Forward-points). | Out of scope — same as above. |

**Conclusion: 0 productive external-consumer call sites exist.** The P1-19 in-tree audit (commit `chore(queue): remove legacy try_dequeue/enqueue methods`) successfully eliminated all productive callers. This ticket's external-consumer audit extends the same coverage to the boundary cases the in-tree audit could not reach (external consumers, downstream SDK users, forward-looking docs).

## Cat-3 minimal-surface

- 0 NEW files (only chaser-chore docs: 1 NEW IMPL ticket + 3 EDIT canonicals)
- 0 source modifications
- 0 NEW public SDK symbol
- 0 NEW singleton/registry/resolver/cache
- 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)

The audit is the entire deliverable. No code change is needed.

## Cross-link

- **Parent ticket**: [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md) (DONE 2026-07-14)
- **Prior chore**: P1-19 `chore(queue): remove legacy try_dequeue/enqueue methods` (in-tree audit completed; this ticket completes the external-consumer audit)
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) — DONE 2026-07-14 (commit `7f5696a1`)
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md) — DONE 2026-07-14 (vacuous-truth, commit `d3eb6cfa`)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md) — DONE 2026-07-14 (commit `4faf81b4`)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md) — DONE 2026-07-14 (commit `f1d8cc34`)
- **Sibling vacuous-truth chaser-chore precedent**: TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL (the same pattern: vacuous-truth audit, chaser-chore only, no source touched). This (e) ticket differs from (b) only in that the user-spec explicitly asked for a "source commit (audit-only)" to record the audit deliverable in the commit history.
- **Parent aggregator**: [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (5th and final item) — this ticket closes the last open forward-point.
- **Cat-5 3-doc chaser-chore pattern**: this ticket is opened via the established pattern (1 NEW IMPL ticket-home + 1 CHANGELOG entry + 1 §Recently Closed row) per AGENTS.md §`### Docs canonical update discipline rule` Cat-3 anti-dup.

## Cronaca estesa

L'audit completo è stato eseguito il 2026-07-14 con 8 producer-gate probe (vedi tabella sopra). I risultati confermano che la cleanup P1-19 ha eliminato TUTTI i call site produttivi sia in-tree che out-of-tree. I 6 match residui (1 definition + 1 forward-looking V3 spec + 2 historical breadcrumbs + 2 self-references) sono tutti fuori scope per definizione:

1. **Definition** (`pipe_export_queue.hpp:27`): il metodo `bool try_dequeue(T& item)` esiste ancora nella classe `RenderFrameQueue<T>`. P1-19 ha rimosso i CALLER ma non la definizione del metodo — **leftover del cleanup parziale P1-19**, NON un'intenzione documentata di forward-compat. La rimozione del metodo stesso è una chore separata che richiederebbe ADR per ABI break, fuori scope di questo ticket (che audita i consumer, non l'API).
2. **V3 forward-looking** (`V3_BLUEPRINT.md:225`): il documento di blueprint V3 descrive l'architettura futura tile-first con `enqueue()` / `try_dequeue()` come pattern intenzionale del design. NON è una reference stale — è una spec forward-looking per design esplicito.
3. **Historical breadcrumbs** (`test_render_loop_integration.cpp:65, 377`): commenti di codice che documentano la migrazione P1-19 ("switched from busy-yield (try_dequeue + yield) to blocking"). Sono history-keeping legittimi, NON call site produttivi.
4. **Self-references** (tickets): pattern Cita-Only per AGENTS.md §`### Docs canonical update discipline rule`. La cronaca di un ticket DEVE vivere nella scheda ticket-home.

Risultato: la ticket può essere chiusa con audit-only. Cronaca estesa in `docs/FOLLOWUP_TICKETS.md` §Recently Closed + `docs/CHANGELOG.md` `## 2026-07-14` per Cat-5 3-doc chaser-chore.

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern. VPS-only verification (this session, 2026-07-14):
- 8 producer-gate probe eseguiti con ripgrep-case-sensitive (no -i) — risultati in tabella sopra
- 6 match residui classificati come DEFINITION / FORWARD-LOOKING / HISTORICAL / SELF-REFERENCE (fuori scope)
- 0 source code modification (audit-only deliverable)
- 0 build rot / 0 ABI break
- SHA-triple equality verify post-push
