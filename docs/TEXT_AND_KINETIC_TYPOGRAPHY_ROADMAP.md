# Chronon3D — Piano operativo per Text e Kinetic Typography

> Documento canonico per completare e migliorare il sottosistema testuale di Chronon3D.
>
> Audit del repository: **22 giugno 2026**.
>
> Questo documento distingue in modo esplicito ciò che è già implementato, ciò che è parziale e ciò che deve ancora essere costruito. Nessuna funzione deve essere dichiarata stabile soltanto perché compila o possiede un test strutturale.

---

## 1. Missione

Chronon3D deve offrire un sistema testuale code-first capace di produrre:

- titoli cinematici;
- kinetic typography;
- animazioni per glifo, grafema, parola e riga;
- sottotitoli e word highlighting;
- typewriter e text replacement;
- rich text nello stesso paragrafo;
- text on path;
- rendering multilingua deterministico;
- preset riutilizzabili e controlli componibili dall’utente.

Il sistema deve restare:

- headless;
- CPU-first;
- deterministico;
- C++ code-first;
- utilizzabile da CLI e backend;
- senza GUI o browser nel core;
- senza GPU obbligatoria;
- senza JSON come formato principale di authoring;
- basato su una sola pipeline produttiva.

Chronon3D non deve diventare un clone completo di After Effects. Deve adottarne le semantiche testuali utili mantenendo un’architettura più piccola, verificabile e adatta al rendering automatizzato.

---

## 2. Stato reale del sottosistema testo

### 2.1 Già implementato e da preservare

Sono già presenti fondazioni valide:

- `TextDocument` come modello del contenuto testuale;
- stile di default, span e paragrafi;
- shaping con FreeType, HarfBuzz e FriBidi;
- layout multilinea e paragraph composer;
- `TextRunLayout` e `PlacedGlyphRun`;
- `TextAnimatorSpec` con selector e proprietà per glifo;
- selector Range con unità Glyph, Grapheme, Character, Word e Line;
- proprietà Position, Scale, Rotation, Skew, Anchor, Opacity, Blur, Fill, Stroke, Tracking e Baseline Shift;
- DSL `authoring::Text`, `Animator` e `Selector`;
- materiali testuali, gradienti, stroke, glow e bevel;
- `AnimatedTextDocument` per sostituzione, scramble e morph;
- text on path con margini, reverse, perpendicular e force alignment;
- `TextPresetRegistry` con 22 preset built-in;
- `AnimatorResolver` che produce `TextAnimatorSpec` per i preset;
- cache di layout e glyph atlas;
- test strutturali per registry, resolver e parte del layout.

Queste componenti non devono essere riscritte da zero.

### 2.2 Implementato ma non ancora stabile

Le seguenti aree esistono, ma non hanno ancora un contratto produttivo completo:

- preset testuali: registrati e invocabili, ma senza golden frame completi;
- rich text: modello interno presente, API authoring ancora incompleta;
- text on path: funzionale, ma con animazione, RTL e regressioni visive incomplete;
- paragraph composer: alcuni casi fondamentali hanno test disabilitati;
- tracking animato: applicato post-layout, da verificare rispetto al reflow desiderato;
- Character Offset: dichiarato pre-shaping ma valutato nello stato post-layout;
- Word Cascade e Character Cascade: il comportamento visivo dipende ancora in parte dalla motion del layer invece che da selector per-unità autentici;
- Expressions V2: sperimentali e non ancora promosse al percorso stabile;
- preset registry: possiede ancora una seconda tabella manuale nel resolver.

### 2.3 Non ancora completato

Mancano ancora:

- Wiggly Selector;
- Wave Selector;
- Expression Selector stabile;
- selector casuali completi per parola, riga e glifo;
- variable font axes animabili;
- ICU line breaking per CJK e casi globali avanzati;
- color emoji/fallback completo;
- motion blur testuale validato;
- Character Value e Character Range compatibili con la semantica AE;
- line anchor e line spacing animabili;
- blur X/Y separato;
- per-character 3D completo;
- una suite visuale che certifichi i preset come stabili.

---

## 3. Problemi architetturali da correggere prima di aggiungere altre feature

## 3.1 Due fonti della verità per i preset

