# TICKET-RANDOM-UNIFY — Canonical deterministic Random API (P1 Public)

## Stato: DONE (2026-07-26, this session)

## Problema
- `chronon3d::detail::hash_to_unit_float(seed, unit_index)` viveva in
  `src/text/glyph_selector_random.{hpp,cpp}` (namespace interno di un
  singolo modulo) ed era il primitive hash usato da:
    - `Chronon3dTextCore::detail::apply_selector_order(Random, …)` (FASE 8)
    - `chrono3d_text_core::detail::get_or_build_permutation(seed, total)` (Fisher-Yates)
- Non c'era una API pubblica cronaca per uso generalizzato (jitter di
  layer, particelle, variazioni temporali, ogni `chronon3d::`-caller
  che volesse numeri deterministici da un seed). I consumer creavano
  helper locali (es. `chronon3d::detail::hash_noise` in test_wiggle.cpp),
  violando Cat-3 anti-dup.
- Cross-OS reproducibility: l'impl esistente usa
  `static_cast<f32>(static_cast<f64>(x) * (1.0 / UINT64_MAX))` con un
  narrowing cast f64→f32 in mezzo — non bit-stable tra libc/chip-set
  diversi (glibc vs musl vs macOS), rendendo impossibile il golden-pixel
  test cross-platform senza tolleranza.
- Il file `src/text/glyph_selector_random.{hpp,cpp}` introdusse un
  `thread_local std::unordered_map` come cache delle permutazioni
  Fisher-Yates — stato globale incrementale (Cat-3 violation del
  "no global state" + complica la diagnostica di thread-safety).

## Soluzione
1. **Nuovo header canonico** `include/chronon3d/animation/random.hpp`
   con:
   - `chronon3d::RandomSeed { u64 value; }` — POD strong-type che
     previene conversioni ambigue da integer letterali.
   - `[[nodiscard]] inline f32 deterministic_random(RandomSeed seed,
     u64 index = 0)` — pure hash, NO stato. Output in `[0, 1)`.
   - `[[nodiscard]] inline f32 deterministic_random(std::string_view
     seed_str, u64 index = 0)` — XXH64 fold → pipeline u64.
   - `[[nodiscard]] inline std::vector<u32> build_random_permutation(
     RandomSeed seed, u32 total_units)` — Fisher-Yates bijection,
     by-value, NO cache, NO thread-local state.

2. **Cross-OS bit-stable math**:
   `static_cast<f32>(static_cast<u32>(x >> 40)) / static_cast<f32>(1ULL << 24)`.
   `1ULL << 24 = 16777216` è esattamente rappresentabile in IEEE-754
   binary32; lo shift + cast u32→f32 + divisione IEEE-754 sono tutti
   libc-stable. NO narrowing cast f64 intermedio.

3. **Migration**:
   - `src/text/glyph_selector_{math,compile}.cpp`: importano
     `<chronon3d/animation/random.hpp>` invece di `"glyph_selector_random.hpp"`.
     `get_or_build_permutation(seed, total)` →
     `build_random_permutation(RandomSeed{seed}, total)` (by-value).
   - `tests/text/test_selector_shapes.cpp`: le 3 call-site storiche
     di `hash_to_unit_float(12345, 67)` migrano a
     `deterministic_random(RandomSeed{12345}, 67)`; SUBCASE aggiuntive
     per verify bijection `[0..n)` == sorted permutation.
   - `src/text/CMakeLists.txt`: `glyph_selector_random.cpp` rimosso
     dalla OBJECT lib `chronon3d_text_core`.
   - `include/chronon3d/text/glyph_selector.hpp`: rimossi i due
     declaration legacy `detail::hash_to_unit_float` +
     `detail::get_or_build_permutation` (Cat-3 anti-dup: nessun
     simbolo morto in public headers).
   - `src/text/glyph_selector_random.{hpp,cpp}`: **DELETED** dal tree.

4. **New canonical test file**
   `tests/core/animation/test_deterministic_random.cpp`
   registrato in `tests/manifests/core_general_sources.cmake`.
   12 TEST_CASE totali: determinism lock / seed-sensitivity /
   index-sensitivity / XXH64 string overload / output-range invariant /
   platform-bit-stable golden / Fisher-Yates bijection /
   seed-different-shuffle-statistically-distinct /
   empty/singleton edge cases / integration con `apply_selector_order` /
   suite over `n ∈ {1, 2, 3, 5, 10, 16, 50, 100}`.

## Cat-3 minimal-surface
- 1 NEW header `chronon3d/animation/random.hpp` (additive; Cat-3 strict
  min: 3 declarations additive: 1 POD struct + 2 inline function
  overloads).
- 1 NEW test file `tests/core/animation/test_deterministic_random.cpp`
  (Cat-3 test coverage additive).
- 4 modified source/test files (migration + registration).
- 2 DELETED source files (`src/text/glyph_selector_random.{hpp,cpp}`).
- 0 ABI break: firme esistenti sostituite 1:1 (a parte la nuova API
  additive che chi importa esplicitamente).
- 0 nuovi singleton / registry / resolver / cache (anzi: rimosso
  `thread_local std::unordered_map` cache dalle permutazioni).
- 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11
  deny-everywhere preservato).

## Forward-points
1. Golden-pixel cross-OS verification: per i sub-task che richiedono
   pixel-bit-identity cross-platform (golden matrix), eseguire il
   medesimo test su macOS (Darwin) + Windows (MSVC) + Linux-musl e
   confermare `==` bit-pattern con la lock `deterministic_random`
   golden lock — questa chore fornisce la math bit-stable, ma il
   golden sweep multi-OS è deferred a `tools/cross_os_golden_sweep.sh`
   (forward-point).
2. Document usage in `docs/V3_BLUEPRINT.md` §Active / §Composition
   layer once we add per-layer seed scenarios (V0.3+ cycle).
3. (Optional) Suite aggiuntiva per `build_random_permutation` con
   N=1000 e statistica Fisher-Yates (chi-quadro goodness-of-fit) —
   deferred to data-analysis stage if needed.
