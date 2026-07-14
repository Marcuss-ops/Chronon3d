# TICKET-TEST-FONT-ASSET-PATH

**Status**: OPEN (P1)
**Forward-point from**: [TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV](docs/tickets/TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md) forward-point (g) PARTIAL macchina-verifica (2026-07-14 this session)
**Discovered via**: `bash tools/install_vcpkg_bootstrap_linux.sh` + `cmake --build` + `./tests/chronon3d_content_tests --test-case='*ShapedGlyphLine*'` attempt

## Problema

Il test harness non riesce a caricare `assets/fonts/Poppins-Regular.ttf` al path atteso. 7/8 test ShapedGlyphLine falliscono con errore:

```
FontEngine HarfBuzz shaping produced zero glyphs
```

Il font asset è referenziato nei test file ma non è presente al path atteso dal test binary. Il blocco è **ASSET-BLOCK** (file system), non CODE-BLOCK (il [[deprecated]] marker da TICKET-COMPOSITIONDESCRIPTOR-MIGRATION Phase 2 è verificato-working — `cmake --build` exit 0 con -Wdeprecated-declarations warnings soppressi da CMakeLists.txt:28 M1.5#8).

## Scope (~30+ test files)

| Dominio | File | Test cases interessati |
|---|---|---|
| Content tests | `tests/content/test_shaped_glyph_line*.cpp` | 7/8 (1 passed, 7 failed, 31 skipped) |
| Certification | `tests/certification/test_cert_text_bbox.cpp` | TBD (forward-point: eseguire audit completo) |
| Visual tests | `tests/visual/test_*.cpp` (text-related) | TBD |
| Install consumer | `tests/install_consumer/main_text.cpp` | TBD |

## Criteri di accettazione

1. `assets/fonts/Poppins-Regular.ttf` è presente al path atteso dal test binary (canonical: `assets/fonts/Poppins-Regular.ttf` relativo a `${CMAKE_SOURCE_DIR}`)
2. I 7 test ShapedGlyphLine che falliscono passano quando eseguiti via `./tests/chronon3d_content_tests --test-case='*ShapedGlyphLine*'`
3. Il test binary NON ha dipendenze hard-coded da path assoluti (portabilità CI/WSL)
4. `tools/check_text_functional_linux.sh` completa il suo gate di verifica

## Soluzione (forward-point)

**Opzione A (canonical)**: aggiungere `assets/fonts/Poppins-Regular.ttf` al repo come tracked file (LICENSE-compatible; Poppins è SIL Open Font License 1.1 — `OFL.txt` deve essere incluso). CMakeLists.txt copia il font in `${CMAKE_BINARY_DIR}/assets/fonts/` al configure time. Test binary risolve il path via `assets/fonts/Poppins-Regular.ttf` relativo a `CMAKE_BINARY_DIR`.

**Opzione B (CI-only)**: download del font al configure time da Google Fonts API (o mirror) con checksum verification. Cache locale in `~/.cache/chronon3d/fonts/`. Più complesso ma evita di tracciare binari nel repo.

**Opzione C (test-only stub)**: creare un font stub minimal (header + 1 glyph) per i test che non hanno bisogno del font reale. Limitato: i test di shaping realistico restano asset-blocked.

## Forward-points

| Forward-point | Status | Chiude quando |
|---|---|---|
| `PHASE-1-FONT-ASSET-PATH-AUDIT` | OPEN | rg-probe completo su tutti i test file che referenziano `Poppins-Regular.ttf` (scope esatto dei 30+ file impattati) |
| `PHASE-2-FONT-ASSET-INSTALL` | OPEN | Opzione A/B/C scelta + implementata + 7/7 test passano |
| `PHASE-3-CI-PORTABILITY` | OPEN | test binary portabile tra CI/WSL/VPS senza dipendenze da path assoluti |

## macchina-verifica (this session, VPS)

- `./tests/chronon3d_content_tests --test-case='*ShapedGlyphLine*'` → 1/8 passed, 7/8 failed, 31 skipped
- 7 failures root cause: `FontEngine` cannot load `assets/fonts/Poppins-Regular.ttf` → HarfBuzz shaping produces zero glyphs → empty render → test assertions fail
- 1 SIGABRT crash in `test_shaped_glyph_line_cluster_benchmark.cpp` (forward-point separato: TICKET-TEST-CLUSTER-BENCHMARK-CRASH)
- macchina-verifica DEFERRED-WBH per asset-block; CLOSED-WBH quando PHASE-2-FONT-ASSET-INSTALL completa

## Cross-link (this session)

- AGENTS.md §"Fare PR piccole e mirate" (P1 minimal-surface recipe)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- AGENTS.md §honest-limitation (asset-block ≠ code-block; cronaca in ticket-home)
- Parent forward-point: TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md forward-point (g) PARTIAL macchina-verifica
- Sibling forward-points: TICKET-CTEST-PER-TESTCASE-REGISTRATION (P2) + TICKET-TEST-CLUSTER-BENCHMARK-CRASH (P2)
- Sibling precedent: TICKET-SABOTAGE-FONT-REAL-ENGINE (prior font-stub → real-engine migration; cronaca in canonical ticket-home)

## macchina-verifica DEFERRED-WBH

Per AGENTS.md §honest-limitation + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV forward-point (g) PARTIAL classification. Chiusura richiede install del font asset (PHASE-2-FONT-ASSET-INSTALL).