Oggi un preset può essere descritto in due punti:

1. `TextPresetRegistry`, con metadata, builder e motion chain;
2. `AnimatorResolver::compose_for()`, con una seconda catena di confronti sugli ID e proprietà ricostruite manualmente.

Questo crea drift possibile tra:

- comportamento del layer;
- comportamento per glifo;
- metadata del preset;
- test del registry;
- output visivo.

### Correzione obbligatoria

Il descrittore del preset deve possedere direttamente la factory dell’animator:

```cpp
struct TextPreset {
    std::string id;
    TextPresetCategory category;
    TextPresetBuilder builder;
    TextAnimatorFactory animator_factory;
};
```

Il resolver deve diventare un adapter sottile verso il registro, non una seconda tabella.

```text
preset id
    ↓
TextPresetRegistry::get(id)
    ↓
TextPresetDescriptor
    ├── metadata
    ├── builder
    ├── animator factory
    └── visual fixture id
```

### Risultato richiesto

- un preset built-in viene definito una sola volta;
- un preset personalizzato registrato dall’utente può essere risolto allo stesso modo;
- non esistono 22 rami `if/else` duplicati;
- test e authoring consultano lo stesso descrittore.

---

## 3.2 Pipeline legacy `TextAnimator`

`include/chronon3d/text/text_animator.hpp` crea un layer separato per carattere, parola, riga o glifo.

Questa è una pipeline parallela rispetto a:

```text
TextRun
    ↓
TextAnimatorSpec
    ↓
GlyphInstanceState
```

### Correzione obbligatoria

Il vecchio `TextAnimator` deve diventare:

- un adapter temporaneo che genera `TextAnimatorSpec`; oppure
- API deprecata con percorso di rimozione esplicito.

Non deve continuare a essere una seconda implementazione produttiva.

### Gate di rimozione

- parity test tra vecchio helper e animator canonico;
- migrazione di tutti i consumer;
- architecture gate che impedisca nuovi utilizzi;
- eliminazione finale del codice layer-per-unità.

---

## 3.3 Mancanza di fasi di valutazione esplicite

Non tutte le proprietà testuali possono essere applicate dopo il layout.

Il sistema deve classificare ogni proprietà secondo la fase in cui modifica il risultato.

### Fase A — Pre-shaping

Modifica il contenuto o il font prima di HarfBuzz:

- Source Text;
- Character Offset;
- Character Value;
- Character Range;
- variable font axes quando cambiano i glyph;
- sostituzioni che cambiano script, cluster o fallback.

### Fase B — Pre-layout

Modifica le metriche e può causare reflow:

- font size;
- tracking con semantica di layout;
- line spacing;
- paragraph style;
- box size;
- text path;
- first e last margin;
- rich text spans che cambiano font o dimensione.

### Fase C — Post-layout per glifo

Non deve ricostruire shaping e paragrafi:

- position;
- scale;
- rotation;
- skew;
- anchor;
- baseline shift;
- opacity;
- fill e stroke;
- tracking puramente visuale, se distinto dal tracking di layout.

### Fase D — Post-raster

Opera sull’immagine o sulla maschera rasterizzata:

- blur;
- glow;
- shadow;
- bevel;
- displacement;
- motion blur compositato.

### Correzione immediata

`CharacterOffsetProperty` è documentato come pre-shaping ma oggi viene scritto in `GlyphInstanceState`. Deve essere spostato nella fase pre-shaping e deve invalidare shaping, layout e cache dipendenti.

---

## 3.4 Claim di stabilità non verificati visivamente

I 22 preset sono presenti nel registro, ma i test attuali verificano soprattutto:

- metadata;
- presenza del builder;
- invocazione senza eccezioni;
- creazione di un nodo;
- presenza di un animator risolto.

Queste verifiche non certificano che il risultato sia corretto o leggibile.

### Regola documentale

Usare queste definizioni:

- **registrato**: il preset esiste nel registry;
- **invocabile**: il builder produce un nodo senza errore;
- **strutturalmente testato**: resolver e proprietà rispettano il contratto;
- **visivamente verificato**: golden frame e metriche approvate;
- **stabile**: test strutturali, visuali, determinismo e multi-resolution sono verdi sullo stesso commit.

