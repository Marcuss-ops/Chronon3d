# TICKET-GOLDEN-E2E-REBASELINE — i golden e2e divergono, ma il render è deterministico

- **Stato:** CLOSED — 2026-11-16
- **Chiusura:** review visiva frame0 PNG (01 CHRONON 2.0 editorial, 02 PACKSHOT 2.5D, 03 MIXED 3D red cube) + `run_golden_e2e.py --update-goldens` su `30b4b721a`, backend software, host YOutube. `goldens.v1.json` ora dichiara `renderer_sha/backend/host/date/review`; rieseguito senza `--update-goldens`: 3/3 PASS, 36 frame, deterministic verde.
- **Ambito:** Chronon3d, golden e2e (`tests/e2e/golden`)
- **Origine:** suite completa 2026-11-16, `chronon3d_golden_e2e_tests`

## Sintomo

Tutti e tre i piani producono hash diversi dai golden registrati, mentre il
controllo di determinismo passa (lo stesso piano produce lo stesso hash a ogni
frame e a ogni run):

| Piano | Hash corrente | Golden |
|---|---|---|
| `01_editorial_text.plan.json` | `1f010e704e211067` | `bcfac79b4bd31d04` |
| `02_packshot_2p5d.plan.json` | `38d28c27625c0e4b` | `d51ada1a3bab8de7` |
| `03_mixed_mesh.plan.json` | `eb961e58b33b6167` | `3c8eada10f62ecc8` |

I piani sono statici (tutti i frame dello stesso piano hanno lo stesso hash),
quindi la divergenza non è rumore GPU: è un cambio di contenuto rispetto alla
baseline registrata.

## Perché non è stato re-pinnato

Aggiornare `tests/e2e/golden/goldens.v1.json` senza review significa congelare
un'eventuale regressione visiva come "nuovo atteso". I commit recenti che
toccano il testo (`6d939732b`, GEL text) e l'esecuzione (`1ba7acb5f`, rimozione
dell'executor Composition) possono spiegare parte del delta, ma non è stato
dimostrato frame per frame.

## Piano di chiusura

1. **Ricostruire la provenienza.** Leggere `tests/e2e/golden/README.md` e
   `goldens.v1.json`: SHA, backend, host e data con cui sono stati prodotti i
   golden correnti.
2. **Rigenerare con review.** Renderizzare i tre piani e salvare i frame PNG;
   ispezionare testo/GEL (piano 01), 2.5D e luce (02), mesh (03) contro
   l'intento del piano.
3. **Attribuire il delta.** Confrontare con i commit `6d939732b` e
   `1ba7acb5f`; per ogni piano decidere se la differenza è attesa
   (miglioramento/correzione) o sospetta.
4. **Aggiornare o correggere.**
   - Differenze attese: aggiornare `goldens.v1.json` con i nuovi hash, SHA e
     provenienza in un commit dedicato, allegando i PNG di review.
   - Differenze inattese: fermarsi sul piano sospetto e aprire un bug; i golden
     restano invariati.
5. **Rendere il gate autoesplicativo.** Il file golden deve dichiarare
   renderer SHA, backend e host, così una futura divergenza è classificabile
   senza ricostruire la storia.

## Criteri di accettazione

- [x] `goldens.v1.json` contiene SHA, backend, host e data di produzione.
- [x] Review visiva documentata per ciascuno dei tre piani.
- [x] I tre hash correnti coincidono con i golden e il check `deterministic`
      resta verde.
- [x] Nessun golden aggiornato senza review.

## Riferimenti

- `Chronon3d/tests/e2e/golden/goldens.v1.json`
- `Chronon3d/tests/e2e/golden/plans/`
- `Chronon3d/tests/e2e/golden/run_golden_e2e.py`
- `Chronon3d/.tmp/ctest-full-20261116b.log`
