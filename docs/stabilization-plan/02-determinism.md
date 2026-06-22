# Determinismo scheduler e rendering

## Stato reale

La copertura è avanzata, ma il contratto non è ancora chiuso perché i golden hash numerici non sono stati acquisiti e resta aperto il FakeBackend composite deterministico.

## Completato

- [x] Validare Sequential, TBB 1, 2, 4 e automatico.
- [x] Ripetere gli scenari più volte.
- [x] Testare cache fredda e calda.
- [x] Riattivare la scena composite reale.
- [x] Aggiungere il percorso tile.
- [x] Testare frame diversi e scene animate.
- [x] Salvare la procedura per seed, hash osservati e piattaforma.

## Ancora aperto

- [ ] Sostituire tutti i sentinel con golden hash numerici reali.
- [ ] Risolvere il blocker toolchain/backend che impedisce la cattura pulita.
- [ ] Eseguire da checkout e build puliti i test di acquisizione.
- [ ] Copiare i valori osservati nelle costanti `kRefBaseline*`.
- [ ] Rimuovere il comportamento dormant basato su `kUncapturedSentinel`.
- [ ] Fare fallire il test quando il golden non è valorizzato.
- [ ] Aggiungere un FakeBackend con compositing deterministico.
- [ ] Riattivare ogni test ancora legato a TICKET-013.
- [ ] Verificare stesso numero di visite per nodo.
- [ ] Eseguire sanitizer o race detector quando disponibile.

## Procedura golden hash

1. Chiudere prima baseline, test falliti, boundary e `linux-lean-dev`.
2. Costruire il target deterministico da directory pulita.
3. Eseguire ogni scenario su piattaforma e toolchain registrate.
4. Verificare che le ripetizioni producano un solo hash per scenario.
5. Inserire i valori numerici nelle costanti di riferimento.
6. Rieseguire seriale, TBB, composite, tile, cold cache e warm cache.
7. Registrare SHA, compilatore, worker count e hash finali.

## Test richiesti

- output bit-for-bit uguale tra scheduler;
- hash uguale al golden numerico atteso;
- stesso numero di visite per nodo;
- nessuna race rilevata nei percorsi concorrenti;
- nessun test saltato per sentinel non acquisito.

## Completato quando

Tutti i percorsi seriali, paralleli, composite e tile producono lo stesso output numerico atteso e nessuna costante golden usa ancora `0xDEADBEEFDEADBEEF` o un altro placeholder.