Fino al completamento della suite visuale, i 22 preset non devono essere descritti come tutti stabili.

---

## 3.5 Registry e resolver non devono proliferare

Non creare un registry separato per ogni piccola variante.

Aggiungere un registry soltanto quando serve realmente:

- estensione runtime tramite ID;
- lookup deterministico;
- catalogazione pubblica;
- sostituzione di una tabella hardcoded;
- composizione da authoring o preset.

Preferire:

- un solo `TextPresetRegistry` per preset e relative factory;
- un solo evaluator canonico per selector;
- un solo sampler per `AnimatedValue`;
- un solo resolver del font;
- descrittori typed invece di mappe generiche;
- adapter temporanei, mai implementazioni parallele permanenti.

Non introdurre `TextEngineV2`, `TextPipelineV2`, `AfterEffectsText`, un secondo shaper o un secondo renderer.

---

## 4. Pipeline canonica target

La pipeline produttiva deve diventare:

```text
TextSpec / TextDocument
    ↓
SourceTextEvaluator
    ↓
PreShapingPropertyEvaluator
    ↓
TextStyleResolver
    ↓
FontResolver + HarfBuzz + FriBidi
    ↓
ParagraphComposer / TextPathComposer
    ↓
TextRunLayout
    ↓
TextUnitMap
    ↓
SelectorEvaluator
    ↓
PostLayoutGlyphAnimator
    ↓
GlyphInstanceState
    ↓
TextRasterizer
    ↓
PostRasterTextEffects
    ↓
TextRunNode
    ↓
RenderBackend
```

### Invarianti

- una sola rappresentazione del contenuto;
- una sola rappresentazione del layout;
- una sola struttura di stato per glifo;
- selector e proprietà valutati da funzioni pure;
- nessun accesso a singleton globali durante il render;
- cache key derivata dalla fase realmente interessata;
- stesso input, stesso frame e stesso seed producono lo stesso output;
- preview e render finale devono condividere le stesse semantiche;
- i content pack non possono introdurre helper di layout o rasterizzazione locali.

---

## 5. Piano di lavoro prioritario

## Blocco 1 — Canonicalizzazione preset e resolver

### Obiettivo

Eliminare la duplicazione tra `TextPresetRegistry` e `AnimatorResolver`.

### Lavoro

- aggiungere `animator_factory` al descrittore canonico;
- spostare le 22 composizioni di proprietà nel registro;
- far consultare il registro a `wire_preset_text_run_params()`;
- supportare preset custom registrati dall’utente;
- mantenere `AnimatorResolver` soltanto come facade temporanea;
- aggiungere test che dimostrino una sola definizione per ID;
- aggiungere test custom preset → animator → `TextRunParams`.

### Definition of Done

- nessuna seconda tabella degli ID;
- nessun confronto manuale duplicato per 22 preset;
- built-in e custom passano dalla stessa pipeline;
- registry, resolver e authoring producono lo stesso `TextAnimatorSpec`.

---

## Blocco 2 — Contratto delle fasi di proprietà

### Obiettivo

Stabilire dove viene valutata ogni proprietà e quali cache invalida.

### Lavoro

- introdurre `TextPropertyStage` o trait equivalente;
- classificare tutte le proprietà esistenti;
- separare tracking di layout e tracking visuale, se entrambe le semantiche sono necessarie;
- spostare Character Offset nel pre-shaping;
- definire invalidazione per source, shaping, layout, glyph state e raster;
- aggiungere test per cambi di frame e random access.

### Definition of Done

- Character Offset cambia realmente i glyph renderizzati;
- una proprietà post-layout non ricostruisce inutilmente HarfBuzz;
- una proprietà pre-layout invalida correttamente il layout;
- cache hit e miss sono spiegabili dal contratto.

---

## Blocco 3 — Ritiro della pipeline legacy

### Obiettivo

Rendere `TextAnimatorSpec` l’unico percorso produttivo per animazioni per-unità.

### Lavoro

- censire tutti gli utilizzi di `TextAnimator` legacy;
- creare adapter verso `TextAnimatorSpec` soltanto per migrazione;
- migrare composizioni e test;
- aggiungere architecture gate;
- eliminare creazione di un layer per ogni carattere/parola/glifo.

