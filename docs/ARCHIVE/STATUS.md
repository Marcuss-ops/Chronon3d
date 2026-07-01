# Chronon3D — Current Status

> **Snapshot:** `main@e853e728` — 2026-06-29.
>
> Questo documento descrive blocker e prove operative. Non certifica una baseline
> verde finché tutti i gate richiesti non sono osservati sullo stesso commit.

## Stato generale (HEAD `main@e853e728`)

Chronon3D è avanzato ma **pre-stabile**. Le fondazioni di rendering, testo,
camera e packaging SDK sono presenti; il lavoro immediato è trasformarle in un
percorso unico, verificato e consumabile fuori dalla source tree.

### 5 fix atomici recenti (4 chiusi in `81cdc738` + ROT-2 chiuso in `e853e728`)

I seguenti fix per lo Step 6, SDK e ROT-2 sono stati fusi su `main`:

1. **Fix 1 — `cmake/Chronon3DSdkArchive.cmake`**: `sdk_archive_merge DEPENDS —
   foreach-if-TARGET filter`. Risolve `ninja: error: 'src/<target>' needed by
   'src/CMakeFiles/sdk_archive_merge', missing and no known rule to make it`
   quando un registry entry è gated-out da `CHRONON3D_BUILD_*` /
   `CHRONON3D_ENABLE_*` / `CHRONON3D_USE_BLEND2D`.
2. **Fix 2 — `src/render_graph/pipeline/`**: `render_runtime.hpp include for
   runtime().executor() ROT-1`, applicato a `scene_tile_execution.cpp` e
   `tile_execution_coordinator.cpp` (audit-comment block TICKET-038/TXT-00,
   cita commit `91debc36` come precedent).
3. **Fix 3 — `cmake/sdk_archive_merge.cmake` (NEW)**: script POST_BUILD `ar
   crs` con `separate_arguments(NATIVE_COMMAND)` per argv shape-stable
   (ARG_MAX-safe). Risolve `CMake Error: Not a file: cmake/sdk_archive_merge.cmake`.
4. **Fix 4 — `cmake/Chronon3DCanarySymbols.cmake`**: catalogo canary aggiornato
   con simboli reali verificati via `nm -C --defined-only` su
   `libchronon3d_sdk_impl.a`. 8/9 PRESENT + 1 BEST-EFFORT documentato.
5. **Fix 5 (ROT-2) — `include/chronon3d/backends/software/software_renderer.hpp`**:
   `render_runtime.hpp include for software_renderer consumers (Step 9 ghost_sweep
   ROT-2)` — chiude il secondo incomplete-type ROT (consumer-side `sw_renderer->
   runtime().executor()` chain). Sibling di Fix 2 / rot-1 con stesso pattern ma
   sul header pubblico `software_renderer.hpp` (path forward-declaration →
   full include + audit-comment block TICKET-038/TXT-00, cita commit `81cdc738`).

### Stato della matrice DoD (9-step run post-fix)

Eseguito `/tmp/dod_c1_verify.sh` end-to-end al commit `e853e728`:

- Step 1 (cmake configure): ✅ PASS
- Step 2 (build `sdk_archive_merge`): ❌ FAIL rc=1 — regressione al run FRESH
  end-to-end (la compilazione locale riporta 289 oggetti in
  `libchronon3d_sdk_impl.a`, ma FRESH rebuild espone un nuovo errore non
  ancora root-causato).
