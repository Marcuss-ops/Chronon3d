# Chronon3D — Miglioramenti e Strategia (IT)

> Documento complementare a `IMPROVEMENTS.md` e `docs/improvements/`.
> Focus: **motion system**, **artefatti residui**, e **pipeline qualità produzione**.

> **Language status / Stato lingua:** Questo documento è in italiano.
> La versione canonica in inglese dei "completamenti" elencati qui vive in
> `docs/IMPROVEMENTS.md` (vedi sezione "🔍 Cose Già Implementate").
> Per la roadmap forward-looking, vedi `docs/improvements/00_INDEX.md`.
>
> Questa pagina resta come changelog storico del team di sviluppo italiano;
> eventuali nuove entries vanno aggiunte in inglese a `IMPROVEMENTS.md`
> e poi ri-mirrorate qui se rilevanti.

---

## Stato Attuale (Giugno 2026)

### ✅ Completato (sessione corrente)

| Area | Cosa | Commit |
|------|------|--------|
| **FontEngine + HarfBuzz** | `FontEngine` con FreeType+HarfBuzz shape_text(), glyph bbox cache con LruCache | `HEAD` |
| **FontEngine Pipeline Wiring** | FontEngine* da LayerBuilder → Layer → RenderNode → rasterizer. `shared_font_engine()` singleton | `HEAD` |
| **Per-Glyph Text Animation** | `TextAnimMode::ByGlyph`, `split_glyphs()` con cluster substring extraction | `HEAD` |
| **Card3DMaterial** | 3D card material rasterizer con homography projection | `HEAD` |
| **DepthGrade** | Depth-aware grading node | `HEAD` |
| **NaN/Inf guards** | Blend pipeline: alpha.hpp, blend_mode.hpp, shape_rasterizer, projected_card, highway kernels, software_compositor, blend2d_bridge | `3fb8a2c` |
| **Framebuffer shift** | `shift()` ora clear delle aree vacate (top, bottom, left, right strips) | `3fb8a2c` |
| **HDR Add blend** | Rimosso `std::min` da `compositor::blend()` Add mode — HDR puro | `3fb8a2c` |
| **Clamp a 1.0** nei write path | blend2d_bridge, projected_card, bloom — clamping solo nella scrittura finale, non nel blend math | `3fb8a2c` |
| **AnimatedCamera2_5D** | Camera con position/rotation/fov/zoom/focus_z animabili via `AnimatedValue` con keyframe | `3fb8a2c` |
| **CameraRig** | 8 preset: hero_push_in, orbit_yaw, parallax_pan, dolly_zoom, focus_pull, low_angle_reveal, subtle_float | `3fb8a2c` |
| **SceneBuilder `.animated_camera()`** | Integrazione CameraRig nel builder | `3fb8a2c` |
| **Bloom NaN guard** | Sostituito raw struct init con NaN/Inf guard esplicita — non usa `compositor::blend()` perché Add mode early-exita su `src.a <= 0` | `2c7366c` |
| **Golden images** | Rigenerate per bloom/blend pipeline changes | `2c7366c` |

### 📋 Già esistente nella repo

| Feature | Dove |
|---------|------|
| **Keyframe system** | `AnimatedValue<T>` con easing, loop mode, ping-pong, hold, expression |
| **Easing** | Linear, Quad, Cubic, Expo, Sine, Back, Elastic, Bounce, Smoothstep, Cubic Bezier |
| **AnimatedTransform** | position, rotation_euler, scale, anchor, opacity — tutto animabile |
| **Layer duration/offset** | `from`, `duration`, `time_offset`, transition in/out |
| **Sequencer** | `Sequence` e `SequenceContext` per slicing temporale |
| **MotionAnimation** | `MotionAnimation` con start/end/easing/modifier |
| **CameraRig presets** | 8 preset cinematici (sessione corrente) |
| **LayerTransitionSpec** | transition_id, direction, duration, delay, easing |
| **TransitionNode** | crossfade, slide, wipe_linear, smooth_wipe, circle_iris, flash |
| **Dirty rects** | Dirty tiles con bitmask, clipping per nodo, scroll optimization |
| **MotionBlurSettings** | enabled, samples, shutter_angle (nella camera) |
| **Test golden** | 375+ test renderer, 255 test core, 73 test scene, golden images automatics |

