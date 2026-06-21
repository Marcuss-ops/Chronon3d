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
