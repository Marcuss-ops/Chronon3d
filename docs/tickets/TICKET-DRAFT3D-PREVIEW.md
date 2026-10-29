# TICKET-DRAFT3D-PREVIEW — Modalità preview a bassa risoluzione (Draft 3D)

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations #27 usa "draft 3D" per lavorare fluido sulle scene pesanti).
Stato registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

> Logica e punti di intervento completi nel «Piano di chiusura passo-passo» qui sotto; sintesi con dove mettere le mani:

- [ ] **Flag `preview_scale` (1/2, 1/4)** in `RenderSettings`/CLI/daemon applicato al solo output display. Dove: `include/chronon3d/runtime/render_settings.hpp` (o la casa tipi equivalente), parsing CLI (`apps/chronon3d_cli/`), IPC daemon. Verifica: parsing e pass-through al runtime, nessun effetto sui golden.
- [ ] **Portare il downscale display-bound di RenderingGen P2 nel runtime senza duplicazioni** (3 casi / 23 asserzioni, `docs/stato/renderinggen.md` §2). Verifica: test condivisi o ricollocati, mai copiati.
- [ ] **Interazione col dirty caching** nel passaggio preview → full (nessun ricalcolo superfluo). Dove: dirty history/refresh lanes del render graph. Verifica: test di transizione senza artefatti di cache.
- [ ] **Misura ≥2× di throughput vs full** su scena 3D pesante (riferimento: torture render 601 frame, `docs/stato/chronon-template.md` §1.3), metodo dichiarato; pixel invariati con `preview_scale` disabilitato.

## Goal

Modalità di render preview con downscale display-bound per editing fluido su
scene 3D pesanti, equivalente al "Draft 3D" di AE 2026: anteprima veloce a
bassa risoluzione senza invalidare lo stato della scena, con render completo
solo all'export.

## Scope (questa repo)

1. Flag di render setting (`RenderSettings`/CLI/daemon): `preview_scale`
   (es. 1/2, 1/4) applicato al solo output display.
2. Riuso del downscale display-bound già modellato per RenderingGen P2
   (test: 3 casi / 23 asserzioni) — portarlo nel runtime senza duplicazioni.
3. Interazione col dirty caching esistente: il passaggio preview → full non
   deve ricalcolare ciò che non è cambiato oltre al necessario.
4. Nessun cambio di semantica dei golden: la verità pixel resta la lane
   completa.

## Fuori scope

- UI: resta un comportamento di CLI/daemon/SDK.
- Downscale come effetto di output in produzione.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
la misura di throughput su scena 3D richiede l'host GPU NVIDIA.

1. **Flag `preview_scale`** (1/2, 1/4) in `RenderSettings` + CLI/daemon,
   applicato al solo output display.
   Verifica: parsing CLI e pass-through al runtime (nessun effetto sui golden).
2. **Riuso del downscale display-bound** di RenderingGen P2 (3 casi / 23
   asserzioni, `docs/stato/renderinggen.md` §2) portato nel runtime senza
   duplicazioni.
   Verifica: test condivisi o ricollocati, non copiati.
3. **Interazione col dirty caching** — il passaggio preview → full non deve
   ricalcolare ciò che non è cambiato oltre il necessario.
   Verifica: test di transizione preview → full senza artefatti di cache.
4. **Misura di throughput** — preview 1/4 su scena 3D pesante (riferimento:
   torture render 601 frame, `docs/stato/chronon-template.md` §1.3): ≥2× vs
   full, con metodo di misura dichiarato e registrato.
5. **Invarianza pixel** — con `preview_scale` disabilitato gli output sono
   identici alla lane completa (golden immutate).
6. **Chiusura formale** — CTest verde; spuntare `## Accettazione`; `## Stato` →
   `COMPLETE`; spostare il file in `docs/tickets/archive/`; aggiornare
   `docs/stato/chronon3d.md` §13.

## Accettazione

- [ ] Preview 1/4 su scena 3D pesante: ≥2× di throughput vs full (misura
      registrata, metodo dichiarato).
- [ ] Stessi pixel della lane completa quando `preview_scale` è disabilitato.
- [ ] Transizione preview → full senza artefatti di cache.
- [ ] CTest verde.

## Dipendenze

Nessuna. Indipendente dal P1.

## Riferimenti

- `docs/stato/renderinggen.md` §2 (downscale display-bound).
- `docs/stato/chronon-template.md` §1.3 (torture render 601 frame: la lane
  multi-layer performante è la motivazione del ticket).
