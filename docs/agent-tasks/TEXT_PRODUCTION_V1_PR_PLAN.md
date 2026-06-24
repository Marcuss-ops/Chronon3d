# Chronon3D — Text Production V1: piano operativo per PR

> Documento operativo per completare il sistema testo e kinetic typography.
>
> Snapshot di partenza verificato: `main@d9626bd`, 23 giugno 2026.
>
> Questo piano non autorizza la creazione di branch paralleli, branch di esplorazione o branch lasciati aperti. `main` è sempre la fonte della verità.

---

## 1. Obiettivo

Portare Chronon3D da un sottosistema testo con fondazioni ampie ma ancora parzialmente validate a un **Text Production V1** realmente utilizzabile per:

- titoli cinematici;
- kinetic typography;
- animazioni per glifo, grapheme, parola e riga;
- rich text nello stesso paragrafo;
- sottotitoli con word timing;
- highlight parola corrente, karaoke e word pop;
- text on path;
- preset riutilizzabili da consumer SDK esterni;
- rendering CPU-first, headless e deterministico.

Il risultato non deve essere un clone generale di After Effects. Deve implementare le semantiche testuali realmente utili al rendering automatico, attraverso una sola pipeline produttiva.

---

## 2. Regola obbligatoria: lavorare sempre dal `main` aggiornato

Ogni task deve iniziare da un checkout pulito del `main` remoto.

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git status -sb
git log -n 5 --oneline
```

### Regole operative

1. `main` è sempre la base e la fonte della verità.
2. Non continuare a lavorare su una copia locale vecchia; rifare sempre pull --ff-only.
3. Non creare branch di prova, branch duplicati o branch per sola analisi.
4. Lavora **direttamente su `main`**, senza branch. Un task alla volta, sequenziale.
5. Pusha direttamente su `main` al termine del task integrato e verificato.

Workflow main-direct:

```bash
git fetch origin
git checkout main
git pull --ff-only origin main

# modifiche al codice

git status -sb
git diff

# test mirati

