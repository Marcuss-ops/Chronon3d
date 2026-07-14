# TICKET-PREVIEW-CELL-LABELS — Per-cell text labels in chronon preview contact sheet (V0 forward-point)

## Stato: OPEN (P3, V0 nice-to-have)

## Problema
`chronon preview --contact-sheet sheet.png` (Commit 3b of Blocco 4.1)
produce una griglia di celle, ma ogni cella è solo l'immagine del frame
senza alcuna label testuale.  L'audit §18 non cita esplicitamente le
label, ma un contact sheet "production-quality" include tipicamente:
- Numero del frame ("frame 0", "frame 30", "frame 60", "frame 90")
- Timestamp / timecode (opzionale)
- Composition ID (opzionale, per debug)

## Soluzione accettabile
Estendere `compose_contact_sheet` in
`apps/chronon3d_cli/commands/preview/register_preview_commands.cpp` per
disegnare una label testuale nell'angolo in alto a sinistra di ogni
cella.  Requisiti:
- Font: riusare il Text system canonico (chronon3d text pipeline,
  TICKET-TEXT-V1).  Niente nuovo font loader.
- Rendering: chiamare il text-rendering canonico (text_run_driver,
  text_run_shape) per generare un mini Framebuffer di label, poi
  `dst->blit(label_fb, x + cell_padding, y + cell_padding)`.
- Background: opzionale, semitrasparente nero o bianco per leggibilità.
- Disabilitato di default (--with-labels flag), per non rompere il
  contact sheet "pulito" che alcuni utenti preferiscono.

## Vincoli
- Niente nuove librerie (nessun Cairo, nessun FreeType, nessun
  stb_easy_font).
- Niente nuove public SDK API.
- Niente nuovi singleton/registry/resolver/cache.
- La label deve usare il font canonico del progetto (chronon3d text
  pipeline), non un font inline o hardcoded.

## Workaround attuale
Gli utenti possono usare ImageMagick esternamente:
```
montage preview/frame_*.png -tile 2x2 -geometry +8+8 -label '%f' sheet.png
```
ma è un workaround shell, non l'integrazione canonica richiesta.

## Criteri di accettazione
- `chronon preview ... --contact-sheet sheet.png --with-labels` produce
  un contact sheet con label "frame N" su ogni cella.
- Le label usano il font canonico del progetto (no font hardcoded).
- Le label sono opzionali (default off).
- Subject envelope del commit ≤ 72 char.
- macchina-verifica: build + ctest 11/11 verde post-commit.
