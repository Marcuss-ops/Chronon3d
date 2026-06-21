# Registry centrale dei moduli CMake

## Problema

Gli stessi target sono elencati in più sezioni per build, archivio SDK, installazione ed export.

## TODO

- [ ] Creare `cmake/Chronon3DModules.cmake`.
- [ ] Definire una funzione `chronon3d_register_module`.
- [ ] Registrare ogni target una sola volta.
- [ ] Derivare automaticamente liste OBJECT, install ed export.
- [ ] Gestire feature condizionali nello stesso registry.
- [ ] Aggiungere un controllo che trovi OBJECT library non registrate.
- [ ] Verificare build in-tree e installata.
- [ ] Verificare che l'SDK produca un solo archivio pubblico.

## Completato quando

Aggiungere un modulo richiede una sola dichiarazione e nessuna lista parallela da aggiornare manualmente.
