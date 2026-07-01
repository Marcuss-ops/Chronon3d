# Chronon3D — Architecture Health Audit

## Scopo

Questo documento organizza gli audit periodici della salute architetturale di Chronon3D.

Non sostituisce:

* `CURRENT_STATUS.md`;
* `FOLLOWUP_TICKETS.md`;
* gli ADR;
* le baseline;
* i test automatici.

Un finding diventa operativo soltanto quando viene collegato a una scheda ticket.

---

## Stati ammessi

| Stato       | Significato                                       |
| ----------- | ------------------------------------------------- |
| NOT AUDITED | Area non ancora analizzata                        |
| OBSERVED    | Evidenza trovata, impatto non ancora classificato |
| CONFIRMED   | Problema riproducibile e documentato              |
| TRACKED     | Ticket creato                                     |
| MITIGATED   | Rischio ridotto ma non eliminato                  |
| RESOLVED    | Criteri di accettazione soddisfatti               |
| ACCEPTED    | Rischio intenzionalmente accettato tramite ADR    |

---

## Priorità

La priorità deriva dall'incrocio tra frequenza di modifica e fragilità.

| Fragilità | Frequenza bassa                              | Frequenza alta                         |
| --------- | -------------------------------------------- | -------------------------------------- |
| Alta      | P1 — intervenire quando l'area viene toccata | P0 — hotspot immediato                 |
| Media     | P2 — pianificare                             | P1 — inserire nella milestone corrente |
| Bassa     | P3 — osservare                               | P2 — manutenzione ordinaria            |

La frequenza deve essere determinata dalla cronologia reale dei commit, non da impressioni manuali.

---

## Categorie di audit

### A. Confini architetturali

Verificare:

* dipendenze cicliche;
* accessi del core direttamente a filesystem, rete o driver;
* API pubbliche che espongono tipi interni;
* moduli che dipendono da implementazioni concrete anziché da contratti;
* percorsi paralleli per la stessa responsabilità;
* registry, resolver, sampler o cache duplicati.

---

### B. Responsabilità dei moduli

Verificare:

* file con troppe responsabilità;
* feature envy;
* classi che coordinano, trasformano, validano e persistono contemporaneamente;
* helper inseriti nel package sbagliato;
* package che diventano contenitori generici;
* dipendenze dormienti mantenute soltanto per compatibilità non dimostrata.

---

### C. Complessità

Verificare:

* funzioni con molti branch;
* annidamento eccessivo;
* switch con responsabilità multiple;
* error handling duplicato;
* retry distribuiti tra più livelli;
* cleanup manuale ripetuto;
* funzioni che restituiscono successo prima del completamento effettivo.

---

### D. Stato e concorrenza

Verificare:

* stato globale mutabile;
* singleton modificabili;
* cache process-wide non isolate;
* inizializzazione dipendente dall'ordine;
* ownership non esplicita;
* accessi concorrenti senza contratto;
* oggetti per-frame conservati oltre la loro durata prevista.

---

### E. Modellazione del dominio

Verificare:

* stringhe usate per rappresentare identificativi strutturati;
* interi senza unità;
* flag booleani con significati multipli;
* JSON non validato passato tra moduli;
* enum duplicati per lo stesso concetto;
* conversioni ripetute tra rappresentazioni equivalenti;
* validazioni replicate in più punti.

---

### F. Prestazioni e risorse

Verificare:

* allocazioni dentro loop per-frame o per-glyph;
* copie di buffer evitabili;
* lookup lineari ripetuti;
* trasformazioni ricostruite per ogni frame;
* I/O sincrono nel percorso di rendering;
* risorse non restituite ai pool;
* cache senza limiti o politiche di espulsione;
* compilazioni o risoluzioni ripetute a runtime.

---

### G. Affidabilità del flusso

Verificare:

* retry senza limite;
* retry senza backoff;
* errori convertiti in warning;
* eccezioni o errori ignorati;
* `continue-on-error` sul percorso candidato;
* funzioni che dichiarano successo dopo un risultato parziale;
* output creato ma non validato;
* artifact incompleti interpretati come completati.

---

### H. Duplicazione

Verificare:

* implementazioni quasi identiche;
* tabelle di registry replicate;
* liste di file mantenute in più punti;
* validazioni duplicate;
* helper locali equivalenti;
* più pipeline per lo stesso input;
* documentazione che replica lo stesso stato in più file.

