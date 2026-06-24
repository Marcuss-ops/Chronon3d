# Chronon3D — Release Gate

> **Snapshot:** `main@83a4bf21` — 24 giugno 2026.
>
> Questo documento definisce i **gate bloccanti** che un commit deve passare per
> essere considerato release candidate. È il contratto formale tra CI, codebase e
> release artifact. Vedi [`CURRENT_STATUS.md`](CURRENT_STATUS.md) per lo stato
> prodotto e [`ROADMAP.md`](ROADMAP.md) per le milestone.

## Gate pre-merge (ogni PR)

Tutti i gate in `.github/workflows/gates.yml` devono essere **verdi e bloccanti**:

| # | Gate | Costo | Descrizione |
|---|---|---|---|
| 1 | `core-build` | 30 min | Build senza text/video/content/diagnostics |
| 2 | `sdk-build` | 30 min | Build con Text+Blend2D |
| 3 | `public-header-check` | 30 min | Ogni header pubblico compilato standalone |
| 4 | `install-consumer-check` | 30 min | `cmake --install` + `find_package` esterno |
| 5 | `architecture-check` | 5 min | Regole statiche + doctest skip tickets + boundary grep + gitignored dirs |
| 6 | `architecture-selftest` | 3 min | 14 assertion sul boundary script |
| 7 | `audit-software-renderer` | 3 min | Inventario SoftwareRenderer |
| 8 | `filename-drift` | 3 min | File citati devono esistere su disco |
| 9 | `profile-measurement` | 60 min | Metriche per profilo |
| 10 | `install-consumer-script` | 30 min | Test SDK end-to-end |

Se la PR tocca `src/`, `include/`, `CMakeLists.txt`, `vcpkg.json` o workflow:
anche `full-validation` (`.github/workflows/gates-full-validation.yml`) deve essere verde.

## Gate architetturali (14 semantic checks)

Da `tools/check_architecture_boundaries.sh`. Tutti e 14 i check sono **bloccanti**:

1. `core/memory/render_session.hpp` retired
2. `renderer_runtime_resources.hpp` retired
3. `renderer_cache_state.hpp` retired
4. `clear_per_frame()` retired
5. `plan_cache` retired
6. `detail::g_debug_config` removed
7. `detail::g_default_assets_root` removed
8. `<chrono3d/...>` typo forbidden
9. `core/memory/*` within allowlist
10. SoftwareRenderer boundaries (advisory: promuovere quando invarianti verdi)
11. msdfgen/libtess2/unicode includes forbidden
12. CMake module registry parity (**bloccante**)
13. vcpkg/find_package parity (**bloccante**)
14. SDK public surface boundary (**bloccante**)

## Gate release candidate (stesso commit)

Tutti i seguenti devono restituire `RC=0` sullo **stesso commit**:

### Build
- [ ] `cmake --preset linux-core-dev && cmake --build --preset linux-core-dev`
- [ ] `cmake --preset linux-lean-dev && cmake --build --preset linux-lean-dev`
- [ ] `cmake --preset linux-ci-nocontent && cmake --build --preset linux-ci-nocontent`
- [ ] `cmake --preset linux-full-validation && cmake --build --preset linux-full-validation`
- [ ] `cmake --preset linux-release && cmake --build --preset linux-release`

### Test
- [ ] `ctest --preset linux-ci-test` (core tests)
- [ ] `ctest --preset linux-full-validation-test` (full suite)
- [ ] `chronon3d_scene_tests` link + run verde
- [ ] Test camera compilati link + run verde

### Architecture
- [ ] `tools/test_architectural.sh` exit 0
- [ ] `tools/check_architecture_boundaries.sh` exit 0
- [ ] `tools/check_architecture_boundaries_selftest.sh` exit 0
- [ ] `tools/check_gitignored_dirs.sh` exit 0
- [ ] `tools/check_filename_drift.sh --strict` exit 0

### SDK boundary
- [ ] `tools/install_consumer_test.sh` exit 0 (consumer fuori-tree)
- [ ] Il consumer installato verifica package, link e un simbolo SDK reale
- [ ] (Target M3) Consumer fuori-tree che renderizza composizione reale

### Documentazione
- [ ] `docs/CURRENT_STATUS.md` aggiornato con commit, comandi e risultati
- [ ] Nessuna contraddizione tra CURRENT_STATUS.md, ROADMAP.md e stato codice

## Regole permanenti

- **Nessun gate può essere indebolito** per adattarlo al codice
- **Nessun test skipped** per nascondere un errore
- **Nessun `continue-on-error`** sui gate bloccanti
- **Nessun push diretto su `main`** — solo merge da PR con required check verdi
- Una baseline non è verde finché build, test, gate, consumer e documenti non
  riportano lo stesso stato sullo stesso commit

## Exit criteria per milestone

### M0 — Baseline verificata
- Tutti i gate release candidate verdi sullo stesso commit
- Documenti sincronizzati

### M1 — Text Production V1
- 20+ preset generali + 8 subtitle con golden 16:9/9:16 multi-timestamp
- Word timing SRT/JSON funzionante
- Consumer SDK usa percorso testuale pubblico
- Output seriale e parallelo deterministico

### M2 — Camera Production V1
- Nessuna composizione usa `AnimatedCamera2_5D` o rig legacy
- `OrientAlongPath` completo
- Framing bounds-aware funzionante
- Test camera bloccanti verdi
- Consumer SDK esterno usa camera compilata

### M3 — SDK Product V1
- Consumer fuori-tree renderizza composizione reale
- Documentazione pubblica e artifact riproducibili
- `find_package(Chronon3D CONFIG REQUIRED)` funziona da progetto vuoto
- Nessun link diretto a target interni
