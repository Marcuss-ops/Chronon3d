# Stato 3D

Questo documento riassume dove siamo oggi sul subsystem 3D di Chronon3d, cosa è già nel motore, cosa resta ancora specializzato e qual è l'obiettivo architetturale.

La direzione è 2.5D, non 3D mesh-based: layer piatti, proiezione prospettica, ordinamento per profondità e rasterizzazione in screen space.

## Cosa c'è già

- `Camera2_5D` esiste e supporta posizione, point of interest, rotazione, zoom/FOV e DOF.
- `project_layer_2_5d()` esiste già e calcola depth, perspective scale, visibilità e trasformazione proiettata.
- Il render graph applica già il pass di trasformazione/proiezione quando serve.
- Il software backend renderizza già:
  - `Rect`
  - `Image`
  - `Text`
  - `FakeBox3D`
  - `FakeExtrudedText`
  - `GridPlane`
- I test di regressione esistono già per:
  - coerenza del transform path
  - proiezione 2.5D
  - compositing e z-order
  - smoke test per `FakeBox3D` e `FakeExtrudedText`

In breve: il sistema non è da inventare, è già presente e funziona. Il lavoro adesso è soprattutto di unificazione e pulizia.

## Convenzione interna attuale

La convenzione pratica del codice è:

- origine 2D in alto a sinistra
- asse `X` verso destra
- asse `Y` verso il basso
- asse `Z` positivo allontana dalla camera
- camera default a `z = -1000`
- piano principale a `z = 0`

Con questa convenzione:

- un layer a `z = 0` con camera a `z = -1000` ha depth `1000`
- se la camera zooma a `1000`, la scala prospettica è `1`
- se il layer va a `z = -500`, la scala diventa `2`
- se il layer va a `z = 1000`, la scala diventa `0.5`

Questa è la base corretta del 2.5D.

## Dove deve stare la logica vera

La logica vera deve stare nel motore, non nei test.

Già oggi il repo sta andando in quella direzione:

- la proiezione vive in `include/chronon3d/math/camera_2_5d_projection.hpp`
- il render graph la usa nel pass di trasformazione
- la parte condivisa di analisi framebuffer sta nel backend software
- i test usano i helper del motore e non ricalcolano la matematica da soli

Il principio è:

- i test sono i nervi
- il motore è il sistema nervoso
- i test non devono contenere la verità matematica
- la verità matematica deve stare sotto `include/` e `src/`

## Cosa manca ancora

Le parti ancora da rifinire sono queste:

- un unico projector 2.5D condiviso da tutti i path software
- meno duplicazione tra `FakeBox3D`, `FakeExtrudedText` e `GridPlane`
- una convenzione esplicita e unica per depth, focal, view matrix e screen mapping
- test più forti sulla parallasse e sull'ordinamento in profondità
- estensione del path unificato ai layer ruotati X/Y con proiezione dei quattro corner, non solo scala prospettica
- separazione più netta tra:
  - trasformazione del layer
  - proiezione camera
  - rasterizzazione
  - compositing

## Obiettivo

L'obiettivo è arrivare a un sistema in cui:

- `Rect`, `Image`, `Text`, `FakeBox3D`, `FakeExtrudedText` e `GridPlane` parlano tutti la stessa lingua matematica
- la camera produce un solo risultato coerente per tutti i tipi di layer
- i renderer specializzati cambiano solo geometria e shading, non la proiezione
- i test descrivono scenari, non implementano matematica
- non esistono due verità diverse tra test, backend e render graph

## Sequenza consigliata

1. Stabilire definitivamente la convenzione matematica interna.
2. Tenere `project_layer_2_5d()` come sorgente di verità per il 2.5D.
3. Usare un helper condiviso per camera setup e projector nel backend software.
4. Ridurre la matematica duplicata nei renderer specializzati.
5. Aggiungere test mirati su:
   - scala con depth
   - parallasse con pan camera
   - visibilità dietro la camera
   - z-order tra layer vicini e lontani
6. Estendere il path unificato ai layer ruotati X/Y quando il comportamento base è stabile.

## Definizione di fatto

Possiamo dire che il 3D 2.5D è in uno stato maturo quando:

- i test `*Unified*` restano verdi
- la matematica camera/proiezione sta solo nel motore
- i renderer specializzati usano helper condivisi
- il test file non contiene logica duplicata
- il comportamento visivo è prevedibile e coerente tra tutti i path

## Stato attuale in una frase

Il 2.5D esiste già, funziona, e ora il lavoro vero è trasformarlo da insieme di path compatibili a sistema unico e condiviso.
