# Registry centrale dei moduli CMake

## Stato reale

Il registry centrale non è ancora implementato. Il lavoro deve iniziare solo dopo la chiusura della baseline, del preset `linux-lean-dev` e del consumer SDK, così il refactor CMake parte da una configurazione verificata.

## Problema

Gli stessi target sono elencati in più sezioni per build, archivio SDK, installazione ed export. Una nuova OBJECT library può compilare in-tree ma mancare nell'archivio SDK o nell'installazione.

## Prerequisiti

- [ ] Test veloci verdi.
- [ ] Gate architetturali verdi.
- [ ] `linux-lean-dev` verde.
- [ ] Consumer SDK esterno verde.
- [ ] Record della baseline aggiornato.

## TODO — inventario

- [ ] Elencare tutte le OBJECT library.
- [ ] Elencare tutte le liste manuali di target SDK.
- [ ] Elencare target installati ed esportati.
- [ ] Elencare feature condizionali e rispettive guardie.
- [ ] Individuare target presenti in una lista ma assenti nelle altre.
- [ ] Registrare la baseline dell'archivio SDK prima del refactor.

## TODO — implementazione

- [ ] Creare `cmake/Chronon3DModules.cmake`.
- [ ] Definire una funzione `chronon3d_register_module`.
- [ ] Registrare ogni modulo una sola volta.
- [ ] Derivare automaticamente la lista OBJECT per l'archivio SDK.
- [ ] Derivare automaticamente install ed export target.
- [ ] Gestire nello stesso record le feature condizionali.
- [ ] Impedire la registrazione duplicata dello stesso target.
- [ ] Fallire la configurazione se una OBJECT library pubblicabile non è registrata.

## Contratto suggerito

Ogni modulo deve dichiarare almeno:

```text
name
target
category
sdk_object yes/no
install yes/no
feature guard optional
public dependencies
private dependencies
```

Il registry deve essere la sola fonte delle liste derivate. Non aggiungere una nuova lista parallela.

## Test richiesti

- [ ] Build core.
- [ ] Build motion/text.
- [ ] Build video.
- [ ] Build extended.
- [ ] Build no-content.
- [ ] Installazione locale.
- [ ] Consumer esterno.
- [ ] Verifica simboli dell'archivio SDK.
- [ ] Canary test con modulo registrato una sola volta.
- [ ] Canary test con OBJECT library volutamente non registrata.

## Completato quando

Aggiungere un modulo richiede una sola dichiarazione e nessuna lista parallela da aggiornare manualmente. Build, installazione, export e consumer devono derivare dallo stesso registry.