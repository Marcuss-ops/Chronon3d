# TICKET-SHADOW-NATIVE-CATCHER — Ombre native, shadow catcher, shutter

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations: 9 video "advanced 3D" usano ground shadow/shadow catcher) e
dai difetti già noti in `docs/stato/chronon-template.md` §2. Stato
registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Pass ombre nativo in lane Vulkan strict-native: il nodo `Shadow` oggi fallisce fail-closed.
- [ ] Shadow catcher catch-only con semantica alpha corretta sui bordi.
- [ ] Accumulatore temporale weighted-add nativo (prerequisito dello shutter full-frame).
- [ ] Fix fail-open DOF: la rotta raw con `camera.dof.enabled` oggi esce 0 con frame RGBA a zero.
- [ ] A/B software vs Vulkan su produzione approvata + repeatability strict (serve host GPU NVIDIA).

## Goal

Chiudere la famiglia ombre/luci T05 (oggi PARTIAL: il nodo `Shadow` fallisce
fail-closed in strict Vulkan) e rendere usabile la pipeline cinematografica
che i tutorial AE replicano con "ground shadow + solid catcher".

## Scope (questa repo)

1. **Pass ombre nativo**: shadow map risolta interamente in lane Vulkan
   strict-native; il nodo `Shadow` non deve più fallire fail-closed.
2. **Shadow catcher**: materiale catch-only (il ground solid riceve e mostra
   solo le ombre), semantica alpha corretta sui bordi.
3. **Accumulatore temporale weighted-add nativo**: precondizione nota per lo
   shutter full-frame (oggi l'operatore Vulkan è solo source-over; la lane
   shutter si ferma chiedendo questo accumulatore — vedi
   `docs/stato/chronon-template.md` §2).
4. **Fix fail-open DOF**: la rotta raw con `camera.dof.enabled` oggi esce 0
   restituendo frame RGBA tutto zero; deve segnalare fail-closed l'operazione
   non supportata (o disegnare il frame).
5. **A/B software vs Vulkan** su produzione approvata + repeatability strict.

## Fuori scope

- Il light rig autorevole e i preset ombra editoriali (ChrononMotion3D,
  `TICKET-LIGHT-RIG-V1.md`).
- Risoluzione del crash TBB già contenuto (guardia host): resta contenuto.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
le certificazioni strict-native richiedono l'host GPU NVIDIA.

1. **Riprodurre i due difetti come test di regressione oggi rossi** — nodo
   `Shadow` che fallisce fail-closed in strict-native e rotta raw con
   `camera.dof.enabled` che esce 0 con frame RGBA a zero. Senza questa base la
   chiusura non è dimostrabile.
   Verifica: i test falliscono per il motivo giusto (fallimento dichiarato,
   non crash).
2. **Accumulatore temporale weighted-add nativo** — operatore di accumulo
   accanto al solo source-over nella lane Vulkan; è la precondizione nota dello
   shutter (oggi la lane shutter si ferma qui,
   `docs/stato/chronon-template.md` §2).
   Verifica: accumulo deterministico su frame di prova + test unitari.
3. **Pass ombre nativo in lane Vulkan strict-native** — shadow map risolta
   interamente in lane nativa; il nodo `Shadow` decodifica e renderizza senza
   fallback.
   Verifica: i test del passo 1 diventano verdi; parità A/B software con le
   metriche RGB-L1 delle run T05 esistenti.
4. **Shadow catcher catch-only** — il ground solid riceve e mostra solo le
   ombre, alpha corretto sui bordi, nessun residuo di staging.
   Verifica: frame di certificazione 1920×1080.
5. **Shutter 90°/8 campioni** in strict-native sull'accumulatore del passo 2.
   Verifica: NVENC accetta i frame (oggi zero).
6. **Fix fail-open DOF** — scegliere e dichiarare una delle due semantiche
   (errore esplicito fail-closed oppure frame disegnato) e testarla.
   Verifica: DoF-on differisce da DoF-off oppure errore esplicito; mai
   successo con frame trasparente.
7. **Certificazione A/B software vs Vulkan su produzione approvata** +
   repeatability strict su due run (serve l'host GPU NVIDIA).
8. **Chiusura formale** — CTest verde e golden registrata; spuntare tutte le
   voci di `## Accettazione`; portare `## Stato` a `COMPLETE` (convenzione di
   `docs/tickets/archive/TICKET-PER-KEYFRAME-EASING.md`); spostare il file in
   `docs/tickets/archive/`; registrare lo stato risolto in
   `docs/stato/chronon3d.md` §13 e aggiornare T05.

## Accettazione

- [ ] Nodo `Shadow` verde in strict-native con parità A/B software (metriche
      RGB-L1 come da run T05 esistenti, oltre soglia approvata).
- [ ] Shadow catcher 1920×1080: ombre visibili, nessun residuo di staging,
      alpha corretto.
- [ ] Shutter 90°/8 campioni completa in strict-native con accumulatore
      weighted-add; NVENC accetta i frame (oggi zero).
- [ ] DoF-on differisce da DoF-off oppure errore esplicito; mai successo con
      frame trasparente.
- [ ] CTest verde; golden registrata.

## Dipendenze

- Host GPU NVIDIA dedicato per la certificazione (blocco trasversale noto:
  `docs/stato/README.md`).
- Nessuna dipendenza da altre repo.

## Ordine

**Primo di tutto** il P1: è prerequisito di spotlight, IBL e del rendering
dei soggetti animati.
