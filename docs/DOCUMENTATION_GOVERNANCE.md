# Chronon3D — Documentation Governance

## Scopo

Questo documento definisce la struttura, le responsabilità e il ciclo di aggiornamento della documentazione di Chronon3D.

L'obiettivo è impedire:

* duplicazione dello stesso stato in più file;
* snapshot incompatibili tra documenti;
* riferimenti a file archiviati;
* dichiarazioni di successo non accompagnate da prove;
* ticket dettagliati inseriti dentro tabelle non leggibili;
* trasformazione di roadmap e status in cronologie dei commit.

La documentazione deve descrivere il repository in modo verificabile, sintetico e coerente.

---

## Principio fondamentale

Ogni informazione deve avere una sola fonte canonica.

Gli altri documenti possono collegare la fonte canonica, ma non devono copiarne integralmente il contenuto.

Esempi:

* lo stato corrente vive in `CURRENT_STATUS.md`;
* i criteri di release vivono in `RELEASE_GATE.md`;
* il futuro vive in `ROADMAP.md`;
* la prova di un'esecuzione vive in `docs/baselines/`;
* il dettaglio di un problema vive nella relativa scheda ticket;
* la motivazione di una decisione architetturale vive in un ADR.

---

## Mappa delle fonti canoniche

### `README.md`

Responsabilità:

* descrizione sintetica del prodotto;
* quick start;
* architettura pubblica ad alto livello;
* collegamenti ai documenti canonici.

Non deve contenere:

* snapshot di commit;
* numero corrente di gate passati;
* lista dei blocker;
* cronologia dei ticket;
* dichiarazioni di release readiness.

Il README deve rimanere valido anche quando `main` avanza.

---

### `AGENTS.md`

Responsabilità:

* workflow Git;
* regole di lavoro;
* feature freeze;
* vincoli architetturali;
* documenti obbligatori;
* azioni vietate;
* definizione generale della baseline verde.

Non deve contenere:

* snapshot degli agenti;
* lista corrente dei ticket;
* stato dettagliato delle feature;
* ricostruzioni delle sessioni precedenti;
* informazioni che cambiano dopo ogni commit.

Gli stati operativi devono essere collegati da `CURRENT_STATUS.md`.

---

### `docs/CURRENT_STATUS.md`

È l'unica fonte canonica dello stato presente.

Deve contenere:

1. SHA osservato;
2. data della verifica;
3. piattaforma verificata;
4. ultima baseline eseguita;
5. risultato sintetico dei gate;
6. blocker correnti;
7. stato sintetico dei sottosistemi;
8. limitazioni conosciute;
9. prossimo passo operativo.

Non deve contenere:

* storia completa dei ticket chiusi;
* descrizioni dettagliate dell'implementazione;
* elenchi di file modificati;
* cronologia dei commit;
* piani futuri completi;
* criteri generali di release.

Limite raccomandato: 150 righe.

Ogni stato deve usare esclusivamente:

* `PASS`;
* `FAIL`;
* `PARTIAL`;
* `NOT RUN`;
* `BLOCKED`;
* `PLANNED`.

Un valore `PASS` deve indicare lo SHA e la baseline che lo dimostrano.

---

### `docs/ROADMAP.md`

È la fonte canonica della direzione futura.

Deve contenere:

* milestone ordinate;
* obiettivo di ogni milestone;
* prerequisiti;
* risultati richiesti;
* gate di uscita;
* non-goal.

Non deve contenere:

* snapshot di `main`;
* risultati della baseline corrente;
* ticket già chiusi;
* ricostruzioni dei commit;
* liste di file modificati;
* istruzioni operative giornaliere.

La roadmap deve rimanere stabile mentre cambia lo stato del repository.

Lo stato di una milestone viene letto da `CURRENT_STATUS.md`, non duplicato nella roadmap.

---

### `docs/RELEASE_GATE.md`

È la fonte canonica dei requisiti permanenti di release.

Deve contenere esclusivamente:

