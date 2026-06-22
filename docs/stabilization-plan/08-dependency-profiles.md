# Profili dipendenze e build

## Stato reale

I quattro profili e lo script di misura esistono già. Restano da chiudere la validazione CI, l'allineamento completo con vcpkg e la riduzione delle feature predefinite.

## Implementato

- [x] Profilo core senza testo, video, mesh ed EXR.
- [x] Profilo motion con Blend2D e testo.
- [x] Profilo video separato.
- [x] Profilo extended per mesh, EXR, telemetria e profiling.
- [x] Preset CMake dedicati ai quattro profili.
- [x] Script `tools/measure_profile.sh`.
- [x] Output di misura JSON e Markdown.
- [x] Documentazione di utilizzo dello script.

## Ancora aperto

- [ ] Allineare completamente preset CMake e feature vcpkg.
- [ ] Verificare che ogni profilo installi solo le dipendenze necessarie.
- [ ] Ridurre le feature predefinite non essenziali.
- [ ] Testare ogni profilo in CI.
- [ ] Registrare tempi di configure e build su una baseline comune.
- [ ] Registrare dimensione di SDK, CLI e target test.
- [ ] Registrare il numero effettivo di dipendenze per profilo.
- [ ] Documentare il profilo raccomandato per produzione CPU-first.
- [ ] Verificare il consumer SDK almeno per core e motion.

## Profili canonici

| Profilo | Scopo | Stato |
|---|---|---|
| `linux-profile-core` | runtime e graph minimi | Implementato, CI pending |
| `linux-profile-motion` | Blend2D e text | Implementato, CI pending |
| `linux-profile-video` | video nativo e telemetria | Implementato, CI pending |
| `linux-profile-extended` | tutte le feature e profiling | Implementato, CI pending |

## Ordine di chiusura

1. Chiudere `linux-lean-dev` e il consumer SDK.
2. Verificare coerenza preset-feature vcpkg.
3. Eseguire la matrice CI dei quattro profili.
4. Registrare misure confrontabili.
5. Ridurre i default soltanto dopo aver verificato che core e motion coprano i casi produttivi.

## Completato quando

Ogni profilo configura, compila, installa ed esegue i test previsti; le dipendenze installate sono coerenti con il profilo; il profilo CPU-first raccomandato è documentato e verificato in CI.