---

## Fase 1 — Fix Artefatti (quasi completo ✅)

### Cosa è stato fatto
- NaN/Inf guards in tutta la pipeline blend
- HDR clamp rimosso dal blend math (mantenuto nei write path)
- Bloom NaN guard
- Framebuffer shift clear vacated areas
- Golden images rigenerate

### Cosa rimane / bug aperti

#### 1a. Tile rendering con effetti spatiali — SEAMS ai bordi tile

**Problema:** Quando un layer ha blur/glow/shadow/bloom con spatial spread e il sistema tile è attivo, ogni tile renderizza l'effetto indipendentemente. Il blur non ha accesso ai pixel dei tile adiacenti → seams visibili ai bordi dei tile.

**Dove:** `src/render_graph/pipeline/scene.cpp` — tile execution loop (righe ~733-757)

**Root cause:** `is_safe_for_dirty_rects()` in `scene_internal.hpp` già forza full-frame quando un layer ha blur/glow/bloom/shadow, ma lo fa tramite bbox = `{0,0,width,height}`. Questo rende TUTTI i tile dirty, ma la tile execution è ancora attiva. Ogni tile renderizza l'effetto sul proprio clip rect senza dati dei tile vicini.

**Fix:**
```cpp
// In scene.cpp, prima del tile execution loop:
bool has_spatial_effects = false;
for (const auto& [name, state] : dirty_out.layer_bboxes) {
    if (state.bbox.x0 <= 0 && state.bbox.y0 <= 0 &&
        state.bbox.x1 >= width && state.bbox.y1 >= height) {
        // Effect forced full-frame — disabilita tile execution
        has_spatial_effects = true;
        break;
    }
}
// Se full-frame a causa di effetti, forza single-pass
if (has_spatial_effects && use_tile_execution) {
    use_tile_execution = false;
    // Fallback a single-pass execution
}
```

**NOTA:** Questo fix disabilita il tile rendering quando ci sono effetti con spread — non è un fix perfetto (sacrifica performance per correttezza), ma è sicuro e facile da implementare. L'ottimizzazione tile+effetti richiederebbe l'espansione del clip rect per tile (complessità alta).

**Priorità:** ALTA — gli seams tile sono il più probabile artefatto visibile rimasto

---

#### 1b. Bloom accumulation in glow path — clamp a 1.0

**Problema:** In `effect_stack.cpp`, `accumulate_glow_pass()` e `accumulate_scaled_glow_pass()` usano `std::min(1.0f, acc.r + g.r)` — clamp a 1.0. Questo limita l'accumulo HDR del glow.

```cpp
// effect_stack.cpp ~riga 60
acc.r = std::min(1.0f, acc.r + g.r);
acc.g = std::min(1.0f, acc.g + g.g);
acc.b = std::min(1.0f, acc.b + g.b);
```

**Fix:** Rimuovere il `std::min` per permettere accumulo HDR. La scrittura finale nel framebuffer farà il clamp appropriato al momento della conversione sRGB/output.

**Priorità:** MEDIA — colpisce solo glow multi-layer con intensità elevata

---

#### 1c. DropShadow composite — ⚠️ FALSO ALLARME (codice originale corretto)

**Analisi errata in versione precedente di questo documento.**

`compositor::blend(src, dst, mode)` — primo parametro è `src` (layer superiore), secondo è `dst` (layer inferiore).

Il codice originale era:
```cpp
fb_row[dx] = compositor::blend(fb_row[dx], shadow_px, BlendMode::Normal);
//          src = fb_row (contenuto), dst = shadow (ombra)
```

Risultato: `out = contenuto * contenuto.a + ombra * (1 - contenuto.a)`
- Dove il contenuto è opaco → solo contenuto visibile ✅
- Dove il contenuto è trasparente → ombra visibile ✅

Questo è il comportamento corretto per un drop shadow: **shadow dietro al contenuto**. Nessun fix necessario.

---

