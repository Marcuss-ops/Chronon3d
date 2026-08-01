# TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL — Dead constructor surface

## Stato: DONE / ARCHIVED (2026-08-01)

Il costruttore che accettava `font_size` e `FontSpec` come parametri ignorati
è stato rimosso insieme alla relativa superficie pubblica. La shaping
canonical passa da `shape_glyph_line(...)`; il layout e la misura operano sul
risultato già shaped.

Non riaprire questa scheda per rimuovere le free-functions pubbliche: quella
sarebbe una modifica ABI distinta e va valutata in un ticket Text V1 dedicato.