### Definition of Done

- nessun consumer produttivo usa il vecchio percorso;
- nessun nuovo codice può includere l’header legacy;
- output equivalente coperto da parity test;
- codice duplicato eliminato.

---

## Blocco 4 — Correttezza del layout e test disabilitati

### Obiettivo

Chiudere i casi fondamentali prima di aggiungere altre animazioni.

### Lavoro

- correggere single paragraph → un layout;
- correggere multiple paragraphs → più layout;
- preservare paragrafi vuoti da newline consecutivi;
- verificare wrap `None`;
- verificare whitespace exclusion per Word selector;
- riattivare tutti i test `doctest::skip()` collegati;
- aggiungere test per RTL, legature, grapheme e fallback font.

### Definition of Done

- nessun test fondamentale del builder è skipped;
- layout seriale e parallelo producono lo stesso risultato;
- cache e no-cache producono layout equivalenti;
- paragrafi vuoti non vengono persi.

---

## Blocco 5 — Visual Regression Harness

### Obiettivo

Impedire preset formalmente validi ma visivamente sbagliati.

### Matrice minima

Per ogni preset candidato stabile:

- frame iniziale;
- frame intermedio;
- frame finale;
- 1920×1080;
- 1080×1920;
- testo corto;
- testo lungo;
- sfondo chiaro;
- sfondo scuro;
- almeno un caso con scaling del layer.

### Metriche

- hash deterministico;
- bounding box dell’inchiostro;
- alpha coverage;
- overflow rispetto alla text box;
- centro visivo;
- pixel diff o metrica percettiva;
- leggibilità minima dei frame intermedi;
- tempo di rendering, senza usarlo come unico criterio.

### Definition of Done

Un preset può essere chiamato stabile solo quando:

- golden frame approvati;
- almeno tre timestamp validati;
- orizzontale e verticale validati;
- output deterministico tra scheduler supportati;
- nessun testo tagliato o invisibile nei casi previsti.

---

## Blocco 6 — Selector Engine completo

### Obiettivo

Portare la selezione per-unità vicino alle semantiche AE senza creare evaluator paralleli.

### Range Selector da completare

- ease high e ease low verificati;
- randomize order deterministico;
- reverse, center-out e edge-in;
- Grapheme distinto da Character;
- corretta semantica RTL;
- amount negativo e combine modes verificati.

### Nuovi selector

#### Wiggly

- minimum e maximum amount;
- wiggles per second;
- temporal phase;
- spatial phase;
- correlation;
- lock dimensions;
- seed deterministico;
- loop deterministico opzionale.

#### Wave

- sine;
- triangle;
- saw;
- pulse;
- frequenza;
- fase;
- propagazione per indice o posizione.

#### Expression

Promuovere solo dopo Expressions V2:

- `textIndex`;
- `textTotal`;
- `selectorValue`;
- time/frame;
- seed deterministico;
- nessun accesso mutabile globale.

### Contratto unico

Ogni selector deve produrre lo stesso tipo:

```cpp
SelectorWeight evaluate(
    const SelectorSpec&,
    const TextUnitMap&,
    GlyphIndex,
    SampleTime
);
```

Tutte le proprietà devono consumare quel peso senza implementare logica selector locale.

---

## Blocco 7 — Preset per-unità autentici

### Obiettivo

Evitare preset che usano un selector globale mentre il nome promette una cascata per parola o carattere.

### Correzioni minime

- `word_cascade` deve usare `TextSelectorUnit::Word`;
- `character_cascade` deve usare `Grapheme`, non byte e non layer separati;
- `word_pop` deve selezionare realmente una parola o una sequenza di parole;
- subtitle presets devono poter consumare timing per parola;
- stagger e offset devono essere espressi nel selector canonico;
- layer motion può restare per movimenti globali, ma non deve simulare una selezione per-unità.

### Definition of Done

- ogni nome preset corrisponde alla sua unità reale;
- selector visibile nei diagnostics;
- output deterministico con lo stesso seed;
- spazi esclusi senza rompere gli indici;
- RTL e grapheme complessi coperti da test.

---

## Blocco 8 — Rich text produttivo

### Obiettivo

Permettere stili diversi nello stesso paragrafo senza creare più layer.