#### 1d. DropShadow — usa `max(a,b)` per merge contact+ambient

**Problema:** In `effect_stack.cpp`, lo shadow contact + ambient vengono fusi con `max(r,g,b,a)` — dovrebbero usare alpha compositing, non channel-wise max. Due ombre semi-trasparenti sovrapposte non dovrebbero usare max.

**Fix:** Usare `compositor::blend(ambient, contact, BlendMode::Normal)` oppure renderizzare entrambi i layer in un unico shadow_map_fb.

**Priorità:** BASSA — effetto visivo sottile

---

## Fase 2 — Motion System (non iniziata)

### 2a. Timeline/Sequencer professionale

**Problema:** Oggi l'animazione è per-proprietà (keyframe su position, opacity, ecc.). Manca un sistema che organizzi il movimento come una sequenza registrata.

**Cosa serve:**
```cpp
// API goal:
Timeline timeline;
timeline.add(card_layer).from(0).duration(90);
timeline.stagger(cards, 4_frames, left_to_right);
timeline.sequence({ intro, camera_push, card_reveal, outro });
```

**Dove implementare:**
- `include/chronon3d/timeline/timeline.hpp` (nuovo)
- `src/runtime/timeline.cpp` (nuovo)

**Pattern:**
```cpp
struct TimelineAction {
    std::string layer_name;
    Frame from;
    Frame duration;
    EasingCurve ease_in;
    EasingCurve ease_out;
};

class Timeline {
    std::vector<TimelineAction> m_actions;
    
public:
    Timeline& add(const std::string& layer) {
        m_actions.push_back({layer});
        return *this;
    }
    Timeline& from(Frame f) { m_actions.back().from = f; return *this; }
    Timeline& duration(Frame d) { m_actions.back().duration = d; return *this; }
    
    // Build AnimatedTransform keyframes from the timeline
    void apply_to(SceneBuilder& s, const FrameContext& ctx);
};
```

### 2b. Stagger System

**Problema:** Non esiste un modo per sfalsare l'entrata di N elementi uguali (card, icone, testo).

**Cosa serve:**
```cpp
// API goal:
stagger(cards, {
    .delay = 3_frames,
    .order = StaggerOrder::LeftToRight,
    .randomize = 0.2f,
    .easing = Easing::OutCubic
});
```

**Dove implementare:**
- `include/chronon3d/presets/stagger.hpp` (nuovo)

**Pattern:**
```cpp
enum class StaggerOrder { RowMajor, ColumnMajor, Random, Radial, Spiral };

struct StaggerConfig {
    Frame delay_per_unit{4};
    StaggerOrder order{StaggerOrder::RowMajor};
    float randomize{0.0f};
    EasingCurve easing{Easing::OutCubic};
};

void stagger(
    std::span<LayerBuilder*> layers,
    const StaggerConfig& config,
    const FrameContext& ctx
);
```

### 2c. Motion Presets (slide_in, pop, settle, float)

**Problema:** Non esistono preset di motion che si possano applicare con una riga.

**Cosa serve:**
```cpp
// API goal:
MotionPreset::slide_in(card, { .direction = Direction::Left, .duration = 24 });
MotionPreset::soft_pop(button, { .overshoot = 0.08f });
MotionPreset::float_idle(icon, { .amplitude = 5.0f, .period = 60 });
```

**Dove implementare:**
- `include/chronon3d/presets/motion_presets.hpp` (esiste già, va esteso)

**Preset minimi:**
| Preset | Descrizione | Parametri chiave |
|--------|-------------|------------------|
| `slide_in` | Entra da un lato con easing | direction, distance, duration, easing |
| `pop` | Scala da 0 a 1 con overshoot | overshoot, settle_frames, easing |
| `float` | Galleggia con movimento sinusoidale | amplitude_x/y, period, phase |
| `settle` | Arriva con smorzamento elastico | stiffness, damping |
| `orbit` | Orbita attorno a un punto | radius, speed, tilt |
| `tracking_reveal` | Tracking letter-spacing che si apre | start_tracking, end_tracking, duration |

### 2d. Text Animator (per-character)

