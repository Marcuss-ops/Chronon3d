# ADR-DRAFT — decide before evaluate

**Stato:** DRAFT, non ratificato (2026-11-01).
**Area:** render graph, confine video, handoff final job.
**Origine:** misure r48/r49 del job Garlasco (overlay batch + final job) e del
ticket [`TICKET-PERF-FRAME-PROOF-CHEAP`](TICKET-PERF-FRAME-PROOF-CHEAP.md).

## Contesto

Tre confini del percorso produttivo eseguono il lavoro e *poi* chiedono se il
lavoro era necessario:

| Confine | Prova cercata | Costo della prova |
|---|---|---|
| frame → frame (Chronon) | «questo frame è identico al precedente?» | `evaluate_scene()` completa: **42,6 ms** su 46,3 (**92%**) |
| item → artefatto (overlay) | «l'artefatto è assemblable?» | nessuno: non lo è, quindi ogni consumatore ri-encoda |
| artefatto → assemble (final job) | «il frammento è conforme?» | encode **1,48 s** + ffprobe **1,52 s** per frammento, r49 |

Il batch overlay di r49 lo dice senza ambiguità: **844 frame su 1607** prendono la
fast-path di duplicazione dei pacchetti (`fast_path_reused_frames=844`) e ognuno
costa **43,5 ms con 0 ms di render**, cioè **36,7 s su un render loop di 74,3 s**.
La decisione è corretta; il prezzo per prenderla è l'intera valutazione del
timeline. L'ordine è invertito rispetto all'informazione: l'identità che serve a
decidere è calcolabile *prima*, ma viene calcolata *dopo*.

## Decisione

**Nessuna fase può ricostruire ciò che una identità deterministica calcolabile a
monte già risponde.** In concreto:

1. L'unità di lavoro di un export non è il frame, è **il piano compilato per
   item** più la sua identità. Il frame porta solo `(t, parametri)`.
2. Il consumatore riceve **l'identità** (digest del piano, digest del programma,
   certificato di stream) e non la deduce dal risultato: copia, salta o rifiuta
   *prima* di produrre pixel.
3. «Verificare» è ammesso solo per ciò che non ha identità a monte. Un ffprobe
   per frammento su byte che il produttore ha già certificato è debito, non
   sicurezza.
4. L'identità deve essere **fail-closed**: se l'identità manca o non combacia, il
   percorso costoso resta disponibile e il fallback è contato, mai silenzioso.

## Conseguenze

**Positive (misurate o proiettate dai dati r49):**

- duplicati a costo ~0: **−36,7 s** su un batch da 74,3 s (49%);
- `timeline_eval` per item invece che per frame: 1607 × 42,6 ms = **68,3 s** →
  un solo passaggio per item;
- final job: 264 s di preparazione frammenti → tagli `-c copy` su confini GOP,
  **0 encode, 0 ffprobe per frammento**;
- il gate di throughput smette di dipendere dalla CPU: a ≤8 ms/frame servono
  5 lane per 500 overlay/min invece di 28.

**Negative / costi:**

- serve un **contratto di identità** scritto in un posto solo (piano compilato,
  certificato di stream, digest del frammento) e va versionato;
- i produttori cambiano: l'encoder dell'overlay deve emettere stream già conforme
  (`bf=0`, GOP 48, `1/90000`, chiuso) — non è un'ottimizzazione, è un requisito
  del confine;
- un'identità sbagliata è un errore silenzioso: ogni percorso identity-first
  richiede un test di **parità byte** contro il percorso che ricostruisce.

## Debito di demolizione

Quando i ticket collegati atterrano, questi percorsi diventano morti e vanno
rimossi, non lasciati come fallback permanente:

- `CompositionFrameState::render_single_frame()` valuta la scena per frame anche
  quando la policy deciderà `DuplicateFrame`;
- `finalJobSourceIsExactPreparedFragment()` accetta il riuso **solo** per
  `source_in_us == 0` (sorgente intera, già preparata da noi): ogni finestra è
  obbligata a ri-encodare;
- `verifyFinalJobFragment()` su frammenti di nostra produzione quando il taglio è
  una copia di pacchetti certificati.

## Non obiettivi

- Non è un permesso a saltare il gate di conformità: `VELOX_ASSEMBLY_READY_V1/V2`
  non cambia e resta l'unica authority di stream.
- Non autorizza output «più o meno uguali»: ogni fase identity-first ha un gate
  di parità byte o di PSNR esplicito.
- Non è un piano di caching: la cache è un effetto dell'identità, non la
  decisione.