### API target

```cpp
text.document()
    .span("QUESTA ")
    .span("PAROLA").color(yellow).weight(800).scale(1.25f)
    .span(" È IMPORTANTE");
```

### Lavoro

- più font nello stesso paragrafo;
- colore, dimensione, peso e tracking per span;
- stroke e materiale per span;
- ID semantico per parola;
- selector limitabile a span o semantic ID;
- highlight background adattivo;
- stile ereditato;
- shaping contestuale preservato attraverso gli span.

### Gate

- legature non spezzate inutilmente;
- script complessi corretti;
- RTL corretto;
- nessun layer per parola;
- layout deterministico.

---

## Blocco 9 — Text on Path completo

### Obiettivo

Consolidare ciò che esiste senza introdurre un secondo path engine.

### Lavoro

- usare il path system canonico esistente;
- animare first e last margin;
- animare baseline offset;
- supportare path animati con invalidazione corretta;
- chiarire open e closed path;
- validare reverse e flip normal;
- integrare selector e animator dopo il posizionamento sul path;
- testare RTL e mixed direction;
- golden frame su curve strette, self-intersection e overflow.

### Non fare

- non creare `TextPathEngineV2`;
- non duplicare arc-length sampler;
- non copiare una seconda implementazione Bezier nel modulo testo.

---

## Blocco 10 — Subtitle e word timing

### Obiettivo

Supportare video automatizzati, karaoke e highlight parola per parola.

### Lavoro

- struttura typed per cue, parola e timestamp;
- import adapter per SRT, VTT e formati esterni;
- nessun JSON obbligatorio nell’API principale;
- mapping timing → semantic word ID;
- active word fill, scale, underline e background;
- gestione overlap e gap;
- safe area e max lines;
- preset subtitle costruiti sullo stesso sistema selector/animator.

### Gate

- random access produce lo stesso stato;
- timing non dipende dal numero di thread;
- nessuna deriva tra audio time e frame time;
- testi lunghi rispettano box e safe area.

---

## Blocco 11 — Funzioni avanzate dopo la stabilizzazione

Queste funzioni non devono bloccare il primo sistema produttivo:

- variable font axes;
- ICU e line breaking globale avanzato;
- color emoji;
- per-character 3D;
- motion blur testuale avanzato;
- Text 3D estruso;
- MSDF.

Ordine consigliato:

1. variable fonts;
2. ICU/global text;
3. motion blur;
4. per-character 3D;
5. Text 3D;
6. MSDF soltanto con benchmark che dimostri un vantaggio reale.

Non introdurre dipendenze pesanti nel profilo core se possono restare opzionali.

---

## 6. Sequenza delle PR

Ogni PR deve partire da `origin/main` aggiornato, avere un solo obiettivo e includere test mirati.

| PR | Obiettivo | File principali | Dipende da |
|---|---|---|---|
| TXT-01 | Integrare animator factory nel `TextPresetRegistry` | registry, resolver, test registry | baseline corrente |
| TXT-02 | Supportare preset custom nel resolver canonico | registry, authoring, test | TXT-01 |
| TXT-03 | Introdurre il contratto `TextPropertyStage` | animator property, driver, cache tests | TXT-01 |
| TXT-04 | Correggere Character Offset pre-shaping | source evaluator, builder, shaping tests | TXT-03 |
| TXT-05 | Deprecare e adattare `TextAnimator` legacy | legacy header, adapter, architecture gate | TXT-03 |
| TXT-06 | Correggere paragraph builder e riattivare test skipped | text run builder, paragraph tests | baseline verde |
| TXT-07 | Aggiungere golden frame dei preset esistenti | visual fixtures, metrics | TXT-01, TXT-06 |
| TXT-08 | Implementare selector Wiggly/Wave nel resolver comune | selector spec/evaluator/tests | TXT-03 |
| TXT-09 | Rendere autentici Word/Character Cascade | preset descriptors, selectors, golden frames | TXT-08 |
| TXT-10 | Completare rich text authoring | document/span API/tests | TXT-06 |
| TXT-11 | Completare text on path | composer, cache, golden frames | TXT-03, TXT-06 |
| TXT-12 | Subtitle word timing | timing model, selector mapping, presets | TXT-09, TXT-10 |