git add <solo-file-modificati>
git commit -m "feat(text): descrizione chiara"
git push origin main
git log -n 5 --oneline
```

### Divieti

- niente branch per il testo;
- niente task che mescolano testo, camera, renderer e refactor generici;
- niente nuova pipeline parallela;
- niente nuovo registry, resolver, shaper, path engine o cache quando esiste già un contratto canonico;
- niente modifica dei gate per nascondere failure;
- niente dichiarazione “verde” senza comando eseguito, exit code e commit verificato.

---

## 3. Stato di partenza da preservare

Il repository possiede già:

- FreeType, HarfBuzz e FriBidi;
- `TextDocument`, span e paragrafi;
- shaping, bidi, layout, wrap, overflow e auto-fit;
- `TextRunLayout` e `PlacedGlyphRun`;
- `TextAnimatorSpec` e `GlyphInstanceState`;
- selector per Glyph, Grapheme, Character, Word e Line;
- posizione, scala, rotazione, skew, anchor, opacity, blur, fill, stroke, tracking, baseline shift e character offset;
- text on path;
- 22 preset built-in;
- visual harness iniziale per 16 preset;
- pipeline canonica `LayerBuilder::text()` → `TextRunParams` → `text_run()` → `TextRunShape`.

Queste parti non devono essere riscritte. Il lavoro deve consolidarle, eliminare le duplicazioni residue e aggiungere solo le funzioni mancanti nel percorso canonico.

---

## 4. Ordine obbligatorio delle PR

Le PR seguenti sono sequenziali. La PR successiva parte soltanto dal `main` che contiene la precedente.

| PR | Obiettivo | Dipende da |
|---|---|---|
| TXT-00 | Baseline testo compilabile ed eseguibile | `main` corrente |
| TXT-01 | Golden e visual regression realmente bloccanti | TXT-00 |
| TXT-02 | Preset registry come unica fonte della verità | TXT-01 |
| TXT-03 | Contratto `TextPropertyStage` e invalidazione cache | TXT-02 |
| TXT-04 | Character Offset realmente pre-shaping | TXT-03 |
| TXT-05 | Ritiro della pipeline legacy `TextAnimator` | TXT-03, TXT-04 |
| TXT-06 | Correttezza paragraph/layout e test riattivati | TXT-05 |
| TXT-07 | Wiggly Selector nel resolver comune | TXT-06 |
| TXT-08 | Wave Selector e completamento Range Selector | TXT-07 |
| TXT-09 | Word/Character Cascade e Word Pop autentici | TXT-08 |
| TXT-10 | Rich text produttivo e semantic word IDs | TXT-09 |
| TXT-11 | Modello timed text e parser SRT/VTT/JSON | TXT-10 |
| TXT-12 | Subtitle compiler, highlight, karaoke e layout | TXT-11 |
| TXT-13 | Text on Path completo | TXT-06, TXT-09 |
| TXT-14 | Certificazione Text Production V1 e consumer SDK | tutte le precedenti |

---

# TXT-00 — Ripristinare una baseline testo compilabile ed eseguibile

## Obiettivo

Dimostrare sullo stesso commit che il target visuale del testo compila, collega ed esegue.

## Prima di modificare

Verificare su `main` aggiornato:

```bash
cmake --preset linux-ci
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8
ctest --test-dir build/chronon/linux-ci -R 'VRTextPreset' --output-on-failure
```

Non correggere errori non osservati. Registrare il primo errore reale e risolvere soltanto quello e gli eventuali errori a cascata direttamente collegati.

## File probabili

- `src/runtime/render_engine.cpp`
- `apps/chronon3d_cli/daemon/daemon_service.cpp`
- `tests/text/test_text_preset_visual.cpp`
- `tests/text_preset_visual_tests.cmake`
- `tests/CMakeLists.txt`
- `docs/baselines/<commit>-text-baseline.md`

## Lavoro

- verificare che tutti i consumer usino gli accessor renderer canonici;
- correggere eventuali lambda capture o deduzioni `auto` realmente fallite;
- evitare alias API aggiunti solo per mantenere vivo codice vecchio;
- non rimuovere test o target dal build graph;
- non indebolire `REQUIRE`, `CHECK`, sentinel gate o architecture gate;
- registrare configure, build, test ed exit code.

## Test obbligatori

```bash
cmake --preset linux-ci
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8
ctest --test-dir build/chronon/linux-ci -R 'VRTextPreset' -V
```

## Definition of Done

- configure `rc=0`;
- build del target `rc=0`;
- test eseguito, non soltanto compilato;
- nessun target rimosso o escluso;
- documento baseline salvato con commit e comandi;
- `git status -sb` pulito dopo il commit.

---

# TXT-01 — Golden frame e visual regression realmente bloccanti

## Obiettivo

Trasformare le sentinelle `0xDEADBEEFDEADBEEF` in riferimenti visuali approvati e rendere il test capace di rilevare regressioni reali.

## Scope

Prima matrice obbligatoria:

- 16 preset già presenti nel test;
- 1920×1080;
- 1080×1920;
- frame 0, 20, 30 e 40;
- almeno un testo corto e un testo lungo per ogni famiglia critica;
- sfondo chiaro e scuro almeno per i preset con fill, stroke, glow o box;
- PNG o altro artifact visuale ispezionabile;
- hash deterministico;
- ink bounding box;
- alpha coverage;
- centro visuale;
- controllo overflow.

## File probabili

- `tests/text/test_text_preset_visual.cpp`
- `tests/text_preset_visual_tests.cmake`
- `tests/helpers/render_regression.hpp`
- `tests/golden/text/`
- `tools/visual_quality_suite.py`
- documentazione baseline visuale.

## Lavoro

- generare i frame dal `main` corrente;
- ispezionare visivamente ogni output prima di accettarne l'hash;
- salvare golden approvate e non semplicemente il primo hash generato;
- fare fallire il test quando hash, bbox o alpha coverage divergono oltre la soglia;
- mantenere metriche diagnostiche leggibili nel failure output;
- non usare il tempo di rendering come unica metrica di correttezza;
- aggiungere un comando esplicito per aggiornare le golden, separato dal normale test.

## Definition of Done

- nessuna sentinella non catturata nella matrice dichiarata stabile;
- test fallisce alterando intenzionalmente un riferimento;
- golden presenti per entrambi gli aspect ratio;
- almeno tre timestamp effettivamente validati per preset;
- output non vuoto, non tagliato e leggibile;
- seriale e parallelo producono gli stessi riferimenti quando supportati.

---

# TXT-02 — `TextPresetRegistry` unica fonte della verità

## Obiettivo

Eliminare la seconda tabella manuale presente in `AnimatorResolver::compose_for()`.

## Contratto target

```cpp
struct TextPreset {
    std::string id;
    std::string display_name;
    TextPresetCategory category;
    TextPresetBuilder builder;
    TextAnimatorFactory animator_factory;
    std::string visual_fixture_id;
};
```

## File probabili

- `include/chronon3d/registry/text_preset_registry.hpp`
- `src/registry/text_preset_registry.cpp`
- `include/chronon3d/registry/animator_resolver.hpp`
- `include/chronon3d/registry/text_preset_resolver.hpp`
- `tests/test_text_preset_registry.cpp`
- architecture gate relativo a registry/resolver.

## Lavoro

- aggiungere `animator_factory` al descrittore già esistente;
- spostare nel registry le composizioni delle proprietà dei 22 preset;
- trasformare `AnimatorResolver` in facade sottile verso il registry;
- supportare preset custom registrati dall'utente;
- far usare lo stesso descrittore a builder, resolver, authoring e test;
- eliminare confronti `if/else` duplicati per ID;
- mantenere la stessa pipeline per preset built-in e custom.

## Test obbligatori

- ogni ID built-in esiste una sola volta;
- lookup registry → animator factory → `TextAnimatorSpec`;
- custom preset → registry → resolver → `TextRunParams`;
- ID sconosciuto produce errore o fallback documentato;
- registry congelato rifiuta nuove registrazioni;
- output visuale dei preset esistenti resta equivalente.

## Definition of Done

- nessuna seconda tabella dei 22 ID;
- nessun confronto manuale duplicato nel resolver;
- built-in e custom seguono lo stesso percorso;
- visual regression TXT-01 resta verde.

---

# TXT-03 — Contratto `TextPropertyStage`

## Obiettivo

Dichiarare esplicitamente in quale fase viene valutata ogni proprietà e quali cache invalida.

## Contratto target

```cpp
enum class TextPropertyStage {
    PreShaping,
    PreLayout,
    PostLayout,
    PostRaster
};
```

Ogni proprietà deve dichiarare:

- fase;
- necessità di rifare shaping;
- necessità di rifare layout;
- necessità di invalidare glyph state;
- necessità di invalidare raster/effects;
- possibilità di essere valutata senza ricostruire le fasi precedenti.

## Classificazione minima

### PreShaping

- Source Text;
- Character Offset;
- Character Value;
- Character Range;
- sostituzioni che cambiano script, cluster o fallback.

### PreLayout

- font size;
- tracking metrico;
- line spacing;
- paragraph style;
- box size;
- path e margini;
- rich text che cambia font o size.

### PostLayout

- position;
- scale;
- rotation;
- skew;
- anchor;
- opacity;
- fill e stroke color;
- baseline shift visuale;
- tracking visuale.

### PostRaster

- blur;
- glow;
- shadow;
- raster effects.

## File probabili

- `include/chronon3d/text/text_animator_property.hpp`
- `src/text/text_animator_property.cpp`
- `src/text/text_run_driver.cpp`
- layout/shaping cache;
- test text mirati.

## Test obbligatori

- classificazione di ogni variant;
- proprietà post-layout non richiama HarfBuzz;
- proprietà pre-layout invalida il layout;
- proprietà pre-shaping invalida shaping e layout;
- cache on/off produce lo stesso output;
- random access produce lo stesso stato del rendering sequenziale.

## Definition of Done

- nessuna proprietà viene valutata in una fase implicita;
- cache hit/miss è spiegabile dal contratto;
- architecture gate impedisce proprietà pre-shaping applicate soltanto a `GlyphInstanceState`.

---

# TXT-04 — Character Offset realmente pre-shaping

## Obiettivo

Fare in modo che Character Offset cambi realmente il testo inviato a HarfBuzz.

## Lavoro

- applicare l'offset al contenuto prima di shaping;
- definire il comportamento per ASCII, Unicode e caratteri non supportati;
- non modificare byte UTF-8 in modo diretto;
- preservare cluster e grapheme validi;
- rifare font fallback quando il carattere risultante non esiste nel font corrente;
- invalidare shaping e layout soltanto quando necessario;
- rimuovere o rendere diagnostico il vecchio campo post-layout non produttivo.

## Test obbligatori

- `A` con offset `+1` produce il glyph di `B`;
- wrap documentato per gli intervalli supportati;
- cambio di glyph modifica la larghezza quando le metriche differiscono;
- fallback font corretto;
- legature e cluster non corrotti;
- RTL e grapheme complessi non crashano;
- cache e no-cache equivalenti;
- random access deterministico.

## Definition of Done

- `character_offset` cambia `glyph_id` e, quando necessario, layout;
- nessun semplice numero viene scritto nello stato finale senza effetto visivo;
- golden dedicata presente.

---

# TXT-05 — Ritiro della pipeline legacy `TextAnimator`

## Obiettivo

Eliminare il percorso che crea un layer separato per carattere, parola, riga o glyph.

## Prima di modificare

```bash
git grep -n "text_animator.hpp"
git grep -n "TextAnimator"
git grep -n "per_character\|per_word\|per_glyph"
```

Classificare ogni consumer:

- produttivo;
- test;
- esempio;
- legacy morto.

## Lavoro

- creare un adapter temporaneo verso `TextAnimatorSpec` solo dove serve migrazione;
- migrare i consumer produttivi;
- aggiungere parity test;
- impedire nuovi include legacy tramite architecture gate;
- eliminare creazione layer-per-unità;
- rimuovere il codice legacy dopo la migrazione completa.

## Definition of Done

- nessun consumer produttivo usa il vecchio percorso;
- nessun nuovo codice può includere l'header legacy;
- output equivalente coperto da test;
- niente secondo evaluator per selector o proprietà.

---

# TXT-06 — Correttezza paragraph e layout

## Obiettivo

Chiudere i casi fondamentali del layout prima di aggiungere altri preset.

## Lavoro

- single paragraph → un layout;
- multiple paragraphs → layout distinti;
- newline consecutivi preservano paragrafi vuoti;
- wrap `None` realmente rispettato;
- whitespace escluso correttamente dai Word selector;
- paragrafi RTL e mixed-direction;
- grapheme distinti da code point e byte;
- legature preservate;
- fallback font verificato;
- rimuovere `doctest::skip()` dai test fondamentali;
- verificare equivalenza seriale/parallela e cache/no-cache.

## Definition of Done

- nessun test fondamentale del builder è skipped;
- paragrafi vuoti non vengono persi;
- layout seriale e parallelo coincidono;
- test RTL, legature, grapheme e fallback verdi.

---

# TXT-07 — Wiggly Selector

## Obiettivo

Integrare Wiggly nel selector evaluator canonico senza creare un evaluator separato.

## Prima di implementare

Cercare branch, commit, test o file già preparati. Riutilizzare il lavoro esistente se è compatibile con `main`; non riscrivere una seconda versione.

## Parametri minimi

- minimum amount;
- maximum amount;
- wiggles per second;
- temporal phase;
- spatial phase;
- correlation;
- lock dimensions;
- seed deterministico;
- loop deterministico opzionale.

## Contratto

Wiggly deve produrre un peso o un contributo attraverso il selector engine comune. Le proprietà non devono contenere logica Wiggly locale.

## Test obbligatori

- stesso seed → stesso risultato;
- seed diverso → risultato diverso;
- random access equivalente al sequenziale;
- seriale/parallelo equivalente;
- loop chiude senza salto quando attivo;
- lock dimensions rispettato;
- comportamento per Glyph, Grapheme, Word e Line.

## Definition of Done

- Wiggly usa `TextUnitMap` e il contratto selector canonico;
- nessun RNG globale mutabile;
- golden in almeno due aspect ratio.

---

# TXT-08 — Wave Selector e completamento Range Selector

## Obiettivo

Aggiungere propagazioni regolari e chiudere le semantiche mancanti del Range Selector.

## Wave minimo

- sine;
- triangle;
- saw;
- pulse;
- frequenza;
- fase;
- propagazione per indice;
- propagazione per posizione.

## Range da completare o certificare

- Ease High;
- Ease Low;
- Amount negativo;
- combine modes;
- reverse;
- from-center;
- to-center;
- edge-in/edge-out se previste dal contratto;
- randomize order deterministico;
- differenza Grapheme/Character;
- RTL;
- esclusione whitespace.

## Definition of Done

- Wave e Range usano lo stesso `evaluate_selector`/resolver comune;
- nessuna proprietà implementa direttamente la selezione;
- test matematici e visuali verdi;
- comportamento documentato per quantità negative e combinazioni multiple.

---

# TXT-09 — Preset per-unità autentici

## Obiettivo

Fare corrispondere il nome del preset alla vera unità selezionata.

## Correzioni obbligatorie

- `word_cascade` usa `TextSelectorUnit::Word`;
- `character_cascade` usa `TextSelectorUnit::Grapheme`;
- `word_pop` seleziona realmente una parola o una sequenza di parole;
- stagger e offset sono rappresentati nel selector canonico;
- layer motion resta soltanto per movimento globale;
- spazi esclusi senza rompere gli indici;
- diagnostics mostrano unità, range, ordine e seed.

## Test obbligatori

- una parola alla volta è osservabile nei frame intermedi;
- un grapheme complesso resta indivisibile;
- RTL usa ordine visuale/logico documentato;
- stesso seed deterministico;
- golden iniziale, intermedia e finale per ogni preset.

## Definition of Done

- nessun preset “word” usa un selector globale;
- nessun preset “character” lavora per byte;
- nomi e comportamento coincidono.

---

# TXT-10 — Rich text produttivo

## Obiettivo

Supportare stili diversi nello stesso paragrafo senza creare più layer.

## API target

```cpp
text.document()
    .span("QUESTA ")
    .span("PAROLA")
        .color(yellow)
        .weight(800)
        .scale(1.25f)
        .semantic_id("keyword")
    .span(" È IMPORTANTE");
