# TICKET-DOF-OCCLUSION-ACCEPTANCE — DoF, 2.5D occlusion e decisione AO

- **Stato**: chiuso (2026-11-04) — chiusura amministrativa: test software DoF/occlusione e decisione AO chiusi; griglia CoC multi-piano, HDR bokeh e parità strict-native integrata demandati a v2.1.

## Chiusura amministrativa 2026-11-04

- Verifica locale: suite C3D 4/4 (camera 93/93, core 1.479/1.479, render graph 448/448, renderer core 207/207); i test DoF/occlusione sono inclusi. Motion 10/10.
- Residui: fixture multi-profondità, foreground bleed/alpha, HDR bokeh e confronto strict-native integrato demandati a v2.1; non sono PASS.
- Evidenza: `docs/evidence/camera-closure-audit-20261104/README.md`.

- **Stato precedente (2026-11-04):** OPEN — decisione AO registrata; accettazione completa DoF non certificata.
- **Priorità:** P2, subordinata ai gate P1 di [AE camera functional parity](TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md).
- **Repository:** Chronon3d; Motion solo per generare i piani camera risolti di riferimento.
- **Obiettivo:** rendere ripetibili i test visuali/quantitativi di DoF e occlusione per il percorso 2.5D con fixture piccole basate su card/immagini. Non richiede asset mesh 3D.

## Stato verificato (2026-10-31)

- Aggiunte in `test_per_pixel_dof_node_rg_integration.cpp` tre regressioni software: la card vicina opaca occlude il piano lontano in entrambi gli ordini di authoring; alpha frazionario compone sul fondo in entrambi gli ordini; controllo senza DoF. Risultato: **3/3 casi, 21/21 asserzioni PASS** sul binario precompilato del checkout locale `919ed8f5`; non è una certificazione same-SHA del branch finale.
- Questo dimostra solo l'ordine di profondità 2.5D e il compositing locale della fixture. Non dimostra qualità generale del DoF, comportamento Vulkan strict-native o Ambient Occlusion.
- Nel renderer non è stata trovata una capability SSAO/HBAO/GTAO o occlusion culling. L'occlusione fra layer 2.5D non è Ambient Occlusion.

## Lavoro rimanente

**Regressioni software locali, 2026-11-02:** i binari precompilati dello
scratch build basato su `9fc4b70031edb2c844b05f6fd989339c4b2251c6` passano
`PR2-RG-DoF:*` (16/16 casi, 979/979 asserzioni) e `PerPixelDOF:*` (23/23,
65.599/65.599). Le fixture testate sono byte-identiche al working tree; lo
scratch contiene il native-mesh edit e overlay/vcpkg manifest ridotto, e la
differenza al sorgente DoF di produzione è solo commento. Nessuna compilazione
è stata fatta in questa verifica. I test confermano i casi software CoC/ROI,
near/far, foreground spill/halo, alpha e occlusione 2.5D; non confrontano la
stessa fixture integrata con Vulkan strict-native. Tracce e digest sono in
[`p2-gpu-vulkan-2026-11-02.md`](../evidence/p2-gpu-vulkan-2026-11-02.md).

**Allineamento producer/contract in corso:** la review ha trovato che Motion
`applyFocusSequence` rifiutava ogni `highlightGain` nonzero benché i kernel e il
RenderPlan C3D lo supportino. Motion ora lo ammette in [0, 8] e continua a
rifiutare le iris poligonali; C3D condivide lo stesso limite fra ammissione
RenderPlan e gate del render diretto. Test di lowering/decoder e fail-closed
sono stati aggiunti o aggiornati. Il build `-j14` ha compilato e linkato il
target render graph aggiornato; il relativo filtro passa 16/16 casi e
979/979 asserzioni. Motion ha compilato 37/37 step e i cinque CTest mirati
passano. Un include ChrononPlugin non usato bloccava il target C ABI nel clone
isolato; rimosso l'include obbligatorio, il build incrementale a `-j14` ha
linkato il target e il test decoder camera/DoF passa 17/17 asserzioni. Le prove
ChrononPlugin restano condizionate al relativo header. Non è stato rilanciato
l'intero target set e questi risultati non chiudono la parità software/Vulkan
integrata né i restanti gate di accettazione.

**Verifica hardware mirata (2026-11-02):** sulla RTX A4000 sono passati il test
Vulkan di occlusione mesh/piano 2.5D nei due ordini (75/75 asserzioni) e il
test Vulkan del disco bokeh contro l'oracolo CPU (1.169/1.169; delta massimo
`1.34576e-07`, soglia `1e-3`). L'evidenza usa il binario locale Vulkan-enabled
con runtime glibc 2.39 temporaneo sul server Ubuntu 22.04; deriva da un build
tree dirty e non è certificazione clean-SHA. Dettagli:
[`p2-gpu-vulkan-2026-11-02.md`](../evidence/p2-gpu-vulkan-2026-11-02.md).
Questi due probe non coprono i gate completi sottostanti.

- [ ] Fixture sintetica a più piani di profondità: ROI di nitidezza sul piano focale e crescita del CoC vicino/lontano, con soglie registrate prima dei fix.
- [ ] Foreground vicino sfocato sovrapposto al piano a fuoco: misurare bleeding/halo sui bordi e premoltiplicazione alpha.
- [ ] Punti luce HDR: verificare clipping e forma/continuità del bokeh circolare supportato con highlight weighting; le lamelle iris restano non supportate e devono fallire in modo esplicito.
- [ ] Eseguire le stesse fixture sul software oracle e Vulkan strict-native con depth attesa; fissare errore pixel/ROI e richiedere zero errori Vulkan.
- [ ] Integrare una fixture DoF + foreground sfocato + sfondo occluso per fissare l'ordine dei pass e scartare bleed da discontinuità di depth.
- [x] **Decisione AO:** Ambient Occlusion non è una capability del prodotto corrente ed è fuori perimetro. Chronon3D non implementa SSAO/HBAO/GTAO né occlusion culling; l'occlusione 2.5D non viene presentata come AO. Una richiesta futura richiede un ticket dedicato con algoritmo, input depth/normal, bias, stabilità temporale e soglie prima di introdurre un nuovo pass.

La forma geometrica dell'iris (lamelle) non è supportata e deve restare fail-closed. `highlight_gain` è implementato nei kernel software e Vulkan e ammesso dal contratto camera nell'intervallo [0, 8]; la sua lowering e la parità numerica CPU/Vulkan fanno parte della verifica del bokeh.

## Criterio di chiusura

Tutti i gate DoF sopra passano su fixture versionate, stessi parametri e soglie nei backend richiesti; risultati raw e manifest SHA vengono allegati. La voce AO si chiude con decisione esplicita: capability implementata e verificata in un ticket dedicato, oppure fuori perimetro documentato. Nessun PASS è inferito da un MP4 dimostrativo o dal solo cambiamento ON/OFF.
