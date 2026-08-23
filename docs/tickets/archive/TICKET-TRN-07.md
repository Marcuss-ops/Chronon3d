# TICKET-TRN-07 — ClipTransitionNode Cut + Dissolve

## Stato
DONE (certified)

## Obiettivo
Implementare e certificare `ClipTransitionNode` per le transizioni tra clip intere, limitato in questa fase a:
- `Cut`
- `Dissolve`

Solo dopo che Cut e Dissolve sono corretti e certificati si aggiungeranno Push, Slide, Wipe, Iris, Zoom, Flash, ecc.

## Criteri di accettazione
- [x] `ClipTransitionNode` supporta `ClipTransitionKind::Cut` e `ClipTransitionKind::Dissolve`.
- [x] Cut restituisce A per `p < 1.0` e B per `p >= 1.0`.
- [x] Dissolve esegue `output = A*(1-p) + B*p` in spazio premoltiplicato.
- [x] I test coprono `p = 0`, `p = 0.5`, `p = 1`.
- [x] I test verificano il corretto comportamento con alpha premoltiplicato.
- [x] I test verificano il boundary esatto di Cut.
- [x] I test verificano il dimensionamento a fit e il rifiuto in caso di mismatch.
- [x] Build e test passano (`chronon3d_render_graph_tests -tc='*ClipTransition*'`).

## Note
Il file `tests/render_graph/features/test_clip_transition.cpp` è stato ridotto allo scope TRN-07: solo `Cut` e `Dissolve`. Le altre transizioni esistenti nel nodo (`Push`, `Slide`, `Wipe`, `Iris`, `Zoom`, `Flash`) NON fanno parte della certificazione TRN-07 e rimangono "use at your own risk" fino a un ticket dedicato.

## Evidenza
- Build target: `chronon3d_render_graph_tests`
- Test run: `chronon3d_render_graph_tests -tc='*ClipTransition*'`
- Risultato: 7/7 test cases passed, 54/54 assertions passed

## Forward points
- TICKET-TRN-07-EXT: certificare Push/Slide/Wipe/Iris/Zoom/Flash con matrice analoga a TRN-06.
