# TICKET-TRN-05 — Camera transition cleanup

## Stato: DONE

## Problema
Il sistema camera aveva quattro questioni aperte:

1. Overlap tra shot non era modellato come un vero periodo in cui entrambi gli shot esistono.
2. Durata di 1 frame produceva `t=0` invece di un cut diretto al target.
3. Esisteva stato sessione duplicato tra `ShotTimelineResolver` e `ShotTimelineSession`.
4. Il resolver aveva factory locali oltre al catalogo.

## Soluzione accettata

### 1. Overlap reale
Durante l'overlap entrambi gli shot vengono valutati ai rispettivi tempi locali:

```cpp
int local_frame = frame - shot.start_frame;
int next_local  = frame - pair.next->start_frame;
```

Il next shot inizia a `shot.end_frame - shot.transition_frames` e il suo tempo locale è clippato a `>= 0` perché non ha ancora ufficialmente iniziato.

### 2. Durata 1 frame
`calculate_transition_t` restituisce `1.0f` quando `transition_frames == 1`, cioè cut istantaneo al target.

### 3. Rimozione stato duplicato
`ShotTimelineSession` ora possiede solo `CameraSessionCache cache`. Il resolver riceve il riferimento alla cache e non mantiene più una sua cache privata.

### 4. Catalogo come unica fonte
`ShotTimelineResolver` prende le transizioni esclusivamente da `CameraTransitionCatalog`. Kind non registrati cadono in `CutTransition` (fail-closed). Gli alias legacy (`Push`, `WhipPan`, `FocusHandoff`) risolvono lo stesso oggetto delle controparti canoniche.

## Test

- `tests/scene/camera/test_shot_timeline.cpp` — test esistenti di endpoint, durata 1 frame, true overlap.
- `tests/scene/camera/test_shot_timeline_random_access.cpp` — nuovo test TRN-05 "direct jump into overlap equals sequential render" che certifica la continuità dell'overlap anche con accesso casuale.

## Commit di chiusura
Aggiunto test di continuità overlap in `tests/scene/camera/test_shot_timeline_random_access.cpp` e creato tracker `docs/tickets/TICKET-TRN-05.md`.
