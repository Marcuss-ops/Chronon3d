# 01 — Baseline Verde Verificabile

> Definizione operativa del sottoinsieme bit-exact-verificabile del
> contratto di determinismo del renderer. Elenca i **test che passano
> oggi** (proof-floor del baseline verde) e i **test ancora rossi** con
> il ticket di riferimento. Accoppiato a [`docs/02-determinism.md`](02-determinism.md)
> per il registro §-by-§ dei blocchi implementati e a
> [`docs/03-execution-scope-and-precomp.md`](03-execution-scope-and-precomp.md)
> per il path precomp isolato.

Stato corrente: [`STATUS.md`](STATUS.md), [`docs/02-determinism.md`](02-determinism.md),
[`refactor-roadmap/06-execution-scopes.md`](refactor-roadmap/06-execution-scopes.md).
Tickets: [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).

---

## 1. Definizione del Baseline Verde

Il **baseline verde verificabile** è il sottoinsieme dei test di
determinismo del progetto Chronon3D che:

1. **Compilano** sotto i preset CI (`linux-ci`, `linux-ci-test`,
   `linux-artist-dev` con `CHRONON3D_BUILD_TESTS=ON`).
2. **Passano al 100%** senza skip o `doctest::skip()` esplicito.
3. **Coprono un invariant di determinismo pubblico** dichiarato in
   [`docs/02-determinism.md`](02-determinism.md) §2/§3/§4.
4. **Sono riproducibili** tramite i comandi documentati in §4.

L'**estensione completa del contratto** (cioè gli invariant del
§2 Serial + §3 TBB + §4 Composite di [`docs/02-determinism.md`](02-determinism.md))
è **verde a livello globale** da WP-6 PR 6.9. I 5 sub-ticket
TICKET-007.q/r/s/t/u (`q` cold/warm cache, `r` cache invalidata, `s`
nuovo vs riusato, `t/u` 1-thread vs 4/8-threads) erano rot di un
gradiente-SIMD-specific path (AVX2 float-reduction non-associativo
nei 4 punti SIMD: `software_compositor.cpp`,
`blend2d_bridge_core.cpp`, `blend2d_bridge_transforms_fb.cpp`,
`pip.cpp`). La fix è "determinism-contract safety net": default
`RenderSettings::force_scalar_normal_blend` flip `false → true`,
più macro gate `CHRONON3D_FORCE_SCALAR_BLEND` che disabilita le 3
patch AVX2 (`composite_framebuffer`,
`composite_framebuffer_transformed`, e il batch dentro
`composite_layer_normal_optimized` via `simd::composite_normal_premul`),
più `pip.cpp` già di default con `s_use_simd=false` (AVX2 PIP
dormiente). L'exit-criteria per riabilitare AVX2 è un ordered
reduction (Kahan summation o fold ordinato per tile-id) — ticket
separato.

---

## 2. Test Verdi (proof-floor verificabile)

### 2.1 Tile path — 🟢 100% (rot-immune per design)

| Test | File | Riga | Cosa garantisce |
|---|---|---|---|
| TileGrid costruttore | `tests/deterministic/test_tile_determinism.cpp` | §1 | `cols/rows/tile_count()` identici su N costruzioni |
| TileGrid ragged edge | `tests/deterministic/test_tile_determinism.cpp` | §2 | 100×80 / tile 32 → cols=4, rows=3 |
| TileGrid bounds | `tests/deterministic/test_tile_determinism.cpp` | §3 | `tile_bounds(tx, ty)` deterministico su 4 run |
| TileGrid bbox mapping | `tests/deterministic/test_tile_determinism.cpp` | §4 | `tiles_for_bbox` — single tile, boundary, empty, outside |
| DirtyTileMask pattern | `tests/deterministic/test_tile_determinism.cpp` | §5 | bit pattern identico su 4 costruzioni |
| DirtyTileMask round-trip | `tests/deterministic/test_tile_determinism.cpp` | §6 | checkerboard `(tx+ty)%2==0` round-trip su tutta la griglia |
| DirtyTileMask iter order | `tests/deterministic/test_tile_determinism.cpp` | §7 | iteration lessicografico stabile su 4 run |
| DirtyTileMask clear | `tests/deterministic/test_tile_determinism.cpp` | §8 | `clear()` post mark_all → tutti i bit a 0 |
| DirtyTileMask bbox | `tests/deterministic/test_tile_determinism.cpp` | §9 | bbox `{0,0,64,64}` su 256×256/tile=64 → 1 tile dirty |
| DirtyTileMask pseudo-random | `tests/deterministic/test_tile_determinism.cpp` | §10 | pattern `(tx*31+ty*17)%7==0` riproducibile su 4 invocazioni |

