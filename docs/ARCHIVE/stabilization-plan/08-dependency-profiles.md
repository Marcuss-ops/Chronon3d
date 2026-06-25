# Profili dipendenze e build

## Stato reale

I quattro profili configurano e l'isolamento vcpkg è verificato dopo il fix `VCPKG_MANIFEST_NO_DEFAULT_FEATURES`. Restano CI, build completa, dimensioni binari e allineamento dei preset non-profile.

## Implementato e verificato

- [x] Profilo core senza testo, video, mesh ed EXR.
- [x] Profilo motion con Blend2D e testo.
- [x] Profilo video separato.
- [x] Profilo extended per mesh, EXR, telemetria e profiling.
- [x] Preset CMake dedicati ai quattro profili.
- [x] Script `tools/measure_profile.sh`.
- [x] Output di misura JSON e Markdown.
- [x] Documentazione di utilizzo dello script.
- [x] Fix `VCPKG_MANIFEST_NO_DEFAULT_FEATURES: "ON"` nei quattro profili (CMakePresets.json).
- [x] Fix bug script: `declare -A bin_paths` mancante (tools/measure_profile.sh).

## Misurazioni configure (Linux, 2026-06-22)

| Profilo | Configure | Package vcpkg | Cosa installa |
|---|---|---|---|
| `linux-profile-core` | 12s | **49** | base + doctest |
| `linux-profile-motion` | 8s | **86** | core + blend2d, freetype, harfbuzz, fribidi, cli11 |
| `linux-profile-video` | 5s | **91** | motion + sqlite3 (telemetry) |
| `linux-profile-extended` | 11s | **125** | tutto: mesh, openexr, benchmark, tracy |

Package base (49): fmt, glm, highway, magic-enum, nlohmann-json, spdlog, stb, taskflow, tbb, xxhash, doctest.

## Gap scoperti e corretti

1. **`default-features` inquinavano tutti i profili** — `vcpkg.json` dichiara `default-features: ["mesh", "cli", "blend2d", "text", "exr"]`. Prima del fix, anche `linux-profile-core` installava blend2d, freetype, harfbuzz, openexr, meshoptimizer (36 package identici al profilo motion). **Fix**: `VCPKG_MANIFEST_NO_DEFAULT_FEATURES: "ON"` aggiunto ai quattro profili in `CMakePresets.json`.

2. **Bug in `tools/measure_profile.sh`** — `bin_paths` usato come array associativo senza `declare -A`, causava crash con chiavi contenenti punti (es. `chronon3d_sdk_impl.a`). **Fix**: aggiunto `declare -A bin_paths`.

## Ancora aperto

- [ ] Testare ogni profilo in CI (configure + build + test).
- [ ] Registrare tempi di build (non solo configure) su baseline comune.
- [ ] Registrare dimensione di `chronon3d_sdk_impl.a`, `chronon3d_cli`, `chronon3d_tests_fast` (richiede `--build`).
- [ ] Documentare il profilo raccomandato per produzione CPU-first.
- [ ] Verificare il consumer SDK almeno per core e motion.
- [ ] Allineare i preset non-profile (`linux-debug`, `linux-release`, `linux-ci`, `linux-fast-dev`, `linux-turbo`): hanno `VCPKG_MANIFEST_FEATURES` incompleti rispetto ai flag CMake default ON, funzionano solo perché mascherati dai `default-features`. Rimuovere `default-features` da `vcpkg.json` e rendere esplicite tutte le feature in ogni preset.
- [ ] Feature `video` e `ffmpeg` non esistono in `vcpkg.json` — non bloccante perché non hanno dipendenze vcpkg esterne, ma andrebbero aggiunte per coerenza se in futuro richiederanno pacchetti.

## Profili canonici

| Profilo | Scopo | Stato |
|---|---|---|
| `linux-profile-core` | runtime e graph minimi | Isolamento verificato, CI pending |
| `linux-profile-motion` | Blend2D e text | Isolamento verificato, CI pending |
| `linux-profile-video` | video nativo e telemetria | Isolamento verificato, CI pending |
| `linux-profile-extended` | tutte le feature e profiling | Isolamento verificato, CI pending |

## Ordine di chiusura

1. Chiudere `linux-lean-dev` e il consumer SDK.
2. Eseguire build completa (`--build`) per ogni profilo e registrare dimensioni binari.
3. Eseguire la matrice CI dei quattro profili.
4. Allineare i preset non-profile e rimuovere `default-features` da `vcpkg.json`.
5. Documentare il profilo raccomandato per produzione CPU-first.

## Completato quando

Ogni profilo configura, compila, installa ed esegue i test previsti; le dipendenze installate sono coerenti con il profilo; i `default-features` sono rimossi da `vcpkg.json` e ogni preset dichiara esplicitamente le proprie feature; il profilo CPU-first raccomandato è documentato e verificato in CI.
