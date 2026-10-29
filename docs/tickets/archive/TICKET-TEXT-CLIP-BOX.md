# TICKET-TEXT-CLIP-BOX — enforce the authored box for `TextOverflow::Clip`

**Stato:** CLOSED for the software TextRun renderer (2026-10-29)
**Priorità:** P3 — non blocking
**Owner:** text domain
**Reference test:** `tests/text/test_text_clip_oversized.cpp` (authored-size and pixel-containment `CHECK`s)

<!-- protocollo-chiusura-completa:2026-10-29:v1 -->

## Protocollo di chiusura completa (2026-10-29)

> Autorità dedicata: sezione **49 — enforcement visivo del clipping** di `../../../PIANO-CHIUSURA-TICKET-APER-TI-2026-10-29.md` (fase consigliata **F1** · priorità **P3**).
> Questo blocco rende la scheda autosufficiente: riporta qui i criteri dedicati alla chiusura e le regole valide per dichiararla `COMPLETE`.
> Fotografia dell'audit 2026-10-29: prima di eseguire un criterio, verificane l'attualità contro il codice, i test e le evidenze già presenti in questa scheda.

### Criteri dedicati

- [x] Applicare clipping dell’ink al box dichiarato nella rasterizzazione TextOverflow::Clip senza auto-fit/ellipsis e mantenendo font size authored.
- [x] Promuovere pixel containment di `tests/text/test_text_clip_oversized.cpp` da WARN a CHECK; il test prova prima l’overflow con `Visible`, poi i quattro bordi con `Clip`, e verifica font size invariato.
- [x] Telemetria strutturata opzionale non richiesta dal prodotto; non aggiunta.

### Regola di chiusura (valida per questa e ogni scheda)

1. Separa sempre **implementazione locale**, **test locale**, **parità GPU o asset/riferimenti esterni**, **CI same-SHA** e **decisioni di prodotto**; una categoria non sostituisce l'altra.
2. Registra per ogni prova: comando completo, SHA (clean o dirty), preset build, host/device, exit status, contatori (test/assert/frame/hash/digest) e percorso dell'evidenza sotto `docs/evidence/`.
3. I gate esterni (runner, content pack, footage, host GPU, decisione di owner) non si chiudono localmente: lo Stato resta `PARTIAL`/`BLOCKED` o `WAITING EXTERNAL` con prerequisito e owner espliciti. Non trasformare mai `NOT RUN`/`FAIL` in `PASS`.
4. Non rimuovere fallback, API o capability fail-closed prima del relativo gate di equivalenza/census; non implementare feature proposte senza la decisione di scope.
5. Chiusura effettiva = tutti i criteri dedicati verdi (oppure formalmente ritirati con motivazione e owner) + aggiornamento di `## Stato` in questa scheda, degli indici `docs/CURRENT_STATUS.md`/`docs/FOLLOWUP_TICKETS.md` se la scheda è indicizzata, di `TODO-RIPRESA-100X-E-TICKET-APER-TI.md`, e archiviazione della scheda secondo le convenzioni del repository.

### Percorso rapido (ordine di esecuzione consigliato)

1. Riconfronta i criteri dedicati qui sopra con lo stato attuale della scheda: le voci già soddisfatte non si rieseguono, si ricollegano all'evidenza esistente.
2. Frammenta l'esecuzione in micro-step: chiudi prima le azioni locali raggiungibili nel checkout corrente, poi le decisioni di contratto/scope aperte, infine prepara e richiedi i gate esterni del piano.
3. Dopo ogni criterio, spontalo in questa sezione con il link all'evidenza; annota nella stessa sezione `NOT RUN`/`BLOCKED` dove la prova non è disponibile, senza cancellare il criterio.
4. Quando tutti i criteri dedicati sono verdi (o ritirati in modo formale), applica la Regola di chiusura (punto 5), aggiorna gli indici canonicali e archivia la scheda.

<!-- /protocollo-chiusura-completa:2026-10-29:v1 -->

## Chiusura locale (2026-10-29)

- Il renderer software taglia la superficie TextRun nel box locale dopo raster/material effects; le ombre sono tagliate allo stesso box compensando il proprio offset. Il taglio precede la trasformazione, quindi segue la rotazione e la scala del testo.
- `TextOverflow::Visible` conserva l’overflow. `TextOverflow::Clip` mantiene font size authored e aggiorna il bbox di visibilità con l’intersezione al box.
- Build del target e test mirato: PASS, 14/14 asserzioni. Evidenza: [`text-clip-box-20261029`](../../evidence/text-clip-box-20261029/README.md).
- Telemetria strutturata opzionale non richiesta. Nessuna certificazione Vulkan TextRun o CI clean-SHA è inclusa in questa chiusura locale.

## Semantica della limitation (machine-verified)

`TextOverflow::Clip` (`include/chronon3d/text/text_definition.hpp` + `text_layout_spec.hpp`) keeps
the authored font size and clips visible software TextRun ink at the declared box. It does not
enable auto-fit. `TextOverflow::Visible` continues to allow overflow.

Verifica: `verify_text_visibility()` (`src/text/text_visibility_audit.cpp` +
`src/text/text_visibility_reporting.cpp`) ora riceve il bbox di ink intersecato
con il box dichiarato; `clip_contains_visible_ink` rimane vero entro la
tolleranza `kTextAuditBBoxTolerance`. Non serve una nuova authority di
telemetria per applicare il contratto.
- `tests/text/test_text_clip_oversized.cpp` verifies visible overflow in `Visible`, containment
  in `Clip`, and an unchanged 200 px materialized font size.

## Decisione di policy

Il contenimento ink-in-box richiede una decisione di policy di layout per il testo oversized:

- auto-fit (ADR-018 lineage) sul font size;
- ellipsis/truncation;
- clip documentato dell'ink al box dichiarato.

La decisione di prodotto è esplicita: `Clip` non riduce il font; l'auto-fit è attivato solo dal
flag dedicato previsto da ADR-018. Il renderer applica ora il taglio richiesto dal contratto.

## Exit condition (per chiudere)

1. The text renderer clips visible ink to the authoring box when `TextOverflow::Clip` is active.
2. `test_text_clip_oversized.cpp` verifies pixel containment while also verifying that the
   authored font size remains unchanged.
3. (Opzionale) `[text-vis] CLIP_DROPS_INK` promosso a campo telemetry strutturato
   (node/layer id + box vs ink bbox), mantenendo il warn-once.

## Fuori confine

- `predicted_bbox`/compositor clip prediction (root cause diverso:
  `TICKET-TEXT-CLIP-PREDICTED-BBOX`, referenziato da `TICKET-TEXT-OVERSIZED-CLIP-WARN`, scheda rimossa il 2026-10-27 come duplicata).
- Qualsiasi modifica al percorso RenderingGen stabile (golden canary invariato).

## Cross-link

- Parent warning: `TICKET-TEXT-OVERSIZED-CLIP-WARN` (scheda rimossa il 2026-10-27 come duplicata: la diagnostica WARN vive in `tests/text/test_text_clip_oversized.cpp`)
- Regression lock: `tests/text/test_text_clip_oversized.cpp`
- Policy decision pointer: `../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md`
