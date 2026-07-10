# Chronon3D — Maintenance Inventory

Backlog visibile per le pulizie tecniche pianificate post-baseline verde.
Ogni rimozione è soggetta a PR separata, mirata e review-abile. Prima di
rimuovere un modulo, rispondere alle **6 domande canoniche**:

```text
□ È referenziato da un target CMake?
□ È incluso da un header pubblico?
□ È utilizzato da uno showcase reale?
□ È necessario per Linux CPU?
□ È coperto da un test utile?
□ Ha un sostituto canonico?
```

Se le risposte sono tutte negative, è un candidato forte alla rimozione.

## Stato corrente

Questo file è un **inventario**, non un piano di esecuzione. Nessuna
delle pulizie elencate è stata avviata. Per ogni rimozione serve PR
dedicata con review esplicita.

## Aree identificate

### 🪟 Windows / cross-platform residue

Riferimento storico: il progetto ha una storia Windows (CLI `.bat`,
workflow CI Windows, `#ifdef _WIN32`, documentazione build Windows).
PR‑7d ha già sostituito `linux-full-validation` con `linux-release-validation` +
`linux-experimental-validation` (presets espliciti Linux-only).
Restano da pulire:

- Codice `#ifdef _WIN32` irraggiungibile (audit mirato richiesto: capire cosa
  si compila davvero su Linux prima di rimuovere i blocchi).
- Workflow `.github/workflows/*-windows*` (se presente).
- Script `.bat` / PowerShell in `tools/` non più utilizzati.
- Documentazione che menziona build Windows come first-class target.

**Sequenza sicura (post‑video showcase):**

1. Eliminare Windows dalla matrice CI esistente.
2. Eliminare Windows dai presets attivi (se restano preset mixed).
3. Compilare su Linux e osservare i blocchi irraggiungibili.
4. Rimuovere `#ifdef _WIN32` rimasti morti.

Branch proposto: `codex/linux-only-build-cleanup` — **da fare dopo il
video showcase funzionante**, perché può creare conflitti con CMake.

### 🕐 Timed text e formati non utilizzati

Il parser JSON timed‑text nella forma corrente è fragile (chiude oggetti
alla prima `}`), incompatibile con oggetti annidati. La parte timed-text
contiene:

- Parser SRT (`src/text/timed_text_parser.cpp` — verificare).  <!-- drift-allow: stale-ref -->
- Parser VTT (se presente).
- Parser JSON timed‑text (potenzialmente fragile).
- `WordTiming`.
- `karaoke_fill` preset.
- `active_word_pop` preset.
- Test relativi (`tests/text/test_*timed*`).

**Decisioni pendenti (scegliere una):**

- **A** — Non useremo timed text → rimuovere tutto il modulo.
- **B** — Useremo SRT, non JSON → mantenere SRT + WordTiming; rimuovere
  JSON timed‑text, VTT (se non necessario), karaoke non usato.
- **C** — Solo testi programmatici → congelare o eliminare tutto il
  modulo importazione.

Branch proposto: `codex/remove-unused-timed-text-formats` — **PR separata,
solo dopo il risultato visivo del showcase.** Prima verificare che
nessuna dipendenza del sistema testuale generale punti a questi parser.

### 🗑️ Golden test camera falsi / incompleti

Molti valori sentinel non sono ancora catturati: in quel caso il test
stampa un messaggio invece di fallire. Crea falsa sicurezza.

**Soluzione A — Rimuovere temporaneamente i golden non reali.**
Mantenere solo i test matematici e visivi effettivamente validi:

- test tangente.
- test banking.
- test keep horizon.
- test projection preservation.
- test frame showcase.

**Soluzione B — Catturare golden autentici.**
Salvare hash o metriche solo per composizioni stabili. Non mantenere
48 configurazioni che non verificano davvero nulla.

Branch proposto: `codex/remove-vacuous-camera-goldens` — **buona pulizia
successiva al completamento della nuova camera.** Indagine preliminare
richiesta sui file `tests/scene/camera/test_*`.

### 🪟 SDK paths inutilizzati

Il consumer installato (`tools/install_consumer_test.sh`) verifica
alcune API camera, ma non dimostra ancora una pipeline reale di rendering.
Il `CMakeLists.txt` conserva commenti e comportamento legacy legati al
fast path e allo skip con codice 2.

**Da fare dopo lo showcase:**

- Far installare Chronon3D.
- Compilare un consumer esterno.
- Generare una scena.
- Aggiungere testo.
- Configurare camera.
- Renderizzare almeno un PNG.
- Verificare che il PNG esista e non sia vuoto.

**Rimuovere:**

- Codice di skip legacy.
- Commenti sul fast path.
- Test che si limitano a compilare senza produrre output.
- Percorsi alternativi non più utilizzati.

Branch proposto: `codex/sdk-real-render-consumer` — **trasforma lo SDK
da "compilabile" a "utilizzabile"**.

### 🧪 Showcase e contenuti vecchi

Quando arriveranno demo reali da camera/text V1, probabilmente
compariranno:

- Demo incomplete.
- Animazioni duplicate.
- Scene sperimentali.
- Asset inutilizzati.
- File output committati.
- Preset dimostrativi senza test.

**Struttura consigliata (avveniristica):**

```text
content/
  showcases/
    camera_text_v1/
    text_presets_v1/
  examples/
  experimental/
```

Il contenuto production-ready deve essere separato da quello
sperimentale. Ogni showcase valido dovrebbe avere:

- Scene source.
- Render command.
- Expected key frames.
- README di massimo 20 righe.

**Eliminare:**

- Output PNG dal repository.
- MP4 committati.
- Scene duplicate.
- Demo che implementano internamente parti del motore.
- Vecchi file chiamati `test2`, `final_new`, `demo_fixed`.

## Pulizie già completate

- ✅ **Documentazione canonica** (`codex/docs-single-source-of-truth`) —
  questo è l'unico punto di stato canonico; gli altri 4 file storici sono
  archiviati. AGENTS.md semplificato.
- (work in corso) **`linux-release-validation` + `linux-experimental-validation`**
  (PR‑7d) — preset Linux-only espliciti, `linux-full-validation` rimosso.

## Aree che NON pulire ora

Le seguenti zone NON vanno toccate in questa fase:

- Renderer software core.
- Compositing core.
- Gestione memoria.
- Threading.
- Asset loader.
- Scene graph.
- ExecutionScope.
- Public API complessiva.
- Namespace.
- Struttura globale delle directory.

Possono sembrare "sporche" ma un refactor ora sarebbe difficile da
misurare. Prima serve una scena reale che funzioni come test di
regressione.

---

**Owner di questo file:** backlog visibile. Le 5 aree di pulizia sopra
vanno affrontate in PR distinte, dopo che i gate baseline sono verdi.
Non impegnare rimozioni senza PR review approvata.
