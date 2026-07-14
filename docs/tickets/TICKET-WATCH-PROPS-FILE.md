# TICKET-WATCH-PROPS-FILE — Apply --props-file in watch subprocess (V0 forward-point)

## Stato: OPEN (P2, depends on TICKET-ADD-LOADER-FOR-CHRONON-JSON P1)

## Problema
`chronon watch --props-file props.json` viene accettato dal subcommand
(`apps/chronon3d_cli/commands/watch/register_watch_commands.cpp`) ma
NON viene applicato al subprocess `chronon render` eseguito ad ogni
cambio file.  L'audit §17 verbatim cita:
```
chronon watch ProductLaunch \
  --frame 60 \
  --props-file props.json \
  -o /tmp/preview.png
```
come flow aspirazionale, non come capability attuale.

## Soluzione accettabile
Quando `TICKET-ADD-LOADER-FOR-CHRONON-JSON` (P1) atterra il
per-composition props decoder, il watch supervisor deve:

1. Leggere `props_file` (JSON) all'inizio del ciclo di polling.
2. Passarlo al subprocess `chronon render` come argomento `--props-file`.
3. Rileggerlo ad ogni `mtimes_changed` (un cambio al file props conta
   come cambio sorgente → re-render automatico).

## Dipendenze
- **TICKET-ADD-LOADER-FOR-CHRONON-JSON** (P1, OPEN): il loader JSON che
  decodifica i props per-composizione.  Senza questo, passare
  `--props-file` a `chronon render` non ha alcun effetto.

## Workaround attuale
Gli utenti possono eseguire `chronon render <comp> --props-file props.json`
manualmente, fuori dal loop del watch.  Il watch non blocca questo flow
— semplicemente non lo automatizza.
