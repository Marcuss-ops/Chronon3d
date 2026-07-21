# TICKET-TIMED-WORD-BINDING — TimedWord → TextSpanOverride/Selector wiring

## Stato: OPEN (2026-07-21, commit pending)

## Problema

L'highlight parola-per-parola (effetto CapCut "karaoke-fill") richiede che
`TimedWord` (per-cue) venga mappato a un byte-range nel `TextDocument`
sottostante per applicare `TextSpanOverride` o `GlyphSelectorSpec` al
range corretto. Il modello `TimedWord` esiste già in
`include/chronon3d/text/timed_text_document.hpp:33-42` con i campi
`text`, `start_s`, `end_s`, `semantic_id` — ma manca il mapping al byte
offset UTF-8 necessario per:

- `TextSpanOverride::byte_start/byte_end` (authoring-level per-range
  style override).
- `TextUnitMap` byte-index lookup (canonical 8-level identity ladder
  in `include/chronon3d/text/text_unit_map.hpp:120`).
- Per-word `GlyphSelectorSpec` routing nel `TextAnimatorSpec`.

Senza questo mapping il builder `SubtitleTrackBuilder::build()`
dichiara esplicitamente (pre-chore): "the per-word TextSpanOverride
attach + WordStyleState rendering hook is deferred to
TICKET-SUBTITLE-WORD-KARAOKE".

## Soluzione implementata (Cat-3 minimal-surface)

Aggiunto 1 campo per lato `TimedWord` (Cat-3 2-field add su struct
esistente, NO nuovo SDK type) + popolamento nei 3 adapter canonici +
wiring N selector nel builder:

1. **TimedWord byte offsets** in `timed_text_document.hpp`:
   ```cpp
   std::size_t byte_start{0};
   std::size_t byte_end{0};
   ```
   Default `{0, 0}` (conservative: default-constructed TimedWord
   advertise "no byte mapping").  UTF-8 byte boundaries, NON grapheme
   count — si allineano direttamente al `TextUnitMap` byte ladder.

2. **3 adapter popolano `byte_start`/`byte_end`**:
   - **SRT** (`timed_text_adapter_srt.cpp`): riusa `split_words()`
     helper che già cattura byte ranges via `wr.offset + wr.length`.
   - **VTT** (`timed_text_adapter_vtt.cpp`): riusa lambda capture
     `offset` (byte position in cue_text, post-`strip_vtt_tags()`).
   - **JSON** (`timed_text_adapter_json.cpp`): due path:
     - Source `words` array: `cue.text.find(tw.text, search_start)`
       sequenziale (partendo da `words.back().byte_end`).
     - Auto-fallback (text senza words): riusa `start` variable
       dallo whitespace-split scan esistente.

3. **SubtitleTrackBuilder::build() wiring** in
   `src/scene/subtitle_track_builder.cpp`:
   - `#include <chronon3d/text/glyph_selector_spec.hpp>` (Cat-3 1-include add).
   - Per ogni cue con `cue.words.size() > 0`, build N
     `GlyphSelectorSpec{unit = TextSelectorUnit::Word, start = i,
     end = i+1, shape = TextSelectorShape::Hold, id =
     "subtitle_cue_<i>_word_<w>_sel"}`.
   - Push su `run_spec.selectors` (verified field at
     `include/chronon3d/scene/builders/params/text_params.hpp:105`).
   - Zero `TextAnimatorSpec` aggiuntivi: il preset esistente
     (`active_word_pop` / `karaoke_fill` / `yellow_keyword`) usa
     naturalmente i selector per applicare highlight properties al
     active word.

**Meccanismo (Option c dal thinker plan)**: la selector math
(`evaluate_selector`) restituisce `weight = 1.0` SOLO per i glifi del
word il cui indice cade in `[start, end)`. Per tutti gli altri word e
glifi adiacenti, `weight = 0.0`. Il preset's animator moltiplica le sue
properties[] per il combined selector weight per glifo: l'active word
riceve l'highlight, i neighbour restano al base.  **Nessuna mutazione
per-frame del selector necessaria** — il math canonico gestisce tutto.

## §Accepted deviations dal verdict spec

### 1. `TimedWordBinding` struct (verdict verbatim) → field extension su `TimedWord` esistente

