# TICKET-WATCH-PROPS-FILE — Apply --props-file in watch subprocess

## Stato: DONE (2026-07-15)

## Problema risolto

`chronon watch --props-file props.json` veniva accettato dal subcommand ma
non raggiungeva il subprocess `chronon render`. La preview poteva quindi
mostrare dati diversi dal render normale.

## Soluzione atterrata

Il supervisor ora:

1. passa `--props-file` al medesimo comando `chronon render` usato nel percorso normale;
2. include il file props nello snapshot degli mtime, anche quando vive fuori da `src/`, `include/` e `apps/`;
3. rilancia build e render quando il JSON cambia;
4. usa quoting shell per binary, composition ID, output e props file;
5. autodetecta il build root cercando `CMakeCache.txt` sopra il binary, eliminando il vecchio default hardcoded `bash build-fast.sh`;
6. mantiene `--build` e `--no-build` come override espliciti.

Flusso stabile:

```bash
chronon watch ProductLaunch \
  --frame 60 \
  --props-file props.json \
  -o /tmp/preview.png
```

Il daemon resta una warm render shell con cache persistenti; il supervisor
watch è l'unico responsabile del rebuild e del re-exec del nuovo binary.

## Commit di chiusura

- `a640cc77` — forwarding props e build auto-detect
- `32b1f869` — props file incluso negli mtime osservati
