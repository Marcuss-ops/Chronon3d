# TICKET-TRN-06 — Certification matrix for existing transitions

## Stato: DONE

## Problema
Prima di aggiungere nuove transizioni (ClipTransitionNode, nuovi tipi, ecc.) è necessario certificare che le transizioni esistenti siano stabili e deterministiche su una matrice minima di dimensioni.

## Matrice di certificazione coperta

| Dimensione | Valori testati |
|------------|----------------|
| Aspect ratio | 16:9 (160×90), 9:16 (90×160) |
| Lato | `transition_in` e `transition_out` |
| Durate | 1, 2, 10, 30 frame @ 30 fps |
| Easing | Linear, InQuad, OutQuad, InCubic |
| Direzioni | Left, Right, Up, Down per slide/wipe |
| Frame campione | start, middle, end |
| Cache | cold (fresh renderer) e warm (stesso renderer, second pass) |
| Accesso | sequenziale vs casuale determinismo |
| Contenuto | opaco e semi-trasparente |
| Scheduler | Sequential vs TbbAutomatic |

## File di test

- `tests/render_graph/features/test_transition_certification.cpp`

## Fix applicato durante la certificazione

Il test di parità tra scheduler serial/parallel impostava la variabile d'ambiente `CHRONON3D_SCHEDULER_MODE` usando `scheduler_mode_name()`, che restituisce nomi in title-case (`TbbFixed`, `TbbAutomatic`). Il parser `parse_scheduler_mode()` invece accetta solo token lower-case (`fixed`, `auto`, `sequential`). Di conseguenza entrambi i modi cadevano silenziosamente nel default.

Corretto aggiungendo `scheduler_mode_env_name()` che mappa `SchedulerMode` ai token riconosciuti dal parser.

## Risultato

Tutti i test TRN-06 passano (11 test, 0 fallimenti).