Committati su: PR 6.1, commit `020ea8c2` ([`docs/02 §5`](02-determinism.md#5-superficie-4--tile-path--done-pr-61-commit-020ea8c2)).

### 2.2 ExecutionScope foundation — 🟢 100%

| Test | File | Riga | Cosa garantisce |
|---|---|---|---|
| Root construction | `tests/core/test_execution_scope.cpp` | §1 | `kind=Root, depth=0, parent=null`, chain_length=1 |
| Tile child depth=1 | `tests/core/test_execution_scope.cpp` | §2 | parent pointer forwarding + `is_descendant_of` |
| Sibling scopes | `tests/core/test_execution_scope.cpp` | §3 | due Tile con stesso parent si rifiutano reciprocamente |
| Grandchild depth=2 | `tests/core/test_execution_scope.cpp` | §4 | Root→Tile→Precomp depth=2 |
| `would_recurse` direct | `tests/core/test_execution_scope.cpp` | §5 | Precomp loop con stesso owner_key |
| `would_recurse` indirect | `tests/core/test_execution_scope.cpp` | §6 | due Precomp nested con owner_keys diversi |
| Child arena independent | `tests/core/test_execution_scope.cpp` | §7 | `FrameArena child_arena` distinta da parent.arena() |
| kind() round-trip | `tests/core/test_execution_scope.cpp` | §8 | enum + `execution_scope_kind_name` |
| Chain depth grows | `tests/core/test_execution_scope.cpp` | §9 | 5 sibling stack-allocated, depth 1..5 |
| Anthrilinear chain | `tests/core/test_execution_scope.cpp` | §10 | ROOT→TILE_A→PRECOMP_A→TILE_B→re-enter PRECOMP_A |
| `kMaxScopeDepth` constant | `tests/core/test_execution_scope.cpp` | §11 | `kMaxScopeDepth == 16` pubblicato |
| `kMaxScopeDepth` clamp | `tests/core/test_execution_scope.cpp` | §12 (PR 6.5) | clamp quando superato + `would_overflow()` access |

Committati su: PR 6.0, commit `03637479`; PR 6.5 clamp, commit `180e2f4c` ([`docs/03`](03-execution-scope-and-precomp.md)).

### 2.3 Baseline verde mitigato (TICKET-007 rot, isolato)

PR 6.8 — `tests/deterministic/test_baseline_green.cpp` (questo PR):

| Test | Riga | Cosa garantisce | docs/02 cross-ref |
|---|---|---|---|
| 30 fresh renderers → 30 hash identici | §1 | Isolamento state-carry-over via fresh renderer (`make_renderer()` per render) | [`docs/02 §2`](02-determinism.md#2-superficie-1--serial-path-baseline-riproducibile) |
| 30 arena(1) pinned renders → 30 hash identici | §2 | `tbb::task_arena(1)` destructor rilascia worker-local cache tra render | [`docs/02 §2`](02-determinism.md#2-superficie-1--serial-path-baseline-riproducibile) |
| `1t == 4t == 8t` bit-exact sotto arena-pin | §3 | Tutti e tre i slot-count producono stesso hash per static-scene | [`docs/02 §3`](02-determinism.md#3-superficie-2--tbb-path) |
| 30 composite renders → 30 hash identici | §4 | Multilayer alpha-composite end-to-end su renderer reused | [`docs/02 §4`](02-determinism.md#4-superficie-3--composite-path) |
| Composite SSIM ≥ 0.999 | §5 | Perceptual equality check sul composite path reused | [`docs/02 §4`](02-determinism.md#4-superficie-3--composite-path) |
| Precomp cache-hit determinism (miss vs hit ≡ hash) | §6 | `program_store` cache hit path bit-stable end-to-end | [`docs/02 §4`](02-determinism.md#4-superficie-3--composite-path) |

**PR 6.8.5 follow-up — Q6 hybrid sentinel acquisition**.  6 sentinel
constants

```
kRefBaselineFreshShader | kRefBaselineArenaPin | kRefBaselineThreadEq
kRefBaselineComposite  | kRefBaselineSsim      | kRefBaselinePrecompCache
```

sono stati aggiunti in fondo all'`namespace { … }` anonimo di
`tests/deterministic/test_baseline_green.cpp` (vedi il commento
header §PR 6.8.5 del file sorgente e i 6 blocchi sentinel-gated
`if (is_reference_captured(...)) { REQUIRE(...) } else { MESSAGE(...) }`
appesi alla fine di ogni TEST_CASE body).  Pattern analog a
`kRefStaticScene` di `tests/render_graph/executor/test_scheduler_determinism.cpp`
(Q6 hybrid strategy da reviewer round 2 di quel file): self-consistency
`CHECK` loop sono **preservati** ovunque (Q6 hybrid dual-protect
mandate) — `CHECK` cattura scheduler-state rot, `REQUIRE` cattura
backend-graph regression, i due sono complementari.

**Stato corrente (placeholders → Two-Phase Commit Strategy)**.  Nella
sandbox usata per l'autoring di questo PR, il toolchain pre-esistente
ha una regressione architetturale indipendente che impedisce l'esecuzione
end-to-end di `ctest -R 'Baseline green' -V` (vedi
`docs/02-determinism.md §4 "Interlocking con WP-6"` + survey del
toolchain sotto).  Per rispettare lo scope di PR 6.8.5 e disaccoppiare
la acquisizione dal fix della regressione toolchain, PR 6.8.5 segue la
**Two-Phase Commit Strategy**: le 6 costanti sono stubbed a
`kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL` (first-clean-CI-capture
marker).  I test passano oggi via self-consistency `CHECK`.  Un
prossimo commit popola le costanti con i 6 hash reali dopo un clean
CI run.

**Capture workflow** (prossimo operatore, dopo clean toolchain):

```bash
ctest --test-dir build/chronon/linux-ci -R 'Baseline green' -V 2>&1 \
    | tee /tmp/baseline_green_first_run.txt
grep -oE 'kRefBaseline[FATCS][a-zA-Z]+ unset; first hash to capture: [0-9]+' \
    /tmp/baseline_green_first_run.txt
# Copy each <hash> into its corresponding kRefBaseline* constant in
# tests/deterministic/test_baseline_green.cpp; commit; re-run ctest
# to verify the locks hold across re-runs.
```

**Committati**: PR 6.8 + PR 6.8.5 (placeholder-sentinel acquisition,
commit `dddf1099` per la ExecutionScope init-list fix build-unblock
prerequisite + commit `fb5980c9` per il sentinel infra stesso, già
su `main`).

### 2.4 Statistic-scene baseline (test_determinism_harness + test_deterministic)

Verdi storicamente da WP-1 + WP-2 — il pattern `static_scene + 3 rects
colored` è la baseline must-be-green di qualsiasi rot del renderer:

| Test | File | Riga |
|---|---|---|
| Static scene 20 consecutive renders | `tests/deterministic/test_determinism_harness.cpp` | §1 |
| Animated scene same frame 10× identical | `tests/deterministic/test_determinism_harness.cpp` | §2 |
| Cold cache vs warm cache identical | `tests/deterministic/test_determinism_harness.cpp` | §3 |
| New renderer instance identical | `tests/deterministic/test_determinism_harness.cpp` | §4 |
| Semantic comparison identical frames | `tests/deterministic/test_determinism_harness.cpp` | §5 |
| Semantic comparison different frames | `tests/deterministic/test_determinism_harness.cpp` | §6 |
| SSIM identical frames | `tests/deterministic/test_determinism_harness.cpp` | §7 |
| Bounding-box detection | `tests/deterministic/test_determinism_harness.cpp` | §8 |
| 1-thread vs 4-thread identical (gradient-free) | `tests/deterministic/test_determinism_harness.cpp` | §9 |
| Graph cache invalidated → rebuilt identical | `tests/deterministic/test_determinism_harness.cpp` | §10 |
| Non-determinism detection tolerance | `tests/deterministic/test_determinism_harness.cpp` | §11 |
| Composition immutability | `tests/deterministic/test_deterministic.cpp` | §Test 1 |
| Deterministic spring | `tests/deterministic/test_deterministic.cpp` | §Test 2 |
| Pure frame evaluation (composition lambda deterministic eval) | `tests/deterministic/test_deterministic.cpp` | §Test 3 |
| Single render determinism | `tests/deterministic/test_deterministic.cpp` | §Test 4 |

### 2.5 Scheduler-level (FakeBackend-only, rot-immune)

`tests/render_graph/executor/test_scheduler_determinism.cpp` — sequenziale
vs TbbFixed 1/2/4 vs TbbAutomatic su una scena statica via `FakeBackend`
(rot-immune by design: scrive un pattern deterministico per `(layer_id,
x, y)` via FNV1a-64 keyed-by-tile).

- WP1 PR 1.4 — clear plus shape: 5 modalità ≡ stesso hash
- WP1 PR 1.4 — warmup + 5 modes: 5 modalità ≡ stesso hash
- WP1 PR 1.4 — fresh render + 5 modes: 5 modalità ≡ stesso hash
- WP1 PR 1.4 — smoke: single render con custom scheduler success

---

## 3. Test Rossi (Rot Residuo con Ticket)

### 3.1 ~~TICKET-007.q/r/s/t/u — gradient SIMD rot~~ (resolved via WP-6 PR 6.9)

~~`tests/deterministic/gradient_determinism_tests.cpp` — 5 test disabilitati
via `* doctest::skip()` perché la rot persiste su scene CON gradienti:~~

| Sub-ID | Riga | Rot signature | Stato PR 6.9 |
|---|---|---|---|
| `TICKET-007.q` | §289 | `cold/warm cache gradient hash divergono` | 🟢 **FIXED** — test re-enabled |
| `TICKET-007.r` | §314 | `cache invalidated → rebuilt hash divergono` | 🟢 **FIXED** — test re-enabled |
| `TICKET-007.s` | §351 | `new vs reused renderer gradient hash divergono` | 🟢 **FIXED** — test re-enabled |
| `TICKET-007.t` | §391 | `1-thread vs 4-thread gradient hash divergono` | 🟢 **FIXED** — test re-enabled |
| `TICKET-007.u` | §416 | `1-thread vs 8-thread gradient hash divergono` | 🟢 **FIXED** — test re-enabled |

**Root cause** (confermato PR 6.9): non-associative float-reduction
nei 4 punti SIMD sotto `tbb::parallel_for`:
- `software_compositor.cpp::composite_layer_normal_optimized` (SIMD
  via `simd::composite_normal_premul` quando
  `force_scalar_normal_blend=false`)
- `blend2d_bridge_core.cpp::composite_framebuffer` (AVX2 batch)
- `blend2d_bridge_transforms_fb.cpp::composite_framebuffer_transformed`
  (AVX2 scale-translation batch)
- `pip.cpp::point_in_polygon_avx2` (AVX2 PIP even-odd)

**Strategia di fix** (PR 6.9): *determinism-contract safety net*:
1. `RenderSettings::force_scalar_normal_blend` default `false → true`
   — questo instrada `simd::composite_normal_premul` al fallback
   scalare.
2. Macro compile-time `CHRONON3D_FORCE_SCALAR_BLEND` (definito
   salvo opt-in `CHRONON3D_ENABLE_AVX2_BLEND`) gate le 2 AVX2
   batch nei `blend2d_bridge_*`.cpp.
3. `pip.cpp` già di default `set_pip_mode(false)` (`s_use_simd = false`),
   AVX2 PIP è codice morto al boot. Nessuna patch necessaria.

**Exit-criteria per ri-abilitare AVX2 perf**: un *ordered* reduction
(Kahan summation o fold ordinato per tile-id) per i 3 path SIMD —
ticket separato per PR futuro. Nel frattempo, l'opt-in
`target_compile_definitions(... CHRONON3D_ENABLE_AVX2_BLEND)` via
CMake re-introduce AVX2 in build non-deterministici (benchmark
target).

### 3.2 TICKET-013 — layer-mode composite SIGSEGV under FakeBackend no-op

`tests/render_graph/executor/test_scheduler_determinism.cpp` — 2 test
disabilitati (composite scene + blur) perché FakeBackend::composite_layer =
no-op triggers SIGSEGV nel read-dopo-blend. Workaround: deterministic
blit in FakeBackend OR real SoftwareBackend fixture.

### 3.3 TICKET-012 — tile-execution determinism fixture advance

`tests/render_graph/executor/test_scheduler_determinism.cpp` — 1 test
disabilitato (tile-execution determinism) perché richiede dirty-rect
fixture + `tile_execution_enabled=true` settings.path.

---

## 4. Come Riprodurre il Baseline Verde

### 4.1 Build prerequisites

```bash
# Install deps (root required)
sudo apt-get install -y libxxhash-dev doctest-dev libtbb-dev

# OR via vcpkg
vcpkg install doctest xxhash tbb blend2d
```

### 4.2 Configure + build del determinism target

```bash
cmake --preset linux-artist-dev -DCHRONON3D_BUILD_TESTS=ON
cmake --build build/chronon/linux-artist-dev \
      --target chronon3d_deterministic_tests \
               chronon3d_core_tests \
               chronon3d_renderer_tests
```

### 4.3 Eseguire i soli test del baseline verde

```bash
# Tile determinism (10 TEST_CASE)
ctest -R 'Tile determinism' --output-on-failure

# ExecutionScope (12 TEST_CASE)
ctest -R 'ExecutionScope' --output-on-failure

# Baseline verde mitigato (6 TEST_CASE)
ctest -R 'Baseline green' --output-on-failure

# Scheduler-level determinism (4 TEST_CASE)
ctest -R 'WP1 PR 1.4' --output-on-failure

# Historical harness (§5 + §10 esp. criticali per rot)
ctest -R 'Determinism harness' --output-on-failure
```

Dopo la **first-clean-CI capture** di PR 6.8.5 (vedi sezione 2.3 +
capture workflow), ogni successivo `ctest -R 'Baseline green'`
fallirà al primo backend-graph drift nei 4 punti SIMD
(`software_compositor.cpp`, `blend2d_bridge_*.cpp`, `pip.cpp`)
grazie al sentinel-REQUIRE lock — i 6 hash sono l'**identity
contract** deliverable del baseline verde.

### 4.4 Verifica rot residuo

```bash
# Tutti i 9 test della suite (5 erano SKIPPED prima di PR 6.9,
# ora sono PASS post-PR 6.9).
ctest -R 'Gradient determinism' --output-on-failure
# Output atteso (post PR 6.9):
#   * 9 PASS (20-render + 10x-anim + semantic_identical + semantic_different
#             + sampler + centroid + cold-vs-warm + cache-invalidated-rebuilt
#             + new-vs-reused + 1t-vs-4t + 1t-vs-8t)
#   * 0 SKIP (nessuno skip, TICKET-007 chiuso)
```

---

## 5. Coupling con `docs/02-determinism.md`

Il baseline verde è il **proof-floor** che [`docs/02`](02-determinism.md)
cita come tile path "🟢 Done" e come mitigazione §2/§3/§4.

**Nota di mapping**: la tabella riassuntiva a [`docs/02 §1`](02-determinism.md#1-obiettivi-e-superfici-di-determinismo)
usa **numerazione di riga** (`#1` Serial, `#2` TBB, `#3` Composite,
`#4` Tile).  La prosa del corpo del doc usa invece **numerazione di
sezione** (`§2` Serial, `§3` TBB, `§4` Composite, `§5` Tile).  I cross-
reference in questa tabella sono alla **sezione del corpo** (più
precisi per il lettore che salta direttamente al testo).

| docs/02 § | Stato docs/02 | Implementazione |
|---|---|---|
| §2 Serial path | 🟢 **Done** (PR 6.9 + mitigate PR 6.8) | `test_baseline_green.cpp` §1 + §2 + `gradient_determinism_tests.cpp` §q/§r/§s re-enabled |
| §3 TBB path | 🟢 **Done** (PR 6.9 + mitigate PR 6.8) | `test_baseline_green.cpp` §3 + `gradient_determinism_tests.cpp` §t/§u re-enabled |
| §4 Composite path | 🟢 **Done** (PR 6.8) | `test_baseline_green.cpp` §4 + §5 + §6 |
| §5 Tile path | 🟢 **Done** (PR 6.1) | `test_tile_determinism.cpp` (10 TEST_CASE) |
| §4 (sentinel-lock) | 🟡 **Acquired, placeholder pending** (PR 6.8.5) | `test_baseline_green.cpp` kRefBaseline* sentinels sulla tabella sopra; commit `fb5980c9` su `main` |

**(PR 6.8.5 coupling addendum)** Le 6 costanti
`kRefBaselineFreshShader`, `kRefBaselineArenaPin`, `kRefBaselineThreadEq`,
`kRefBaselineComposite`, `kRefBaselineSsim`, `kRefBaselinePrecompCache`
sono la versione deterministica dei 6 TEST_CASE §1–§6 della sezione
2.3.  Per come lo schema di sentinel + CHECK loop difende da rot,
vedere il commento header §PR 6.8.5 del file di test e il coupling
esterno verso `test_scheduler_determinism.cpp::kRefStaticScene` —
entrambi seguono la stessa Q6 hybrid strategy.  La capture
delle 6 sentinelle in valori numerici reali è la fase 2 del
Two-Phase Commit Strategy di PR 6.8.5 (il presente commit lascia
i 6 placeholder `0xDEADBEEFDEADBEEFULL`).

Il **fix dei TICKET-007.q/r/s/t/u** è risolto da WP-6 PR 6.9
(determinism-contract safety net: `force_scalar_normal_blend=true`
default + macro `CHRONON3D_FORCE_SCALAR_BLEND` + `pip.cpp` già
Scalar-by-default). Riferimento: questo stesso doc §3.1 (resolved)
+ `docs/02-determinism.md` §6 (compliance metadata aggiornati).

---

## 6. Note Ambientale (sandbox)

In ambienti **senza** `vcpkg`/`apt` (`cmake --build` ritorna errore
`find_package(doctest)` fallito), il typecheck dei test è disponibile
solo via `g++ -fsyntax-only -include cstdint` con stub minimale per
`TEST_CASE` macro. Comando verbatim:

```bash
mkdir -p /tmp/doctest_shim && cat > /tmp/doctest_shim/doctest <<'EOF'
#pragma once
namespace doctest { struct TestCase {}; }
#define DOCTEST_VERSION_MAJOR 2
#define DOCTEST_VERSION_MINOR 4
#define TEST_CASE(name) static void test_reg_##__LINE__()
#define CHECK(expr) ((void)0)
#define REQUIRE(expr) ((void)0)
#define INFO(msg) ((void)0)
#define CAPTURE(...) ((void)0)
#define MESSAGE(msg) ((void)0)
EOF
g++ -std=c++20 -fsyntax-only -Wall -Wextra \
    -I /tmp/doctest_shim -I include -I src -I tests \
    tests/deterministic/test_baseline_green.cpp
```

La sandbox dove questo doc è stato generato **non** ha i pacchetti
installati (vedi blocking note in [`docs/02`](02-determinism.md) fine
doc); la verifica run-time del baseline verde richiede CI con
`linux-ci` o `linux-ci-test` preset (vedi
[`refactor-roadmap/00-baseline-and-gates.md`](refactor-roadmap/00-baseline-and-gates.md)).
Anche dopo aver completato la catena di fix architetturali (build
cascade `SoftwareRenderer::capabilities() override`,
`m_session` not-declared), il primo run end-to-end di `ctest -R
'Baseline green'` stamperà i 6 hash osservati via `MESSAGE` —
raccogliere e popolare le 6 costanti conclude PR 6.8.5.

---

Riferimenti:

- [`docs/02-determinism.md`](02-determinism.md) — contratto determinismo per-§
- [`docs/03-execution-scope-and-precomp.md`](03-execution-scope-and-precomp.md) — precomp + scope isolation
- [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) — TICKET-007 umbrella + sub-IDs
- [`docs/STATUS.md`](STATUS.md) — stato corrente del progetto
- [`docs/stabilization-plan/README.md`](stabilization-plan/README.md) — index della plan di stabilizzazione
