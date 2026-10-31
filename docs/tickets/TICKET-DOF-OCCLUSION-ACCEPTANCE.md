# TICKET-DOF-OCCLUSION-ACCEPTANCE — DoF, 2.5D occlusion e decisione AO

- **Stato:** OPEN — copertura mirata iniziale presente; accettazione completa DoF non certificata.
- **Priorità:** P2, subordinata ai gate P1 di [AE camera functional parity](TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md).
- **Repository:** Chronon3d; Motion solo per generare i piani camera risolti di riferimento.
- **Obiettivo:** rendere ripetibili i test visuali/quantitativi di DoF e occlusione per il percorso 2.5D con fixture piccole basate su card/immagini. Non richiede asset mesh 3D.

## Stato verificato (2026-10-31)

- Aggiunte in `test_per_pixel_dof_node_rg_integration.cpp` tre regressioni software: la card vicina opaca occlude il piano lontano in entrambi gli ordini di authoring; alpha frazionario compone sul fondo in entrambi gli ordini; controllo senza DoF. Risultato: **3/3 casi, 21/21 asserzioni PASS** sul binario precompilato del checkout locale `919ed8f5`; non è una certificazione same-SHA del branch finale.
- Questo dimostra solo l'ordine di profondità 2.5D e il compositing locale della fixture. Non dimostra qualità generale del DoF, comportamento Vulkan strict-native o Ambient Occlusion.
- Nel renderer non è stata trovata una capability SSAO/HBAO/GTAO o occlusion culling. L'occlusione fra layer 2.5D non è Ambient Occlusion.

## Lavoro rimanente

- [ ] Fixture sintetica a più piani di profondità: ROI di nitidezza sul piano focale e crescita del CoC vicino/lontano, con soglie registrate prima dei fix.
- [ ] Foreground vicino sfocato sovrapposto al piano a fuoco: misurare bleeding/halo sui bordi e premoltiplicazione alpha.
- [ ] Punti luce HDR: verificare clipping e forma/continuità del bokeh circolare supportato. Le lamelle e l'highlight gain non supportati devono continuare a fallire in modo esplicito.
- [ ] Eseguire le stesse fixture sul software oracle e Vulkan strict-native con depth attesa; fissare errore pixel/ROI e richiedere zero errori Vulkan.
- [ ] Integrare una fixture DoF + foreground sfocato + sfondo occluso per fissare l'ordine dei pass e scartare bleed da discontinuità di depth.
- [ ] Separatamente, decidere se AO è una capability di prodotto. Se approvata: specificare algoritmo, input depth/normal, bias, stabilità/temporalità e soglie su piani/sfere sintetici prima di implementare. Fino ad allora non dichiarare supporto o PASS AO.

## Criterio di chiusura

Tutti i gate DoF sopra passano su fixture versionate, stessi parametri e soglie nei backend richiesti; risultati raw e manifest SHA vengono allegati. La voce AO si chiude con decisione esplicita: capability implementata e verificata in un ticket dedicato, oppure fuori perimetro documentato. Nessun PASS è inferito da un MP4 dimostrativo o dal solo cambiamento ON/OFF.