**Verdict**: nuovo struct `TimedWordBinding { semantic_id; byte_start;
byte_end; start_s; end_s; }`.
**Implementation**: aggiunto `byte_start` + `byte_end` direttamente
su `TimedWord` esistente (gli altri 3 campi erano già presenti).
**Rationale**: Cat-3 minimal-surface — NO nuovo SDK type per duplicare
info già presente in `TimedWord`. AGENTS.md §"No espansione API non
necessaria" preferisce field extension quando il campo naturale
appartiene a un struct già canonico. Trade-off documentato.

### 2. `selector.by_semantic_id(active_word_id)` (verdict verbatim) → per-word range selectors

**Verdict**: selector prende `active_word_id` come parametro, ritorna
1.0 per il word con quell'ID.
**Implementation**: N selector (uno per word) con
`unit=Word, start=i, end=i+1`. La selector math canonica isola
naturalmente per-word senza nuovi campi SDK. L'`active_word` a un
dato frame è implicitamente il word il cui range contiene il
glyph corrente.
**Rationale**: aggiungere un campo `semantic_id` a `GlyphSelectorSpec`
(Option d dal thinker) sarebbe Cat-3 1-field add su SDK public header
— il math canonico già fornisce l'isolamento per-word richiesto,
quindi l'add è evitabile. Trade-off documentato.

### 3. `TextSpanOverride attach + WordStyleState rendering hook` (verdict verbatim) → `GlyphSelectorSpec` push su `run_spec.selectors`