```

## Funzioni minime

- più font nello stesso paragrafo;
- colore, size, weight e tracking per span;
- stroke e materiale per span;
- ID semantico per parola/span;
- selector limitabile a span o semantic ID;
- highlight background adattivo;
- stile ereditato;
- shaping contestuale preservato attraverso gli span.

## Test obbligatori

- legature non spezzate inutilmente;
- script complessi corretti;
- RTL corretto;
- nessun layer per parola;
- layout deterministico;
- selector applicato soltanto allo span target.

---

# TXT-11 — Timed text e parser

## Obiettivo

Definire il modello dati canonico per sottotitoli e word timing.

## Modello minimo

```cpp
struct TimedWord {
    std::string text;
    SampleTime start;
    SampleTime end;
    std::string semantic_id;
};

struct SubtitleCue {
    SampleTime start;
    SampleTime end;
    std::vector<TimedWord> words;
};
```

## Lavoro

- modello typed nel core testo;
- adapter SRT;
- adapter WebVTT;
- adapter JSON esterno;
- nessun JSON obbligatorio nell'API principale;
- mapping timing → semantic word ID;
- validazione overlap, gap, ordine e timestamp;
- conversione coerente tra audio time, frame e `SampleTime`.

## Test obbligatori

- parsing SRT/VTT;
- cue multilinea;
- timestamp non ordinati rifiutati o normalizzati secondo contratto;
- overlap e gap documentati;
- nessuna deriva audio/frame;
- random access produce la stessa parola attiva.

---

# TXT-12 — Subtitle compiler, highlight, karaoke e layout

## Obiettivo

Costruire i preset subtitle sullo stesso sistema selector/animator.

## Funzioni minime

- active word fill;
- active word scale/pop;
- underline;
- background adattivo;
- karaoke progressivo;
- max lines;
- safe area;
- line breaking controllato;
- politiche 16:9, 9:16 e 1:1;
- gestione overlap e gap;
- almeno 8 preset subtitle.

## Architettura

```text
TimedText
→ semantic word IDs
→ subtitle compiler
→ TextDocument/rich spans
→ selector canonico
→ TextAnimatorSpec
→ TextRunShape
```

## Definition of Done

- nessuna pipeline subtitle separata dal sistema testo;
- stessa scena produce lo stesso stato in random access;
- testi lunghi rispettano box e safe area;
- almeno 8 preset con golden nei tre aspect ratio previsti.

---

# TXT-13 — Text on Path completo

## Obiettivo

Consolidare il path system esistente senza introdurre `TextPathEngineV2`.

## Lavoro

- usare il sampler path canonico;
- first e last margin animabili;
- baseline offset animabile;
- path animati con invalidazione corretta;
- open e closed path;
- reverse;
- flip normal;
- perpendicular e force alignment verificati;
- selector e animator applicati dopo il posizionamento sul path;
- RTL e mixed direction;
- overflow e curve strette.

## Divieti

- non duplicare il sampler arc-length;
- non copiare una seconda implementazione Bezier;
- non creare un secondo path engine dentro il modulo testo.

## Definition of Done

- golden per curva aperta, chiusa, stretta e animata;
- cache corretta al cambio del path;
- nessun salto in random access;
- RTL e reverse coperti da test.

---

# TXT-14 — Certificazione Text Production V1

## Obiettivo

Dimostrare che il sistema è produttivo attraverso test, consumer esterno e documentazione sincronizzata sullo stesso commit.

## Requisiti finali

- almeno 20 preset generali verificati;
- almeno 8 preset subtitle verificati;
- input word timing JSON/SRT/VTT;
- rich text e styling per parola;
- highlight, karaoke e word pop;
- golden 16:9 e 9:16 per tutti i preset dichiarati stabili;
- golden 1:1 per i preset subtitle;
- testo corto e lungo;
- almeno tre timestamp per preset;
- output seriale e parallelo deterministico;
- cache on/off equivalente;
- random access equivalente al sequenziale;
- consumer SDK fuori-tree che usa soltanto header pubblici e `Chronon3D::SDK`;
- consumer che registra o usa un preset, renderizza un frame e scrive un PNG;
- documentazione pubblica con API e comando riproducibile.

## Gate finale

Sul medesimo commit:

```bash
cmake --preset linux-ci
cmake --build --preset linux-ci --parallel 8
ctest --test-dir build/chronon/linux-ci --output-on-failure