### Regole Git

- `git fetch origin` prima di iniziare;
- branch `codex/<nome-blocco>` da `origin/main` aggiornato;
- una sola feature chiara;
- niente refactor non collegati;
- rebase frequente su `origin/main`;
- test del modulo toccato;
- `git status -sb` prima del push;
- nessun push diretto su `main`;
- controllare gli ultimi commit dopo il push;
- niente mega PR.

---

## 7. Test obbligatori

## Test unitari

- selector weight per ogni shape e order;
- combine modes;
- property stage classification;
- cache invalidation;
- source replacement e Character Offset;
- rich text spans;
- paragraph ranges;
- path sampling;
- preset lookup built-in e custom.

## Test di integrazione

- `TextDocument → shaping → layout → animator → raster`;
- stesso input con cache on/off;
- stesso input seriale/parallelo;
- random access dei frame;
- RTL, grapheme complessi e legature;
- preset registry → `TextRunParams` → render node;
- subtitle timing → selector → glyph state.

## Test visuali

- static text;
- multiline;
- tracking;
- stroke;
- gradient;
- blur/glow/shadow;
- per-glyph reveal;
- word cascade;
- character cascade;
- rich text;
- text on path;
- RTL;
- CJK quando ICU sarà disponibile;
- scale estreme;
- 16:9 e 9:16.

## Test architetturali

Devono fallire quando:

- viene introdotto un secondo preset table;
- un content pack implementa layout o shaping locale;
- il legacy `TextAnimator` viene usato da nuovo codice;
- un selector implementa direttamente una proprietà;
- una proprietà pre-shaping viene applicata soltanto al `GlyphInstanceState`;
- un preset dichiarato stabile non possiede fixture visuali.

---

## 8. Criterio di stabilità del sistema testo

Il sottosistema può essere dichiarato produttivo quando, sullo stesso commit:

- build core verde;
- test text unitari verdi;
- test text di integrazione verdi;
- nessun test fondamentale skipped;
- architecture gate verde;
- determinism test verde;
- visual regression suite verde;
- almeno 20 preset visivamente verificati;
- preset built-in e custom usano lo stesso registro;
- nessuna pipeline legacy produttiva parallela;
- Character Offset e proprietà di source modificano realmente shaping e layout;
- documentazione aggiornata con comandi e commit verificato.

L’assenza di errori nei test strutturali non equivale a stabilità visuale.

---

## 9. Cose da non fare

- non riscrivere FreeType/HarfBuzz/FriBidi;
- non introdurre un secondo shaper;
- non introdurre Skia, Pango o Cairo come pipeline parallela;
- non creare un layer per ogni parola o carattere;
- non aggiungere nuovi preset copiando logica in più file;
- non creare sei registry indipendenti senza una necessità concreta;
- non usare JSON come API principale;
- non inserire GUI o browser nel core;
- non rendere GPU obbligatoria;
- non iniziare Text 3D o MSDF prima della canonicalizzazione;
- non dichiarare stabile una feature senza output visuale verificato;
- non correggere problemi architetturali con micro-patch locali che lasciano due contratti attivi.

---

## 10. Priorità immediata

Il prossimo lavoro non deve essere una nuova feature estetica.

La sequenza corretta è:

```text
1. TextPresetRegistry come unica fonte della verità
2. preset custom attraverso lo stesso resolver
3. property evaluation stages
4. Character Offset pre-shaping
5. ritiro del TextAnimator legacy
6. correzione test paragraph skipped
7. visual regression dei preset
8. selector avanzati
9. rich text, path e subtitle
10. variable fonts, ICU e funzioni premium
```

### Primo milestone concreto

Il primo milestone è completato quando:

- il registro contiene metadata, builder e animator factory;
- `AnimatorResolver` non mantiene più una seconda tabella;
- un preset custom può produrre `TextAnimatorSpec`;
- Character Offset opera prima dello shaping;
- il vecchio `TextAnimator` non è più un percorso produttivo;
- i test fondamentali del paragraph builder sono attivi;
- almeno cinque preset possiedono golden frame a tre timestamp e due aspect ratio.

Solo dopo questo milestone ha senso espandere il catalogo e inseguire ulteriore parità con After Effects.
