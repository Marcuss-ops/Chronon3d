# Anti-Duplication Rules — Chronon3D Architectural Constitution

> **Status:** Binding. Every PR must pass the anti-duplication checklist.
> **Last updated:** 2026-06-19
>
> Queste regole sono vincolanti, non consigli. Impediscono che Chronon3D accumuli
> doppie implementazioni, registry paralleli, API sovrapposte e logica consumer
> nel motore.

---

## Source of Truth — One per Domain

| Dominio            | Unica fonte               | Note                                  |
| ------------------ | ------------------------- | ------------------------------------- |
| Composizioni       | `CompositionRegistry`     | Nessun altro registro composizioni    |
| Moduli             | `ExtensionCatalog`        | Tutti i plugin passano da qui         |
| Contesto plugin    | `ExtensionContext`        | Passato a ogni ExtensionModule        |
| Props              | `CompositionProps`        | PR 3 — in arrivo                      |
| Asset              | `AssetResolver`           | PR 3 — in arrivo                      |
| Rendering pubblico | `RenderEngine`            | PR 3 — facciata unica                 |
| Effetti            | `EffectCatalog`           | Registro canonico effetti             |
| Nodi               | `GraphNodeCatalog`        | Registro canonico render graph nodes  |
| Sampler            | `SamplerRegistry`         | `src/registry/sampler_registry.cpp`   |
| Shape              | `ShapeRegistry`           | `src/registry/shape_registry.cpp`     |
| Source             | `SourceRegistry`          | `src/registry/source_registry.cpp`    |
| Cache generiche    | `LruCache`                | `src/cache/` — no cache ad-hoc       |
| Errori pubblici    | `Result<T, ChrononError>` | Futuro — oggi eccezioni               |
| Config motore      | `EngineConfig`            | Futuro — oggi flag sparsi             |
| Job rendering      | `RenderRequest`           | Futuro — oggi args CLI                |

Qualunque nuovo tipo che sovrappone uno di questi concetti deve essere rifiutato.

---

## Gate Obbligatorio — Prima di Scrivere Codice

Prima di aggiungere una feature, rispondere:

```text
1. Quale registry esistente utilizza?
2. Quale resolver esistente utilizza?
3. Quale sampler esistente utilizza?
4. Quale cache comune utilizza?
5. Quale API pubblica espone?
6. Sta introducendo un secondo percorso per qualcosa?
7. Può vivere interamente nel composition pack esterno?
8. Richiede davvero modifiche a Chronon?
```

- Se la risposta al punto **6** è **sì** → **TASK BLOCCATO**
- Se la risposta al punto **7** è **sì** → **IMPLEMENTARE FUORI DA CHRONON**

---

## Regole

### 1. Un solo percorso di registrazione composizioni

```
CompositionModule → ExtensionContext → CompositionRegistry
```

**Vietato:**
- `CHRONON_REGISTER_COMPOSITION(...)` (macro legacy — rimossa)
- `builtin_composition_entries().push_back(...)` (statico globale — rimosso PR 2)
- Qualsiasi altro meccanismo di registrazione composizioni

**Consentito solo:** `context.compositions.add("Name", factory);`

### 2. Un registro per dominio

Non devono esistere contemporaneamente registri sovrapposti per lo stesso dominio.

Esempio vietato: `CompositionRegistry` + `BuiltinCompositionRegistry` + `ContentRegistry` + `CompositionCatalog`

Deve esistere un solo `CompositionRegistry`.

### 3. Nessuna registrazione globale statica

**Vietato:**
```cpp
static std::vector<Factory> factories;
static Registry& instance();
GlobalCompositionRegistry::get();
__attribute__((constructor))
AutoRegisterComposition registration(...);
```

**Consentito:**
```cpp
CompositionRegistry compositions;
ExtensionCatalog extensions;
ExtensionContext context{.compositions = compositions, ...};
```

Tutti i registry devono essere creati dall'host e passati esplicitamente.

### 4. Una sola API di rendering pubblica

Non devono esistere: `RenderEngine`, `Renderer`, `RenderService`, `VideoRenderer`, `CompositionRenderer`, `RenderPipelineRunner` contemporaneamente.

Deve esistere una facciata pubblica unica (`RenderEngine`) con operazioni chiare:
```cpp
render_frame(...)
render_sequence(...)
render_video(...)
```

### 5. La CLI non duplica la pipeline di rendering

La CLI non implementa build scene, compile graph, execute graph, encode video, gestire cache, gestire asset.

La CLI traduce argomenti in `RenderRequest` e chiama `RenderEngine`.

### 6. Un solo sistema di props

Non creare: `CompositionProps`, `RenderProps`, `SceneVariables`, `TemplateData`, `DynamicValues`, `JsonProps`, `InputParameters` in parallelo.

Deve esistere un contratto canonico: `CompositionProps`.

JSON, Python dict, payload HTTP devono convertirsi in quel tipo.

### 7. Nessun JSON dentro le composizioni

**Vietato:**
```cpp
auto json = load_json("job.json");  // dentro una composizione
```

**Corretto:**
```cpp
Composition make_video(const CompositionProps& props) {
    auto title = props.require_string("title");
}
```

Le composizioni non conoscono JSON, CLI, HTTP, database o code di lavoro.

### 8. Un solo AssetResolver

Non creare funzioni sparse: `find_font()`, `resolve_font_path()`, `locate_asset()`, `search_image()`, `get_project_asset()`.

