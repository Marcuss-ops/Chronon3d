# SDK e install consumer

## Stato reale

Il consumer esterno non è verde. Il configure standalone non trova correttamente dipendenze come `spdlog` perché toolchain e prefix vcpkg non sono propagati in modo coerente.

## TODO

- [ ] Riprodurre il fallimento da directory pulite.
- [ ] Registrare il comando CMake esatto del consumer.
- [ ] Verificare `CMAKE_TOOLCHAIN_FILE`.
- [ ] Verificare `CMAKE_PREFIX_PATH`.
- [ ] Verificare la posizione delle dipendenze vcpkg installate.
- [ ] Controllare `Chronon3DConfig.cmake` e i file export.
- [ ] Usare `find_dependency` per le dipendenze pubbliche necessarie.
- [ ] Verificare la link interface di `Chronon3D::SDK`.
- [ ] Installare in un prefix temporaneo vuoto.
- [ ] Configurare un consumer esterno senza accesso al source tree.
- [ ] Collegare soltanto `Chronon3D::SDK`.
- [ ] Compilare ed eseguire il consumer.

## Un solo test canonico

- [ ] Usare un solo progetto in `tests/install_consumer/`.
- [ ] Usare un solo script di orchestrazione.
- [ ] Chiamare lo stesso script da locale, CTest e CI.
- [ ] Eliminare consumer generati direttamente nel workflow.

## Completato quando

Installazione, configurazione, build ed esecuzione del consumer terminano con successo usando soltanto `find_package(Chronon3D)` e `Chronon3D::SDK`.