# ADR DRAFT — HUD structured buffer: compiled plan data instead of per-frame interpretation

Status: **DRAFT** (documentation only; no code change in this pass).
Related: `refactored/docs/operations/TICKET-PARADIGM-ALPHA-LAYER-HUD.md` §4 (cammino B),
`TICKET-PERF-REVOLUTIONARY-IDEAS` C-10 (espressioni riparse a ogni frame),
P1 (`loop_other` 1,3–2,3 s non attribuiti), `TICKET-100X-PERF-WAVES` B1/B2.

## Context

Gli overlay editoriali sono funzionalmente HUD: testo (MTSDF) su forme con
easing e fade. Il path attuale li rende come motion graphics completi: a ogni
frame il grafo viene valutato (`composition_->evaluate`), le proprietà animate
vengono ripesate/riparse (C-10: espressioni riparse da stringa a ogni frame per
ogni proprietà), i keyframe cercati (C-11, mitigato), i quad ricostruiti. Il
residuo misurato `loop_other` è 1,3–2,3 s su wall 2,4–3,6 s per job 1-frame e
il breakdown per-frame (`loop_prepare_frame`/`loop_graph_eval`/
`loop_encode_handoff`/`loop_other`, telemetry P1) non è ancora stato letto sul
corpus golden (gate B0).

Un gioco rende lo stesso tipo di lavoro (glifi + quad + trasformazioni
eased) in <0,5 ms/frame con un modello diverso: **i dati del piano vivono in
un buffer GPU compilato una volta per job**, il frame non interpreta nulla.

## Decision (proposed)

Introdurre una lane **HUD** accanto al path di interpretazione, per la classe
`text_phrase` con moto rigido/fade (perimetro iniziale: la maggioranza del mix
golden):

- **Compilazione del piano a job time**: per item, un record in uno structured
  buffer GPU: quad dei glifi (indici atlas), costanti keyframe
  (transform/opacity/colore per fase, easing risolto a compile time in
  costanti — nessuna espressione a runtime), riferimenti texture per forme.
- **Una pipeline persistente per job** (stesso protocollo di vita del glyph
  atlas e di `PersistentOverlayNode`, `LifetimeClass::JobPersistent`), draw
  instanziati per item; il frame loop fa solo dispatch + encode handoff.
- **Glyph atlas persistente** condiviso tra job (cache: vedi conseguenze),
  chiave di invalidazione = hash(font, size, glyph set) come per le pagine
  atlas esistenti.
- **Selezione della lane**: compilazione a lane HUD solo per la classe
  coperta; ogni item fuori classe (animatori per-cluster, word-reveal, effetti
  non coperti) **fallisce esplicitamente sul path attuale** — mai rendering
  silenzioso con semantica diversa.

## Consequences

- `loop_graph_eval` e il re-parse espressioni (C-10) spariscono per
  costruzione nella classe coperta; il costo per-frame CPU della classe deve
  scendere a decine di µs (target di progetto, gate misurato in B2).
- **Nuove entità con vita propria**: pipeline persistente + atlas condiviso +
  cache dei record compilati. Per `AGENTS.md` ("No nuovi
  singleton/registry/resolver/cache senza ADR") questo ADR è il prerequisito;
  l'implementazione dovrà dichiarare per ogni cache: chiave, invalidazione,
  ownership e kill switch (protocollo già usato da `ManifestOwnedCache`).
- Parità: la lane deve essere **bit-identica** per la classe coperta (stesso
  raster MTSDF, stesso ordine di blend); altrimenti la classe resta sul path
  attuale. Nessun golden "visivamente simile" accettato come parità.
- Interazione con B1/B2 di `TICKET-100X-PERF-WAVES`: la lane HUD subsume
  `PersistentOverlayNode` per la classe coperta; la coesistenza è temporanea
  (Demolition Debt sotto).

## Demolition Debt (obbligo `AGENTS.md`)

- **Ingresso:** lane HUD + ADR (questo documento).
- **Uscita:** alla certificazione B2, il path di interpretazione per la classe
  coperta viene rimosso (incluso il re-parse espressioni raggiungibile solo da
  quella classe), non affiancato. Le classi non coperte restano sul path
  attuale finché la loro lane non passa gli stessi gate.
- **Divieto:** nessun doppio path permanente per la stessa classe.

## Exit conditions before implementation

1. Gate B0 eseguito: breakdown `loop_other` per componente su fixture
   `chronon_text_full_static` + corpus golden; se `loop_graph_eval +
   loop_other` < 30% del wall item, questo ADR si chiude come misura negativa.
2. Definizione del golden di parità bit-identica per la classe coperta
   (fixture + protocollo di confronto, riuso del protocollo segmentato P4).
3. Dichiarazione completa delle cache (chiave, invalidazione, ownership, kill
   switch) in questo file, prima del codice.
4. Approvazione esplicita dell'utente al core (`AGENTS.md`) prima del merge.