cmake --preset linux-lean-dev
cmake --build --preset linux-lean-dev --parallel 8
ctest --test-dir build/chronon/linux-lean-dev --output-on-failure

# architecture e renderer gates
# install consumer
# full validation
```

Salvare risultati, commit, comandi ed exit code sotto `docs/baselines/`.

## Definition of Done

Text Production V1 può essere dichiarato chiuso soltanto quando:

- tutti i gate sopra sono verdi sullo stesso SHA;
- nessun test fondamentale è skipped;
- nessuna sentinella dichiarata stabile è vuota;
- nessuna pipeline legacy produttiva parallela resta attiva;
- built-in e custom preset usano lo stesso registry;
- il consumer SDK esterno renderizza realmente testo animato;
- `STATUS.md`, `NEXT_STEPS.md`, `ROADMAP.md` e questo piano descrivono lo stesso stato osservato.

---

## 5. Funzioni da non iniziare prima di Text Production V1

Non aprire PR per queste funzioni finché TXT-14 non è chiusa:

- Text 3D estruso;
- bevel 3D avanzato;
- MSDF;
- per-character 3D completo;
- morph e displacement avanzati;
- color emoji completo;
- ICU globale;
- variable font axes;
- Expression Selector produttivo;
- motion blur testuale premium.

Eccezione: una funzione può essere studiata o documentata senza branch, ma non deve essere implementata se sottrae tempo ai blocchi V1.

Ordine successivo consigliato:

1. variable fonts;
2. ICU/global text;
3. motion blur testuale;
4. Expression Selector;
5. per-character 3D;
6. Text 3D;
7. MSDF soltanto dopo benchmark CPU che ne dimostri il vantaggio.

---

## 6. Checklist obbligatoria per ogni PR

### Prima del codice

- [ ] `git pull --ff-only origin main` (se necessario: `git fetch origin` prima)
- [ ] `git status -sb`
- [ ] `git log -n 5 --oneline`
- [ ] ricerca del codice esistente completata
- [ ] un solo task attivo alla volta
- [ ] file ownership e scope definiti

### Durante il codice

- [ ] una sola feature chiara
- [ ] nessun file fuori tema
- [ ] nessuna duplicazione di registry/resolver/sampler/cache
- [ ] test aggiunti o aggiornati
- [ ] documentazione aggiornata soltanto per stato osservato

### Prima del push

- [ ] `git fetch origin && git pull --ff-only origin main`
- [ ] `git status -sb`
- [ ] `git diff`
- [ ] test mirati verdi
- [ ] nessun output o file generato committato
- [ ] commit piccolo e leggibile

Dopo il push:

- [ ] `git log -n 5 --oneline`
- [ ] CI/check osservati
- [ ] nessun claim “verde” senza prova
- [ ] task successivo già pronto su `main` aggiornato

---

## 7. Regola di arresto

Quando una PR scopre un errore fuori scope:

1. non correggerlo nello stesso diff salvo blocco diretto e inseparabile;
2. documentare file, comando ed errore;
3. completare o fermare la PR corrente in modo pulito;
4. tornare su `main` aggiornato;
5. decidere se il problema richiede davvero una nuova PR;
6. non creare task paralleli sullo stesso scope.

Il progresso è misurato da codice canonico, test eseguiti, golden approvate e una baseline riproducibile su `main`.