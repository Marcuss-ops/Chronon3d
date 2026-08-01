# TICKET-PUB-DEPRECATE-REMOVAL — ShapedGlyphLine legacy constructor

## Stato: DONE / ARCHIVED (2026-08-01)

Il costruttore pubblico deprecato di `ShapedGlyphLine` non è più dichiarato
né definito. Il percorso pubblico corrente è la shaping factory
`shape_glyph_line(...)`, con `ShapedGlyphLine::try_shape(...)` come adapter
compatibile già presente.

La rimozione è stata verificata con una scansione dei chiamanti in
`content/`, `src/`, `apps/` e `tests/`: non restano invocazioni del costruttore
legacy. Le eventuali future riduzioni delle free-functions di shaping sono
fuori scope e richiedono un ticket separato con verifica ABI.
