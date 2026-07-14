# TICKET-PREVIEW-CONTACT-SHEET — Contact-sheet composer for chronon preview (Commit 3b)

## Stato: OPEN (Commit 3b of Blocco 4.1, deferred from Commit 3a)

## Problema
`chronon preview --contact-sheet sheet.png` viene accettato dal
subcommand (apps/chronon3d_cli/commands/preview/register_preview_commands.cpp)
ma NON è ancora implementato (Commit 3a emette solo un warning one-time).

L'audit §18 verbatim cita la seconda fase:
```
chronon preview ProductLaunch \
  --frames 0,30,60,90 \
  --contact-sheet sheet.png
```
come capability attesa.

## Soluzione accettabile
1. Dopo aver renderizzato tutti i frame in `<output_dir>/frame_NNNN.png`:
   - Load ogni PNG via `load_image_as_framebuffer(path, cell_width, -1)`.
     Il `cell_width` (default 640) è il target width per il resize
     aspect-preserving.  `target_height = -1` mantiene l'aspect ratio.
   - Costruisci un `std::shared_ptr<Framebuffer> grid` di dimensione:
     - cols = ceil(sqrt(N))
     - rows = ceil(N / cols)
     - grid_w = cols * cell_w + (cols + 1) * cell_padding
     - grid_h = rows * cell_h + (rows + 1) * cell_padding
     dove `cell_w`, `cell_h` sono le dimensioni del PRIMO frame
     caricato (dopo il resize aspect-preserving).
2. Per ogni frame al cell (col, row):
   - Calcola la posizione top-left: x = padding + col * (cell_w + padding),
     y = padding + row * (cell_h + padding).
   - Blit il Framebuffer sorgente nel grid via un helper in-file
     `blit(Framebuffer& dst, const Framebuffer& src, int x, int y)`
     (~50 LoC, no new public SDK API).
3. Salva il grid via `save_png(*grid, contact_sheet_path)`.

## Vincoli
- Niente nuove librerie (no stb_image_resize, no ImageMagick `montage`).
- Niente nuove public SDK API (no `Framebuffer::blit_from`).
- Niente nuovi singleton/registry/resolver/cache.
- Tutto in-file nel `register_preview_commands.cpp` (sibling del
  `copy_with_substitution` precedent in `register_create_commands.cpp`).

## Workaround attuale
Gli utenti possono usare ImageMagick esternamente:
```
montage preview/frame_*.png -tile 2x2 -geometry +8+8 sheet.png
```
ma è un workaround shell, non l'integrazione canonica richiesta dall'audit.

## Criteri di accettazione
- `chronon preview ProductLaunch --frames 0,30,60,90 --contact-sheet sheet.png`
  produce un file `sheet.png` con i 4 frame in un grid 2x2.
- I frame sono scalati a `--cell-width` (default 640) con aspect ratio
  preservato.
- Un padding di `--cell-padding` px (default 8) separa le celle.
- Il warning one-time di Commit 3a viene rimosso.
- Subject envelope del Commit 3b ≤ 72 char.