**Problema:** Il testo entra come blocco unico. Manca animazione per-carattere/parola.

**Cosa serve:**
```cpp
// API goal:
TextAnimator anim;
anim.mode(TextAnimMode::ByCharacter)
    .delay_per_unit(2_frames)
    .opacity({0.0f, 1.0f})
    .position({-20, 0}, {0, 0})
    .tracking({0.5f, 0.0f});
```

**Dove implementare:**
- `include/chronon3d/animation/text_animator.hpp` (nuovo)
- `src/backends/text/text_animator.cpp` (nuovo)

**Pattern:**
```cpp
struct TextAnimUnit {
    int char_index;
    int word_index;
    Vec2 base_position;     // from layout engine
    Vec2 animated_offset;
    float opacity;
    float tracking_offset;
};

class TextAnimator {
    std::vector<TextAnimUnit> m_units;
    TextAnimMode m_mode{TextAnimMode::ByCharacter};
    Frame m_delay_per_unit{3};
    
    void build(const TextLayoutResult& layout);
    void evaluate(Frame frame, std::span<TextAnimUnit> output);
    void apply_to(SceneBuilder& s);
};
```

---

## Fase 3 — Motion Blur Reale

**Problema:** `MotionBlurSettings` esiste nella camera con `enabled`, `samples`, `shutter_angle`, ma non viene usato nella pipeline di rendering. Il campo `motion_blur` in `RenderSettings` è letto ma il rendering multi-sample non è implementato.

**Cosa serve:**
```cpp
// Pipeline goal (in render_pipeline_composition.cpp):
if (settings.motion_blur.enabled && settings.motion_blur.samples > 1) {
    for (int s = 0; s < samples; ++s) {
        float t_offset = shutter_angle / 360.0f * (s / float(samples) - 0.5f);
        Scene sub_scene = comp.evaluate(frame, t_offset);
        auto sub_fb = render_scene(sub_scene, ...);
        accumulate(sub_fb, weight);
    }
    normalize(output_fb);
}
```

**Dove implementare:**
- `src/render_graph/pipeline/render_pipeline_composition.cpp` — il loop di accumulazione
- `include/chronon3d/render_graph/render_pipeline.hpp` — wire up motion blur settings
- `src/backends/software/software_renderer.cpp` — parallelizzare i samples

**Ottimizzazione:** Parallelizzare i samples con `tbb::parallel_for`:
```cpp
std::vector<OwnedFB> sample_fbs(samples);
tbb::parallel_for(tbb::blocked_range<int>(0, samples), [&](auto& r) {
    for (int s = r.begin(); s < r.end(); ++s) {
        sample_fbs[s] = render_sample(comp, frame, s, samples);
    }
});
for (int s = 0; s < samples; ++s) {
    accumulate_simd(output, *sample_fbs[s], weight);
}
```

---

## Fase 4 — Scene Presets Professionali

**Problema:** Non esistono preset di scena completi. Per creare una intro SaaS o un kinetic title, devi scrivere tutto da zero.

**Cosa serve:**
```cpp
// API goal:
Anim::hero_title_reveal(ctx, {
    .title = "MASTERCLASS",
    .subtitle = "Impara dai migliori",
    .duration = 150
});

Anim::saas_card_float(ctx, {
    .cards = {card1, card2, card3},
    .stagger_delay = 4
});

Anim::kinetic_word_stack(ctx, {
    .words = {"BUILD", "CREATE", "INNOVATE"},
    .per_word_delay = 8
});
```

**Dove implementare:**
- `include/chronon3d/presets/animation_presets.hpp` (nuovo)
- `src/presets/animation_presets.cpp` (nuovo)

**Preset minimi:**
| Preset | Descrizione |
|--------|-------------|
| `hero_title_reveal` | Titolo + sottotitolo con tracking reveal e fade |
| `saas_card_float` | 3-4 card che entrano con stagger e float |
| `orbit_showcase` | Oggetti in orbita 3D con camera dollying |
| `soft_button_pop` | Bottone con overshoot e ripple |
| `kinetic_word_stack` | Parole che appaiono con kinetic type |
| `depth_parallax_intro` | Layer a profondità variabile con camera parallax |
| `text_tracking_open` | Testo tracking che si apre con easing |