---

## Registro dei finding

| ID      | Categoria | Titolo                                                 | Stato     | Priorità | Evidenza                                                             | Ticket    |
| ------- | --------- | ------------------------------------------------------ | --------- | -------- | -------------------------------------------------------------------- | --------- |
| DOC-001 | H         | Snapshot incompatibili tra documenti canonici          | CONFIRMED | P0       | `CURRENT_STATUS`, `ROADMAP`, `RELEASE_GATE` riportano SHA differenti | Da creare |
| DOC-002 | G/H       | Il contratto doc-sync richiede documenti archiviati    | CONFIRMED | P0       | `check_doc_sync.sh` usa `STATUS.md` e `NEXT_STEPS.md`                | Da creare |
| DOC-003 | B/H       | `FOLLOWUP_TICKETS.md` mescola indice e specifiche      | CONFIRMED | P1       | Celle contenenti intere analisi e criteri di chiusura                | Da creare |
| DOC-004 | B         | `RELEASE_GATE.md` mescola requisiti e certificazione   | CONFIRMED | P1       | Baseline corrente e storia inserite nel gate permanente              | Da creare |
| DOC-005 | B/H       | `CURRENT_STATUS.md` contiene cronologia implementativa | CONFIRMED | P1       | Descrizioni estese dei ticket chiusi nella sezione Testo             | Da creare |
| DOC-006 | H         | `ROADMAP.md` rimanda a un percorso storico             | CONFIRMED | P1       | Riferimento operativo a `NEXT_STEPS.md`                              | Da creare |

---

## Formato di un finding

### ARCH-NNN — Titolo

**Stato:**
**Priorità:**
**Categoria:**
**Area:**
**Commit osservato:**

#### Evidenza

Indicare file, simboli, comando, output o test che dimostrano il finding.

#### Perché è un problema

Descrivere l'effetto concreto:

* regressioni;
* difficoltà di testing;
* accoppiamento;
* race condition;
* pressione sulla memoria;
* drift documentale;
* falso successo;
* rallentamento dello sviluppo.

#### Frequenza di modifica

Indicare:

* numero di commit nel periodo osservato;
* numero di autori o agenti;
* numero di regressioni correlate;
* eventuali revert.

#### Confine

Specificare chiaramente cosa appartiene al finding e cosa resta fuori.

#### Risultato richiesto

Descrivere l'invariante finale, non una soluzione implementativa obbligatoria.

#### Criteri di accettazione

* evidenza precedente non più riproducibile;
* test o gate dedicato;
* assenza di un percorso parallelo;
* nessuna regressione dei confini correlati;
* documenti canonici aggiornati;
* baseline associata allo stesso SHA.

#### Collegamenti

* Ticket:
* ADR:
* Baseline:
* Milestone:
* Finding correlati:

---

## Regole dell'audit

1. Non dichiarare un problema senza evidenza.
2. Non dichiarare risolto un finding soltanto perché il codice è cambiato.
3. Non usare percentuali manuali.
4. Non inserire soluzioni complete dentro la tabella riepilogativa.
5. Non duplicare il dettaglio già presente nel ticket.
6. Non trasformare un warning in successo.
7. Non confondere un limite ambientale con una regressione del codice.
8. Non trasferire risultati verificati da uno SHA a uno SHA successivo.
9. Non aprire due finding per la stessa causa radice.
10. Consolidare finding equivalenti prima di creare nuovi ticket.

---

## Cadenza

L'audit completo viene eseguito:

* prima di rimuovere un feature freeze;
* prima di una milestone di release;
* dopo una grande migrazione architetturale;
* quando un'area presenta regressioni ripetute;
* quando un hotspot combina alta frequenza di modifica e alta fragilità.

Gli audit intermedi possono concentrarsi su una sola categoria.

---

## Ordine raccomandato

1. falsi successi e gate fragili;
2. dipendenze cicliche e violazioni dei confini;
3. stato globale e ownership;
4. pipeline duplicate;
5. hotspot ad alta frequenza;
6. complessità locale;
7. primitive obsession;
8. ottimizzazioni non ancora dimostrate da misure.

La correttezza e l'osservabilità precedono le ottimizzazioni.
