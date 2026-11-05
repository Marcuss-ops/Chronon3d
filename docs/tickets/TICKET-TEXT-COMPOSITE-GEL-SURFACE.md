# TICKET-TEXT-COMPOSITE-GEL-SURFACE — superficie 2.5D composta per testo glossy

## Stato

**OPEN — gap architetturale e parità visiva non chiusi.** Verifica iniziale
2026-11-05. Il frame Vulkan 1280×720 è riproducibile, ma non corrisponde al
riferimento: il testo legge come contorno ciano sottile, con riflessi discontinui.
Il probe 4K/120 precedente misurava 1,798 ms mediani GPU e 58–111 ms/frame di
conversione/readback; non dimostra né il budget né i 120 fps end-to-end richiesti.

## Obiettivo

Ottenere per il titolo “calories of ingredients” una resa 2.5D gonfia e lucida,
con faccia luminosa, bordi blu/ciano, ombra di contatto, glow e frange ottiche,
vicina al [riferimento fornito](https://drive.google.com/file/d/1Tr3N3MPSoFrC6-x_-hLLTBwQv6KAfGrD/view?usp=drive_link).
Il confronto è di resa ottica, non di mesh o simulazione fisica del gel.

## Diagnosi architetturale

La bombatura attuale viene valutata nel ciclo per-glifo di
`src/backends/vulkan/shaders/text_tile_raster.comp`: per ogni glifo si campiona
il suo atlas MTSDF, si ricostruiscono altezza/normale e si applica lo shading,
poi si compone il fill. In questo modo ogni glifo porta la propria superficie;
il renderer non ha un campo di distanza della sagoma composta del TextRun da
cui ricavare una cresta coerente sulle aste e sulle zone di contatto.

After Effects offre un riferimento concettuale diverso: gli stili di livello
operano sulla sagoma/alpha del livello e poi combinano rilievo, ombre, glow e
compositing. Per Chronon manca quindi una superficie condivisa fra il TextRun e
i suoi effetti, non un altro insieme di parametri per il solo riflesso. Il
percorso GPU deve conservare shaping, layout, atlas e cache esistenti; la nuova
superficie è una modalità del materiale gel, non un secondo renderer tipografico.

La pipeline attuale ha già effetti di layer dopo il rendering del layer nel
graph builder. Il contratto va mantenuto: il pass gel produce un layer RGBA
completo e il glow deve aggiungere l’alone lasciando intatto il testo sorgente.
Un probe locale con `style.glow` ha fatto sparire il titolo; quel difetto va
riprodotto in una fixture persistente e isolato prima di dichiarare glow valido.

## Lavoro richiesto

- [ ] Costruire, solo per il materiale gel, una maschera composta del TextRun
  nello spazio di output e un campo di distanza con banda interna sufficiente
  alla bombatura. Il campo deve rappresentare l’unione dei glifi senza perdere
  i contatori (`e`, `g`, `o`, `a`) o gli spazi fra lettere.
- [ ] Ricavare altezza e normali dal campo composto con profilo limitato e
  stabile vicino al bordo e all’asse mediano. Evitare cuspidi, normali non finite
  e highlight generati esclusivamente dal contorno.
- [ ] Applicare shading ciano glossy sul campo composto: cresta ampia sulla
  faccia, specularità controllata, terminatore morbido, rim e ombra di contatto.
  Mantenere una variante di unione morbida configurabile per glifi che si
  sovrappongono; non fondere gli spazi tipografici per default.
- [ ] Far consumare la stessa maschera agli effetti del layer. Verificare source
  over glow, alpha premoltiplicato e clipping sul pass Vulkan; il glow non deve
  sostituire né attenuare il testo.
- [ ] Aggiungere trasmissione stilizzata dipendente dallo spessore e dispersione
  RGB limitata alla silhouette e alle alte luci. Dichiarare esplicitamente le
  approssimazioni: non chiamare il risultato simulazione ottica fisica.
- [ ] Conservare un solo contratto d’authoring in Chronon3D e ChrononMotion3D,
  inclusi RenderPlan, validazione, fingerprint, camera 2.5D e preview. Nessun
  atlas/font pipeline duplicato.

## Gate di accettazione

Prima prova visiva a 1280×720 contro il riferimento; 4K viene dopo il gate
visivo. Salvare piani, raw, PNG, log GPU e confronto nella directory
`docs/evidence/gel-text-vulkan-smoke-20261105/`.

1. **Topologia:** `%&@$# æøå gggQQQ 88888888 ||||||||`; contatori leggibili,
   nessun picco o normale invertito quando aste e occhielli diventano sottili.
2. **Kerning/fusione:** “calories or ingredients”, tracking da +50 a −30 px e
   ritorno in 61 frame deterministici; niente cuciture a lama o flicker. Le
   lettere si fondono solo quando la modalità di fusione è abilitata.
3. **Serif e subpixel:** font arrotondato e serif ad alto contrasto, zoom
   0,05×→10×→0,05× e rotazione Z fino a 15°; nessuno scintillio visibile o
   variazione temporale instabile.
4. **Luce radente:** sweep luce 360° a elevazione bassa e alta; la cresta scorre
   senza discontinuità e il terminatore non crea tagli neri.
5. **Trasmissione:** scacchiera ad alto contrasto dietro al testo; attenuazione
   visibile dipendente dallo spessore, compositing alpha corretto e nessun
   semplice adesivo opaco.
6. **4K/120:** solo dopo il pass visivo; GPU timestamp sull’intera pipeline
   testo/post, 4K UHD e 120 frame/s effettivamente presentati. Obiettivo ≤1,5 ms
   GPU per post del testo sulla macchina target documentata; misurare a parte
   conversione/readback e non usarli come prova di frame rate end-to-end.

In ogni gate registrare esito PASS/FAIL/NOT RUN, macchina, backend, comando,
hash raw e frame ispezionati. I test unitari dello shader non sostituiscono la
review del risultato ottico.

## Evidenza disponibile e limiti

La prima evidenza 720p e i limiti già osservati sono registrati in
[`docs/evidence/gel-text-vulkan-smoke-20261105/README.md`](../evidence/gel-text-vulkan-smoke-20261105/README.md):
highlight facettati, fusione kerning fallita e assenza di pass gel dedicati per
bloom/dispersione. Il probe corrente conferma inoltre che il contorno per-glifo
non basta a raggiungere la resa richiesta. Il risultato non è accettato come
match visivo.

## Dipendenze e confini

- Riutilizzare `TextRun`, atlas MTSDF, `EffectCatalog`,
  `RenderSurfaceRegistry` e render graph canonici.
- Coordinare l’authoring con ChrononMotion3D; implementazione GPU e gate visivi
  appartengono a Chronon3D.
- Mantenere separati i ticket shadow/spotlight: questa scheda non li chiude né
  sostituisce la loro certificazione.
- Valutare un generatore SDF o Jump Flood solo se il campo composto non si può
  ottenere correttamente dal percorso esistente; nessuna dipendenza esterna è
  presupposta da questa scheda.