* controlli richiesti;
* definizione di successo;
* output atteso;
* ambiente supportato;
* artifact richiesti;
* criteri per Text V1, Camera V1 e SDK V1;
* regole per certificare o revocare una release.

Non deve contenere:

* certificazione corrente;
* SHA corrente;
* baseline storiche;
* progressi di singoli ticket;
* spiegazioni dei fallimenti correnti.

La certificazione effettiva vive in una baseline immutabile e viene riepilogata in `CURRENT_STATUS.md`.

---

### `docs/FOLLOWUP_TICKETS.md`

È un indice, non una raccolta di specifiche.

Deve contenere al massimo i blocker attivi più importanti.

Ogni ticket occupa una sola riga:

| ID | Priorità | Area | Stato | Blocca | Scheda |
| -- | -------- | ---- | ----- | ------ | ------ |

Le celle non devono contenere paragrafi.

Il dettaglio completo vive in:

```text
docs/tickets/TICKET-NNN.md  <!-- drift-allow: stale-ref -->
```

Quando un ticket viene chiuso:

1. viene rimosso dall'indice degli aperti;
2. la scheda viene marcata `DONE`;
3. viene aggiunta una breve voce al changelog;
4. la prova viene collegata alla baseline o ai test pertinenti.

---

### `docs/tickets/TICKET-NNN.md`  <!-- drift-allow: stale-ref -->

Ogni scheda deve utilizzare questa struttura:

```markdown
# TICKET-NNN — Titolo

## Stato

OPEN | IN PROGRESS | BLOCKED | DONE

## Priorità

P0 | P1 | P2 | P3

## Problema

Descrizione breve del comportamento osservato.

## Evidenza

File, comando, test, output o baseline che dimostrano il problema.

## Impatto

Effetto su stabilità, manutenibilità, prestazioni o release.

## Confine

Cosa appartiene al ticket e cosa non vi appartiene.

## Soluzione accettabile

Descrizione del risultato richiesto, senza imporre dettagli implementativi non necessari.

## Criteri di accettazione

- criterio osservabile;
- test o gate richiesto;
- nessuna regressione dei confini correlati;
- documentazione sincronizzata.

## Collegamenti

- ADR:
- baseline:
- milestone:
- ticket correlati:
```

Una scheda ticket non deve trasformarsi in un diario di sviluppo.

Le analisi superate devono essere rimosse oppure spostate in una sezione finale chiamata `Historical notes`.

---

### `docs/baselines/`

Le baseline sono prove immutabili.

Ogni file deve essere associato a un solo SHA e non deve essere aggiornato dopo che `main` è avanzato.

Formato del nome:

```text
main-<short-sha>-baseline.md
```

Ogni baseline deve contenere:

* SHA completo;
* data;
* ambiente;
* versione degli strumenti rilevanti;
* comandi eseguiti;
* exit code;
* risultato di ciascun gate;
* artifact o log prodotti;
* verdetto finale.

Una baseline non descrive il futuro e non propone refactoring.

---

### `docs/CHANGELOG.md`

Il changelog contiene soltanto lavoro completato.

Ogni voce deve essere breve:

```markdown
## YYYY-MM-DD

### TICKET-NNN — Titolo

- risultato ottenuto;
- comportamento corretto;
- test o gate aggiunto;
- eventuale compatibilità rilevante.
```

Non deve contenere:

* specifiche ancora aperte;
* lunghi elenchi di file;
* dettagli riga per riga;
* ipotesi future;
* ticket pianificati.

Limite raccomandato: da tre a sei punti per ticket.

---

### `docs/adr/`

Un ADR documenta una decisione architetturale, non lo stato di implementazione.

Ogni ADR deve indicare:

* contesto;
* decisione;
* alternative considerate;
* conseguenze;
* stato: Proposed, Accepted, Superseded o Rejected.

Una decisione accettata non deve essere riscritta dentro `CURRENT_STATUS.md` o `ROADMAP.md`.

---

### `docs/ARCHIVE/`