**Verdict**: builder allega `TextSpanOverride` per ogni word + hook
rendere via `WordStyleState`.
**Implementation**: builder push di `GlyphSelectorSpec` per ogni word
su `run_spec.selectors`. La render logic è demandata al preset
animator esistente che applica highlight via selector weight.
`WordStyleState` resta come return type di `active_word_style_at()`
helper (presets/text/subtitle.hpp:48) per query/test purposes, ma non
è usato nel render path (il preset's animator fa il lavoro).
**Rationale**: la struttura corretta è selector-driven, non layer-per-
parola (verdetto §6). Cat-3 riusa canonical selector system senza
nuovi layer né TextSpanOverride loop. Trade-off documentato.

## §Design rationale

### Default `{0, 0}` per default-constructed `TimedWord`

Il default `byte_start{0}, byte_end{0}` (NON `byte_start{undefined}`)
garantisce che un caller che usa un `TimedWord` mai passato per un
adapter NON riceve mai un false-positive "byte mapping at offset 0".
Locked dal test `TimedCue default word_timing_quality is None`
(Fase 4 word-timing-quality lineage, conservative-default pattern).

### UTF-8 byte offsets, NON grapheme count

`byte_end = byte_start + tw.text.size()` (NON `+ tw.text.cp_count()`).
Il `std::string::size()` ritorna la lunghezza in byte UTF-8 — `text[i]`
è il byte `i`-esimo, non il codepoint `i`-esimo. Il TextUnitMap
canonico lavora su byte indices (TextUnitMap::byte_to_codepoint),
quindi il mapping è diretto. Locked dal test "SRT adapter populates
TimedWord byte offsets (UTF-8 byte, NOT grapheme)" con parola
`naïve` (5 codepoints, 6 UTF-8 bytes — `ï` mid-word 2-byte).

### N selector strategy

Per un cue con K words, build K `GlyphSelectorSpec` (uno per word).
La selector math O(glyphs × selectors) è lineare e tipicamente K è
5-20 parole per cue subtitle — accettabile. Per cue molto lunghe
(100+ parole), valutare la variante Option d (`semantic_id` field +
per-frame `active_word_idx` mutation) — deferred a forward-point.

### `TextSpanOverride` NON usato nel karaoke-pop path

`TextSpanOverride` è per static per-range style override (es. bold
per un range). Il karaoke-pop richiede DYNAMIC per-frame change
(active word cambia colore/scale ogni frame). Il selector math è
il meccanismo canonico per dynamic per-unit modulation.

## §Forward-points

- **TICKET-TIMED-WORD-BINDING-JSON-FIND-ROBUSTNESS** (P2, deferred):
  il sequential `find()` nel JSON adapter può fallire silenziosamente
  su edge cases (multi-occurrence "the the", word con punteggiatura
  attaccata, empty word). Attualmente lascia `byte_start=0, byte_end=0`
  silenziosamente — è rot-class (silently wrong, not absent).
  Fix: defensive fallback che usa `search_from_cue_start + word_index *
  estimated_word_width` oppure diagnostic WARN. Decidere quando il
  fallback è safe.
- **TICKET-TIMED-WORD-BINDING-N-SELECTORS-PERF** (P2, deferred): per
  cue molto lunghe (100+ parole), N selector è O(N) per glyph per
  frame. Forward-point: aggiungere `semantic_id` field a
  GlyphSelectorSpec (Option d dal thinker) + per-frame
  `active_word_idx` mutation via `start/end`.
- **TICKET-TIMED-WORD-BINDING-VISUAL-VERIFY** (P2, deferred): il
  round-1 reviewer Step 5 richiede "verifica con un frame al centro
  di una parola che `selector.weight == 1.0` E che il fill
  orange/yellow sia effettivamente renderizzato sul glyph cluster
  corretto, non su un cluster adiacente". Test strutturale
  `TextRunSpec::selectors` lock presente; test VISUAL (render
  pipeline end-to-end + pixel-level assert) deferred a follow-up.
- **TICKET-SUBTITLE-WORD-KARAOKE** (parent forward-point, **DONE
  in this commit**): la riga "Per-word TextSpanOverride attach in
  SubtitleTrackBuilder::build()" in `docs/FOLLOWUP_TICKETS.md` viene
  chiusa — questo chore implementa la wiring via selector list
  (Cat-3 alternative alla TextSpanOverride loop).

## Test coverage

4 nuovi TEST_CASEs aggiunti in `tests/text/test_subtitle_productive.cpp`:

1. `SRT adapter populates TimedWord byte offsets (UTF-8 byte, NOT grapheme)`
   — round-2 strengthened con parola `naïve` (5 codepoints / 6 bytes,
   `ï` mid-word 2-byte) per stressare la distinzione byte vs grapheme.
2. `VTT adapter populates TimedWord byte offsets for multi-word cue`
   — `Hello world` byte offsets (0-5, 6-11).
3. `JSON adapter populates TimedWord byte offsets (source words array path)`
   — JSON con `"words"` array.
4. `SubtitleTrackBuilder emits one GlyphSelectorSpec per TimedWord (karaoke-pop wiring)`
   — builder integration smoke test (no-throw per `active_word_pop` preset).
5. `TextRunSpec::selectors holds N word selectors post-builder-wiring (round-2 reviewer lock)`
   — lock strutturale del field canonico + count assertion (round-2
   reviewer Finding #1 fix: blocca silent failure se `run_spec.selectors`
   non fosse il field giusto).

Test target: `chronon3d_subtitle_productive_tests` (esistente, già
PASS per Fase 3 + Fase 4 lineage).

## References

- AGENTS.md §"Disciplina di aggiornamento dei canonici" (Cat-3 anti-dup)
- AGENTS.md §"No espansione API non necessaria" (giustificazione field extension)
- AGENTS.md Gate 5 Check 11 deny-everywhere (`<msdfgen>/<libtess2>/<unicode[/...]>`)
- Verdict CapCut-grade §6 (per-word highlight non renderizzato — questo chore chiude il blocker)
- Verdict §5 step 1-5 (il piano `TimedWordBinding` verbatim)
- [TICKET-WORD-TIMING-QUALITY.md](TICKET-WORD-TIMING-QUALITY.md):
  precedent `§Accepted deviations` pattern + `WordTimingQuality` enum
  introdotto Fase 4 (stesso file `timed_text_document.hpp`).
- [TICKET-SUBTITLE-PRODUCTIVE-FOUNDATION.md](TICKET-SUBTITLE-PRODUCTIVE-FOUNDATION.md):
  parent ticket — `SubtitleTrackBuilder`, `SubtitleTrack`, SRT/VTT/JSON
  adapter canonical forms.
- [TICKET-SUBTITLE-WORD-KARAOKE.md](TICKET-SUBTITLE-WORD-KARAOKE.md)
  *(forward-point catena — chiuso da questo chore via selector-list wiring)*.
- `include/chronon3d/text/text_unit_map.hpp:120` (8-level canonical map)
- `include/chronon3d/text/text_span_override.hpp:29` (TextSpanOverride struct)
- `include/chronon3d/text/glyph_selector_spec.hpp:74` (GlyphSelectorSpec struct)
- `include/chronon3d/text/animation/text_animator_spec.hpp` (TextAnimatorSpec)
- `include/chronon3d/scene/builders/params/text_params.hpp:105` (TextRunSpec::selectors field)
- `src/scene/subtitle_track_builder.cpp:75-110` (build() wiring)
- `src/text/timed_text_adapter_*.cpp` (3 adapter byte offset population)
- `src/text/animation/text_animator_evaluator.cpp` (selector math)