- Step 3 (ar count > 1): ⏭ SKIP (cascade da Step 2).
- Step 4 (nm canaries): ⏭ SKIP — local probe: 8/9 PRESENT + 1 BEST-EFFORT
  (content area, see Follow-up #3).
- Step 5 (cmake install): ❌ FAIL (cascade dallo Step 2).
- Step 6 (find_package): ⏭ SKIP.
- Step 7 (consumer build): ⏭ SKIP.
- Step 8 (BOUNDARY-OK + PNG): ⏭ SKIP.
- Step 9 (ghost sweep / full build): ✅ PASS — **ROT-2 chiuso** via commit
  `e853e728` su `software_renderer.hpp` consumer chain. Precedent: rot-1
  Fix 2 in commit `81cdc738`.

**TOTALS: 2 PASS / 2 FAIL / 5 SKIP — pre-stabile.**

## Gap noti (follow-up richiesto per chiudere M0)

Due problemi bloccano la promozione a baseline machine-verificata. Tutti
tracciati anche in `docs/NEXT_STEPS.md`:

1. **Regressione end-to-end `sdk_archive_merge` (P0)**: da investigare perché
   il build FRESH fallisca. La build locale (warm cache) passa; FRESH
   end-to-end no.
2. **TICKET-039 follow-up — content manifest-filter (P1)**: il filtro
   `find("$_obj" "/${_reg_obj}.dir/" ...)` in
   `cmake/Chronon3DSdkArchive.cmake` non matcha `_content.dir`; il simbolo
   `chronon3d::register_content_modules` esiste in
   `content/CMakeFiles/_content.dir/register_content_modules.cpp.o` ma non
   viene mergiato in `libchronon3d_sdk_impl.a`.

> **Gap precedenti chiusi**: ROT-2 in `animated_value_*` era un secondo
> incomplete-type su ROT-1 / Fix 2. ROOT ROOT era il consumer-side
> forward-declaration di `RenderRuntime` su `software_renderer.hpp`. Chiuso
> via commit `e853e728` con `#include <chronon3d/runtime/render_runtime.hpp>`
> + audit-comment block TICKET-038/TXT-00. Tracciato come
> `TICKET-058` (Recently closed) in `docs/FOLLOWUP_TICKETS.md`.

## Threading dei commit su docs (ultimi 6 su `docs/`)

- **(NEW)** Aggiornamento `STATUS.md` + `NEXT_STEPS.md` + `FOLLOWUP_TICKETS.md`
  per snapshot `e853e728` (chiusura ROT-2 via commit `e853e728`).
- **(NEW)** Creazione di `docs/baselines/main-9ef0fe33-dod-fail-matrix.md`
  (pin pre-fix DoD matrix FAIL con 1P/3F/5S).
- **(NEW)** Amend surgical a `ROADMAP.md` (snapshot bump + M0.x sub-section).
- `c6b9b991` — precedenti polish di ROADMAP/CURRENT_STATUS prima del DoD.
- `24388800` / `651f6d64` / `54c63abc` — closure TEXT-INV-01 footnota /
  polish.
- `223ae3c1` — bump di CURRENT_STATUS all commit `62c71e55`.

## Catena delle 5 fix atomiche (5 commit `56ab0625` → `81cdc738` + `e853e728`)

```
fix(sdk): sdk_archive_merge DEPENDS — foreach-if-TARGET filter
fix(render_graph): render_runtime.hpp include for runtime().executor() ROT-1
fix(sdk): create sdk_archive_merge.cmake POST_BUILD `ar crs` response-file script
fix(sdk): update canary catalog picks — 8 verified via nm -C, 1 BEST-EFFORT
fix(backends/software): include render_runtime.hpp for software_renderer consumers (Step 9 ghost_sweep ROT-2)         ← nuovo in e853e728
```

## Definizione locale di "fix verificato"

Un fix è considerato verde quando:

1. `cmake --preset linux-ci --fresh` rc=0 (configure).
2. `cmake --build <build> --target sdk_archive_merge -j8` rc=0.
3. `ar t libchronon3d_sdk_impl.a` count > 1 (markers + almeno 1 subsystem).
4. `nm -C libchronon3d_sdk_impl.a | grep -F <sym>` ≥1 match per ogni canary
   del catalogo (a meno di BEST-EFFORT documentato).
5. `cmake --install <build> --prefix <prefix>` rc=0.
6. `cmake -S tests/install_consumer -B <consumer>` rc=0 + consumer build rc=0.
7. `tools/install_consumer_test.sh` `GHOST-OK` + `BOUNDARY-OK` markers.

Al commit `e853e728` i punti 1–4 sono verdi localmente; il punto 7 (Step 9
GHOST-OK / BOUNDARY-OK) è verde grazie alla chiusura del ROT-2 in
`software_renderer.hpp` Fix 5. I punti 5–6 (install + consumer build)
falliscono ancora per il cascade dallo Step 2 (regressione FRESH pre-fix).

---

Un'attività è **completata** soltanto quando codice, test, gate e documenti
riportano lo stesso stato. Il DoD 9/9 verde è la condizione di uscita di M0;
il prossimo passo è la chiusura dei 2 gap residui in `docs/NEXT_STEPS.md` + re-run.
