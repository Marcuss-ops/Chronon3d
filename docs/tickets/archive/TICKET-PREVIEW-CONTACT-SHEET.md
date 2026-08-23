# TICKET-PREVIEW-CONTACT-SHEET — Contact-sheet composer

## Stato

**DONE (2026-07-15).**

## Soluzione atterrata

`chronon preview` supporta:

```bash
chronon preview ProductLaunch \
  --frames 0,30,60,90 \
  --contact-sheet sheet.png
```

Il comando:

1. costruisce un solo `RenderJob` con `selected_frames`;
2. usa un solo renderer e una sola sessione per tutti i frame;
3. salva i PNG per-frame nell'output directory;
4. carica soltanto i frame realmente prodotti;
5. compone una griglia `ceil(sqrt(N))` x `ceil(N / cols)`;
6. preserva l'aspect ratio con `--cell-width`;
7. applica `--cell-padding`;
8. crea la directory del contact sheet quando necessario;
9. propaga qualsiasi failure nell'exit code del comando.

La composizione usa gli helper immagine canonici e non introduce una preview pipeline separata.

## Criteri di accettazione

- [x] `--contact-sheet` produce un PNG grid;
- [x] frame non contigui renderizzati in un solo `RenderJob`;
- [x] stesso renderer/sessione per l'intera preview;
- [x] aspect ratio preservato;
- [x] padding configurabile;
- [x] nessuna libreria esterna;
- [x] nessuna nuova API SDK pubblica;
- [x] failure propagate nell'exit code;
- [ ] per-cell labels opzionali, tracciate separatamente da `TICKET-PREVIEW-CELL-LABELS`.

## Riferimenti

- `apps/chronon3d_cli/commands/preview/register_preview_commands.cpp`
- `include/chronon3d/timeline/render_job.hpp`
- `tools/verify_cli_render_surface_linux.sh`
