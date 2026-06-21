# Determinismo scheduler e rendering

## Problema

I test confrontano più scheduler, ma restano hash non acquisiti e percorsi composite o tile non completamente validati.

## TODO

- [ ] Sostituire i sentinel con golden hash reali.
- [ ] Validare Sequential, TBB 1, 2, 4 e automatico.
- [ ] Ripetere ogni scenario più volte.
- [ ] Testare cache fredda e calda.
- [ ] Riattivare la scena composite.
- [ ] Aggiungere un FakeBackend con compositing deterministico.
- [ ] Aggiungere il percorso tile.
- [ ] Testare frame diversi e scene animate.
- [ ] Salvare seed, hash osservati e piattaforma.

## Test richiesti

- output bit-for-bit uguale tra scheduler;
- hash uguale al golden atteso;
- stesso numero di visite per nodo;
- nessuna race con sanitizer quando disponibile.

## Completato quando

Tutti i percorsi seriali, paralleli, composite e tile producono lo stesso output atteso senza skip temporanei.
