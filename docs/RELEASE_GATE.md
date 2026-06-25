# Chronon3D — Release Gate

Questo documento definisce i criteri tecnici minimi per considerare verificato un commit.
Lo stato prodotto è in [`CURRENT_STATUS.md`](CURRENT_STATUS.md); le milestone sono in
[`ROADMAP.md`](ROADMAP.md).

## Piattaforma supportata

Chronon3D è Linux-only. Non sono richiesti build, artifact o test Windows.

## Gate obbligatori

- build core;
- build SDK con testo;
- header pubblici compilabili standalone;
- installazione + `find_package(Chronon3D CONFIG REQUIRED)` da consumer esterno;
- controlli architetturali;
- controllo confine SoftwareRenderer/SoftwareBackend;
- controllo drift dei file citati;
- test camera e testo mirati;
- render dello showcase camera + testo;
- output riproducibile sullo stesso commit.

## Regole

- nessun gate può essere indebolito per adattarlo al codice;
- nessun test può essere skipped per nascondere un errore;
- nessun `continue-on-error` sui gate bloccanti;
- una baseline non è verde finché build, test, consumer, render e documenti non
  riportano lo stesso stato sullo stesso commit;
- su `main` scrive un solo agente alla volta;
- branch e PR vengono usati soltanto quando richiesti esplicitamente.

## Camera + Text Production V1

La milestone è verificata quando:

1. `OrientAlongPath` usa la tangente campionata;
2. trajectory source preserva projection, lens, DOF, motion blur e parent;
3. banking e keep-horizon sono coperti da test;
4. quattro preset testuali cambiano realmente nel tempo;
5. lo showcase contiene almeno tre piani con parallasse;
6. vengono verificati frame iniziale, intermedio e finale;
7. la CLI genera PNG e MP4 su Linux CPU;
8. due render degli stessi frame chiave producono lo stesso risultato.

## SDK Product V1

La milestone è verificata quando un progetto esterno:

1. installa e trova `Chronon3D::SDK`;
2. include soltanto header pubblici;
3. registra un extension pack;
4. costruisce testo e camera;
5. renderizza un frame reale;
6. scrive un file valido;
7. non collega target interni.