Usare `AssetResolver` con metodi canonici: `resolve_font(id)`, `resolve_image(id)`, etc.

**Vietato** path hardcoded: `"../../Chronon3d/assets/font.ttf"`, `std::filesystem::current_path() / "assets"`, `getenv("HOME") + "/my_assets"`.

### 9. Nessuna cache locale ad-hoc

**Vietato:**
```cpp
static std::unordered_map<Key, Value> cache;
MyCustomImageCache, MyTextCache, CompositionLocalLru
```

senza prima verificare il sistema esistente.

Usare `LruCache<Key, Value, Policy>` del modulo `src/cache/`.

Ogni cache nuova deve dichiarare: chiave, ownership, limite, policy di invalidazione, thread safety, telemetry, criterio di eliminazione.

### 10. Nessuna reimplementazione di interpolazioni/animazioni

**Vietato:**
```cpp
float custom_lerp(float a, float b, float t);  // dentro una composizione
```

Usare: `chronon3d::interpolate(...)`, `chronon3d::spring(...)`, `chronon3d::evaluate_track(...)`.

### 11. Nessuna reimplementazione di blend ed effetti

Una composizione non implementa manualmente: overlay, multiply, screen, blur, glow, shadow, color correction, alpha composition.

Usare: `layer.blend_mode(BlendMode::Overlay)`, `layer.glow(...)`, o registrare l'effetto in `EffectCatalog`.

### 12. Nessuna condizione specifica del progetto nel core

**Vietato nel repository Chronon:**
```cpp
if (project_name == "PipelineGen")
if (channel_type == "WWE")
if (template == "NewsIntro")
if (customer_id == ...)
```

Chronon non conosce nomi di programmi, clienti, canali, brand o composizioni esterne.

### 13. Nessuna modifica a SceneBuilder per una composizione

**Vietato:** `SceneBuilder::make_pipelinegen_intro(...)`

**Corretto:** componente esterno, funzione helper nel composition pack, o modulo generico se veramente riutilizzabile.

### 14. Nessuna modifica al GraphExecutor per una feature

Prima di toccare GraphExecutor, RenderGraph, Scene core, Framebuffer, cache evaluator, node runner, compilare:

```text
Capacità generica mancante:
Perché non può essere implementata come nodo:
Perché non può essere implementata come effetto:
Perché non può essere implementata come extension:
Consumer che ne beneficiano:
Test di compatibilità:
```

### 15. Un solo extension system

Non creare: `PluginManager`, `ModuleRegistry`, `PackLoader`, `AddonSystem`, `CompositionPluginRegistry` in parallelo a `ExtensionModule` / `ExtensionCatalog` / `ExtensionContext`.

Anche i futuri plugin dinamici devono essere adapter verso `ExtensionModule`.

### 16. Solo target CMake pubblici dal consumer

Il consumer non linka: `chronon3d_graph`, `chronon3d_scene`, `chronon3d_runtime`, etc.

Deve linkare solo: `Chronon3D::SDK`.

Eventuali target pubblici aggiuntivi (`Chronon3D::Core`, `Chronon3D::Text`, `Chronon3D::Video`) solo se definiti come API pubbliche.

### 17. Solo header pubblici dal consumer

**Vietato:**
```cpp
#include "../../Chronon3d/src/render_graph/internal.hpp"
#include <chronon3d/detail/...>
#include <src/runtime/...>
```

**Consentito:**
```cpp
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/extension/extension_context.hpp>
```

### 18. Un solo modello di errori pubblici

Per l'SDK pubblico: `Result<T, ChrononError>`.

Le eccezioni rimangono per errori di programmazione. Errori di input, asset, composizione e rendering → `Result`.

### 19. Nessuna duplicazione di configurazioni

Non creare: `RenderConfig`, `EngineConfig`, `RendererOptions`, `RenderSettings`, `PipelineOptions`, `VideoOptions` con campi sovrapposti.

Struttura:
- `EngineConfig`: configurazione lunga vita del motore
- `RenderRequest`: singolo lavoro
- `VideoOutputOptions`: encoding/output

### 20. Nessuna composizione esterna nel repository Chronon

**Vietato:** `Chronon3d/content/pipelinegen/`, `Chronon3d/content/news/`, `Chronon3d/content/customer_x/`.

In Chronon possono restare soltanto: `hello_world`, `minimal_smoke_test`, `SDK example`, `diagnostic composition`.

---

## Checklist PR Anti-Duplicazione

Ogni PR deve includere:

```markdown
## Anti-duplication check

- [ ] Ho cercato implementazioni esistenti prima di aggiungere codice
- [ ] Non ho creato un nuovo registry
- [ ] Non ho creato un nuovo resolver
- [ ] Non ho creato un nuovo sampler
- [ ] Non ho creato una nuova cache
- [ ] Non ho creato una seconda API di rendering
- [ ] Non ho aggiunto stato globale statico
- [ ] Non ho aggiunto logica CLI nel core
- [ ] Non ho aggiunto logica consumer nel motore
- [ ] La feature usa ExtensionContext
- [ ] La composizione vive fuori da Chronon
- [ ] Il consumer include soltanto header pubblici
- [ ] Il consumer collega soltanto target CMake pubblici
```

---

## Regola Principale

> **Prima si estende una struttura comune. Non si crea mai una struttura parallela.**

E la regola più severa:

> **Quando una capacità può vivere nel composition pack, è vietato aggiungerla al core di Chronon.**
