# TICKET-TEST-CLUSTER-BENCHMARK-CRASH

**Status**: OPEN (P2)
**Forward-point from**: [TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV](docs/tickets/TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md) forward-point (g) PARTIAL macchina-verifica (2026-07-14 this session)
**Discovered via**: `./tests/chronon3d_content_tests --test-case='*ShapedGlyphLine*'` SIGABRT crash in `test_shaped_glyph_line_cluster_benchmark.cpp`

## Problema

Il test `test_shaped_glyph_line_cluster_benchmark` crasha con SIGABRT dovuto a `std::optional::operator*()` assertion failure:

```
this->_M_is_engaged()
```

Il crash è **TEST-ONLY** (non production code). Root cause probabile: P1-19 migration residual — la migrazione `try_dequeue/enqueue → try_shape` ha lasciato un path dove `std::optional<GlyphRun>` viene dereferenziato senza check `has_value()`.

Il blocco è **TEST-CRASH** (test code bug), non CODE-BLOCK (il [[deprecated]] marker da TICKET-COMPOSITIONDESCRIPTOR-MIGRATION Phase 2 è verificato-working — `cmake --build` exit 0). Il test CRASH è ortogonale al [[deprecated]] marker.

## Scope (1 test case)

| File | Test case | Status |
|---|---|---|
| `tests/content/test_shaped_glyph_line_cluster_benchmark.cpp` | `test_shaped_glyph_line_cluster_benchmark` | SIGABRT (this session) |

## Criteri di accettazione

1. Il test `test_shaped_glyph_line_cluster_benchmark` NON crasha (exit 0 o assertion-fail con messaggio chiaro)
2. Il test copre lo scenario cluster benchmark (misurazione throughput/quality per cluster di glyph runs)
3. La root cause è investigata e documentata in questo ticket-home
4. Zero new public SDK API; zero new singleton/registry/resolver/cache (AGENTS.md Cat-3 minimal-surface)

## Root cause investigation (forward-point)

**Ipotesi A (P1-19 migration residual)**: il test chiama una API che è stata migrata da `try_dequeue/enqueue` a `try_shape` in P1-19. La migrazione potrebbe aver lasciato un path dove il `std::optional<GlyphRun>` ritornato da `try_shape` non viene controllato prima di dereferencing.

**Ipotesi B (benchmark-specific)**: il test cluster benchmark ha un setup specifico (es. glyph runs con cluster window size = 0 o negativo) che triggera un edge case non coperto dal test.

**Ipotesi C (regression post-merge)**: un commit recente ha introdotto un'invariante rotta nel cluster benchmark path. `git log --oneline tests/content/test_shaped_glyph_line_cluster_benchmark.cpp` per identificare il commit sospetto.

## Forward-points

| Forward-point | Status | Chiude quando |
|---|---|---|
| `PHASE-1-ROOT-CAUSE` | OPEN | rg-probe + git log + debugger session per identificare la root cause esatta (ipotesi A/B/C) |
| `PHASE-2-FIX` | OPEN | fix applicato al test (o al production code se la root cause è lì) + test passa |
| `PHASE-3-REGRESSION-LOCK` | OPEN | `tools/check_no_optional_dereference.sh` Cat-4 ancillary gate che faila su `*->*` patterns senza `has_value()` check |

## macchina-verifica (this session, VPS)

- `./tests/chronon3d_content_tests --test-case='*ShapedGlyphLine*'` → 8 test cases | 1 passed | 7 failed | 31 skipped
- 1 of the 7 failures is SIGABRT in `test_shaped_glyph_line_cluster_benchmark.cpp` (the other 6 are FONT-ASSET-BLOCK per TICKET-TEST-FONT-ASSET-PATH)
- macchina-verifica DEFERRED-WBH per test-crash; CLOSED-WBH quando PHASE-2-FIX completa

## Cross-link (this session)

- AGENTS.md §"Fare PR piccole e mirate" (P2 minimal-surface recipe)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- AGENTS.md §honest-limitation (test-crash ≠ code-block; cronaca in ticket-home)
- Parent forward-point: TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md forward-point (g) PARTIAL macchina-verifica
- Sibling forward-points: TICKET-TEST-FONT-ASSET-PATH (P1) + TICKET-CTEST-PER-TESTCASE-REGISTRATION (P2)
- P1-19 migration precedent: `chore(queue): remove legacy try_dequeue/enqueue methods` (the prior chore that migrated the API surface)
- Sibling std::optional handling: TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL (strategy A Phase-1 try_shape factory migration; may have similar std::optional patterns)

## macchina-verifica DEFERRED-WBH

Per AGENTS.md §honest-limitation + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV forward-point (g) PARTIAL classification. Chiusura richiede root cause investigation + fix (PHASE-1-ROOT-CAUSE + PHASE-2-FIX).
