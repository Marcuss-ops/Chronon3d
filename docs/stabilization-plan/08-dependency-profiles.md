# Profili dipendenze e build

- [ ] Definire profilo core senza testo, video, mesh ed EXR.
- [ ] Definire profilo motion con Blend2D e testo.
- [ ] Definire profilo video separato.
- [ ] Definire profilo extended per mesh, EXR e telemetria.
- [ ] Ridurre le feature predefinite non essenziali.
- [ ] Allineare preset CMake e feature vcpkg.
- [ ] Testare ogni profilo in CI.
- [ ] Misurare tempi, dimensioni e dipendenze installate.
- [ ] Documentare quale profilo usare in produzione.

## Completato quando

Ogni build installa solo ciò che serve e la configurazione CPU-first rimane semplice, ripetibile e leggera.

## Implementazione eseguita

Deliverable concreti (a partire dal commit `359fef38` e successivi
fix in `8c2fb…`/etc.):

- [`../../tools/measure_profile.sh`](../../tools/measure_profile.sh) —
  script bash canonico che, per un `linux-profile-{core,motion,video,extended}`
  dato, esegue `cmake --preset`, misura tempo configure e numero deps
  vcpkg, opzionalmente builda, misura dimensione dei binari
  (`chronon3d_sdk_impl.a`, `chronon3d_cli`, `chronon3d_tests_fast`),
  scrive JSON + Markdown in `build/profiles/<name>/`.
- [`../../CMakePresets.json`](../../CMakePresets.json) — quattro
  nuovi `configurePresets` nominati secondo la matrice del piano
  08: `linux-profile-core` (Release, minimo), `linux-profile-motion`
  (Release, blend2d + text), `linux-profile-video` (Release, native
  ffmpeg + telemetry), `linux-profile-extended` (Release, tutto,
  unity OFF, profiling ON). Ogni preset ha `displayName` e
  `description` consultabili via `cmake --list-presets`.
- [`../../docs/measure_profile_readme.md`](../../docs/measure_profile_readme.md) —
  documenta l'uso dello script, la tabella profilo→preset, la CI
  matrix proposta, la manutenzione.

Collegamento al gate `D3` del piano 07:
`tools/check_doc_sync.sh` (regola `R4`) richiede che ogni modifica
a `vcpkg.json` o `CMakePresets.json` sia accompagnata da un update
di questo file. Quando si aggiunge un nuovo preset profilo,
riprodurre la riga nella tabella "Profili" sopra.
