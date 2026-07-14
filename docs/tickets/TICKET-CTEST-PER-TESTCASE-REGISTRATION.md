# TICKET-CTEST-PER-TESTCASE-REGISTRATION

**Status**: OPEN (P2)
**Forward-point from**: [TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV](docs/tickets/TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md) forward-point (g) PARTIAL macchina-verifica (2026-07-14 this session)
**Discovered via**: `ctest -R ShapedGlyphLine*` returned 0 despite 23 ShapedGlyphLine test cases in `chronon3d_content_tests` binary

## Problema

`ctest` registra il test binary come un singolo test, non i singoli `TEST_CASE` doctest al suo interno. Conseguenza:

```
$ ctest -N -R ShapedGlyphLine
Total Tests: 0
```

…nonostante il binary contenga 23 test case ShapedGlyphLine:

```
$ ./tests/chronon3d_content_tests --list-test-cases | rg -c -i 'shaped'
23
```

Il blocco è **CTEST-REGISTRATION-GAP** (CMake/CTest configuration), non CODE-BLOCK (il [[deprecated]] marker da TICKET-COMPOSITIONDESCRIPTOR-MIGRATION Phase 2 è verificato-working — `cmake --build` exit 0).

## Scope (all test files using TEST_CASE pattern)

| Area | Test binary | Test cases impattati |
|---|---|---|
| Content | `chronon3d_content_tests` | 23 ShapedGlyphLine + N altri (forward-point: audit completo) |
| Core | `chronon3d_core_tests` | TBD (audit completo) |
| Cache | `chronon3d_cache_tests` | TBD |
| Text | `chronon3d_text_*_tests` | TBD |

## Criteri di accettazione

1. `ctest -N -R ShapedGlyphLine` elenca i 23 test case ShapedGlyphLine individualmente (NON solo il binary)
2. `ctest -R ShapedGlyphLine` esegue i 23 test case e riporta pass/fail per ciascuno
3. La soluzione è backwards-compatible: i test binary esistenti continuano a funzionare via direct invocation (`./test_binary --test-case=...`)
4. Zero new public SDK API; zero new singleton/registry/resolver/cache (AGENTS.md Cat-3 minimal-surface)

## Soluzione (forward-point)

**Opzione A (canonical, doctest-native)**: usare `doctest_discover_tests()` da `cmake/FindDoctest.cmake` (o equivalente) per auto-discover i TEST_CASE al configure time. Questo richiede che doctest supporti un mode di listing compatibile con CMake (flag `--list-test-cases` o `--list-test-suites`).

**Opzione B (explicit add_test)**: per ogni test binary, generare una lista di `add_test(NAME <test_case> COMMAND <binary> --test-case=<name>)` via CMake function che chiama il binary con `--list-test-cases` e parsa l'output. Più verboso ma funziona con qualsiasi versione di doctest.

**Opzione C (CTestTestfile with -J parallel)**: configurare CTest per eseguire il binary con `-j` parallelo e reporting per-test-case. Meno robusto ma più semplice.

## Forward-points

| Forward-point | Status | Chiude quando |
|---|---|---|
| `PHASE-1-REGISTRATION-AUDIT` | OPEN | rg-probe completo su tutti i test binary per quantificare il gap (target: 100+ test case scoperti) |
| `PHASE-2-DISCOVERY-IMPLEMENT` | OPEN | Opzione A/B/C scelta + implementata in `cmake/Chronon3DTestSuite.cmake` |
| `PHASE-3-CI-INTEGRATION` | OPEN | `tools/verify_*.sh` family usa `ctest -R` invece di direct binary invocation |

## macchina-verifica (this session, VPS)

- `ctest -N -R ShapedGlyphLine` → 0 matches
- `./tests/chronon3d_content_tests --list-test-cases | rg -c -i 'shaped'` → 23 matches (binary has tests, ctest doesn't see them)
- macchina-verifica DEFERRED-WBH per ctest-registration-gap; CLOSED-WBH quando PHASE-2-DISCOVERY-IMPLEMENT completa

## Cross-link (this session)

- AGENTS.md §"Fare PR piccole e mirate" (P2 minimal-surface recipe)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- AGENTS.md §honest-limitation (ctest-registration-gap ≠ code-block; cronaca in ticket-home)
- Parent forward-point: TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md forward-point (g) PARTIAL macchina-verifica
- Sibling forward-points: TICKET-TEST-FONT-ASSET-PATH (P1) + TICKET-TEST-CLUSTER-BENCHMARK-CRASH (P2)
- Canonical CMake function: `cmake/Chronon3DTestSuite.cmake` (the §12.1 test source registration helpers)
- ADR-018 — TICKET-CMAKE-TEST-MANIFEST-UNIFICATION (precedent for test registration discipline)

## macchina-verifica DEFERRED-WBH

Per AGENTS.md §honest-limitation + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV forward-point (g) PARTIAL classification. Chiusura richiede implementazione di doctest_discover_tests o equivalente (PHASE-2-DISCOVERY-IMPLEMENT).
