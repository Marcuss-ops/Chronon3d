# TICKET-TEXT-INK-BBOX-REPIN — `local_ink_bbox` 156 contro pin 116

- **Stato:** CLOSED — 2026-11-16
- **Chiusura:** ricalcolo manuale: gx=i*40, bbox_x0=-8/bbox_x1=68 → [-8,148] size 156 (il pin 116 era advance-based, stale da `6d939732b`). Pin aggiornati a 156 + expanded 196 + guardia ink-inside-bbox. Verifica: `chronon3d_visibility_contract_tests` 7/7, 79/79 PASS su release `30b4b721a` + working tree.
- **Ambito:** Chronon3d, text visibility contract (FU04), GEL text
- **Origine:** suite completa 2026-11-16, `chronon3d_visibility_contract_tests`

## Sintomo

Tre asserzioni dello stesso contratto falliscono:

```
tests/text/test_visibility_contract.cpp:301: CHECK( audit.local_ink_bbox.size.x == doctest::Approx(116.0f) )
  values: CHECK( 156 == Approx( 116 ) )
tests/text/test_visibility_contract.cpp:307: CHECK( audit.world_ink_bbox.size.x == doctest::Approx(116.0f) )
  values: CHECK( 156 == Approx( 116 ) )
tests/text/test_visibility_contract.cpp:342: CHECK( audit.world_ink_bbox.size.x == doctest::Approx(116.0f) )
  values: CHECK( 156 == Approx( 116 ) )
```

Il resto del binario passa; `size.y` resta `33.8` come da pin. La divergenza è
solo sull'asse X.

## Contesto

Il commento del caso #5 deriva 116 dal modello **advance-based**:

```
min_x = -8, max_x = 108  →  size.x = 116
```

`compute_text_run_visual_bounds()` (`src/text/text_run_geometry.cpp:52`) è stato
però riscritto per usare i bbox FreeType per glifo (`placed.glyphs[i].bbox_*`)
al posto dell'approssimazione sull'advance, e include stroke, blur e shear. La
modifica arriva con `6d939732b` (GEL text / 2.5D lighting-text parity), mentre
il pin 116 risale a `53507157b` (FU04 ink bbox). Il valore 156 è probabilmente
la nuova geometria reale, ma **non è stato verificato con un riferimento
visivo**: per questo non è stato aggiornato a mano.

## Piano di chiusura

1. **Ricalcolare a mano.** Con la fixture `make_test_shape(with_engine=true,
   glyph_count=3, with_glyphs=true)`, applicare la formula corrente di
   `compute_text_run_visual_bounds` (bbox outline + pad) e ricavare
   analiticamente `min_x`/`max_x`; verificare che 156 discenda da lì e non da un
   doppio conteggio `layout_position + position`.
2. **Misurare l'inchiostro.** Produrre una rasterizzazione della stessa forma
   (o passare `rendered_output` ad `audit_text_visibility`) e misurare
   l'alpha-bbox: `world_ink_bbox` deve contenere tutto l'inchiostro, senza
   clip e senza eccedere oltre il necessario.
3. **Decidere.**
   - Se 156 è corretto e l'inchiostro è dentro: aggiornare i tre pin e il
     commento di derivazione nel test, in un commit unico con l'evidenza.
   - Se 156 è troppo grande: correggere il calcolo (probabile doppio conteggio
     della posizione) e mantenere 116.
4. **Guardia.** Aggiungere al caso un controllo esplicito
   «ink dentro il bbox» sulla rasterizzazione, così un futuro re-pin non può
   nascondere un clip.

## Criteri di accettazione

- [x] I tre pin sono coerenti con una derivazione scritta nel test.
- [x] La misura alpha-bbox mostra l'inchiostro interamente contenuto.
- [x] Nessun pin aggiornato senza riferimento visivo o rasterizzato.
- [x] `chronon3d_visibility_contract_tests` verde senza allargare le tolleranze.

## Riferimenti

- `Chronon3d/tests/text/test_visibility_contract.cpp:260-350`
- `Chronon3d/src/text/text_run_geometry.cpp:34-150`
- Commit `53507157b` (pin FU04), `6d939732b` (GEL text)