Tutto ciò che si trova in `docs/ARCHIVE/` è storico e non operativo.

I documenti attivi:

* non devono richiedere l'aggiornamento di file archiviati;
* non devono usare file archiviati come fonte canonica;
* possono collegarli soltanto per ricostruzioni storiche esplicite.

---

## Politica degli snapshot

Soltanto questi documenti possono contenere uno SHA corrente:

* `CURRENT_STATUS.md`;
* file in `docs/baselines/`;
* eventuali report di audit datati.

Non devono contenere snapshot correnti:

* `README.md`;
* `ROADMAP.md`;
* `RELEASE_GATE.md`;
* `AGENTS.md`;
* ADR accettati;
* guide operative stabili.

Quando `main` avanza senza una nuova baseline:

```text
HEAD corrente: <sha>
Ultima baseline verificata: <sha precedente>
Stato su HEAD corrente: NOT RUN
```

Non si deve trasferire automaticamente il risultato della baseline precedente al nuovo HEAD.

---

## Politica dei collegamenti

I collegamenti devono seguire questa direzione:

```text
README
  → CURRENT_STATUS
  → ROADMAP
  → RELEASE_GATE

CURRENT_STATUS
  → ultima baseline
  → ticket aperti

FOLLOWUP_TICKETS
  → singole schede ticket

Ticket
  → ADR
  → baseline
  → milestone

CHANGELOG
  → ticket completati
```

Sono vietati cicli in cui più documenti si dichiarano reciprocamente fonti dello stesso stato.

---

## Matrice di aggiornamento

| Evento                                                    | Documenti da aggiornare                                 |
| --------------------------------------------------------- | ------------------------------------------------------- |
| Modifica interna senza variazione osservabile dello stato | Nessun documento di stato obbligatorio                  |
| Nuovo blocker verificato                                  | Ticket + indice ticket + Current Status                 |
| Ticket completato                                         | Ticket + indice ticket + Changelog                      |
| Risultato di gate cambiato                                | Nuova baseline + Current Status                         |
| Nuova decisione architetturale                            | ADR                                                     |
| Cambiamento delle milestone                               | Roadmap                                                 |
| Cambiamento dei requisiti di release                      | Release Gate + eventuale ADR                            |
| Nuova release certificata                                 | Baseline + Current Status + Changelog                   |
| Documento sostituito                                      | Spostamento in Archive + aggiornamento dei collegamenti |

---

## Definition of Done documentale

Un cambiamento è documentato correttamente quando:

1. la fonte canonica appropriata è stata aggiornata;
2. nessuna informazione è stata duplicata per esteso;
3. gli stati riportano evidenza osservabile;
4. lo SHA della prova è esplicito;
5. i collegamenti non puntano a percorsi archiviati o mancanti;
6. il ticket aperto non è presentato come completato;
7. il changelog non contiene lavoro futuro;
8. la roadmap non contiene stato operativo;
9. il release gate non contiene certificazioni temporanee;
10. la baseline rimane immutabile.

---

## Pattern vietati

Sono vietati:

* percentuali manuali di completamento;
* `PASS` ereditati da commit differenti;
* più file dichiarati come fonte canonica dello stesso fatto;
* paragrafi completi dentro celle di tabella;
* ticket aperti descritti nel changelog;
* cronologie dei commit dentro `CURRENT_STATUS.md`;
* snapshot correnti dentro documenti permanenti;
* riferimenti operativi a `docs/ARCHIVE/`;
* creazione di nuovi file status con nomi simili;
* copia integrale dello stesso blocker in status, roadmap, gate e ticket.

---

## Regola finale

Quando non è chiaro dove scrivere un'informazione, usare questa domanda:

> Questa informazione descrive il presente, il futuro, un requisito, un problema, una decisione o una prova?

* presente → `CURRENT_STATUS.md`;
* futuro → `ROADMAP.md`;
* requisito → `RELEASE_GATE.md`;
* problema → ticket;
* decisione → ADR;
* prova → baseline;
* risultato completato → changelog.