---

## Fase 5 — Performance Pipeline

### 5a. Box Blur 3-Pass Parallelization

**Stato: ✅ GIÀ IMPLEMENTATO**

`apply_blur()` in `effect_blur.cpp` è già parallelizzato con `tbb::parallel_for` su entrambi i pass (orizzontale su righe, verticale su colonne). Include anche sum sliding window O(1) e prefetch. **Guadagno:** 4-8× su blur con raggio > 50.

### 5b. any_cast Chain → Enum Dispatch

**Problema:** `effect_stack.cpp` ha una catena di `std::any_cast` per ogni effetto.

**Fix:** Aggiungere `EffectType` enum a `EffectInstance` e dispatch con switch:
```cpp
switch (inst.type) {
    case EffectType::Blur: { ... break; }
    case EffectType::Glow: { ... break; }
    // O(1) dispatch invece di O(n) any_cast
}
```

### 5c. RAII Guard Thread-Local Ptrs

**Problema:** `profiling::g_current_*` sono settati/resettati manualmente — rischio dangling pointer se un'eccezione viene lanciata.

**Fix:** `ProfilingGuard` RAII:
```cpp
ProfilingGuard guard(&m_counters, m_framebuffer_pool.get());
// Se eccezione → guard distrutta → restore automatico dei ptr
```

### 5d. Motion Blur Accumulation Parallel + SIMD

Vedi Fase 3 sopra — parallelizzare i sample + SIMD-izzare l'accumulazione con Highway.

---

## Riepilogo Priorità

| Priorità | Cosa | Tempo | Impatto | Complessità |
|----------|------|-------|---------|-------------|
| **🔴 P0** | DropShadow blend invertito (1c) | 5 min | medio | 🟢 bassa |
| **🔴 P0** | Tile seams con effetti (1a) | 2 ore | alto | 🟡 media |
| **🟡 P1** | Glow accumulation clamp (1b) | 10 min | medio | 🟢 bassa |
| **🟡 P1** | Box blur parallel (5a) | 1 giorno | alto | 🟢 bassa |
| **🟡 P1** | any_cast → enum dispatch (5b) | 20 min | basso | 🟢 bassa |
| **🟡 P1** | RAII guard (5c) | 15 min | medio | 🟢 bassa |
| **🟢 P2** | Motion Presets (2c) | 2-3 giorni | alto | 🟡 media |
| **🟢 P2** | Stagger System (2b) | 2 giorni | alto | 🟡 media |
| **🟢 P2** | Timeline/Sequencer (2a) | 3-5 giorni | alto | 🔴 alta |
| **🔵 P3** | Motion Blur reale (Fase 3) | 5-7 giorni | alto | 🔴 alta |
| **✅ ✅** | **Text Animator (2d)** | **3-5 giorni** | **alto** | **🔴 alta** | **✅ Fatto — per-glyph con FontEngine** |
| **⚫ P4** | Scene Presets (Fase 4) | 5-10 giorni | alto | 🟡 media |

---

## Checklist Implementazione

### Subito (questa sessione)

- [ ] Fix DropShadow blend invertito
- [ ] Fix tile seams con effetti (disabilita tile execution quando full-frame da effetti)
- [ ] Fix glow accumulation clamp
- [ ] Box blur parallel (quick win)
- [ ] any_cast → enum dispatch (quick win)
- [ ] RAII guard thread_local ptrs

### Prossima sessione

- [ ] Fix DropShadow contact+ambient merge (1d)
- [ ] Motion Presets (slide_in, pop, settle, float)
- [ ] Stagger System
- [ ] Timeline/Sequencer V1

### Prossimo mese

- [x] **Text Animator per-glyph** — ✅ Fatto con FontEngine + ByGlyph mode
- [ ] Motion Blur multi-sample con accumulazione parallela + SIMD
- [ ] Scene Presets (hero_title_reveal, saas_card_float, ecc.)

---

*Documento generato il 1 Giugno 2026 — basato su analisi del codice al commit `2c7366c`.*
