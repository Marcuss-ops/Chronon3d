# Verdetto — Gap Camera/Animazione vs After Effects

La tua tabella è già parzialmente superata: **Motion Blur, Depth of Field, luci, espressioni e Camera Path Export esistono già**, ma alcuni sono ancora implementazioni semplificate.

Per arrivare vicino ad After Effects **nel solo comparto camera e animazione 2.5D**, Chronon3d è circa al **45–55%**. La base architetturale è buona; ciò che manca davvero è un modello temporale sub-frame universale, una camera fisica e curve spaziali separate dalle curve temporali.

## Stato reale della tua lista

| Funzione                 |                 Stato reale | Completezza | Problema principale                                        |
| ------------------------ | --------------------------: | ----------: | ---------------------------------------------------------- |
| Motion Blur Multi-Sample |   Implementato parzialmente |         60% | Molte animazioni vengono ancora valutate a frame interi    |
| Depth of Field           |   Implementato parzialmente |         55% | CoC non fisico, bokeh e occlusioni incompleti              |
| Sistema luci             |               Base presente |         30% | Una sola ambient e directional, niente point/spot multipli |
| Spatial Bezier Path 3D   |               Quasi assente |         15% | Hai easing temporale, non tangenti spaziali                |
| Advanced Expressions     |                    Parziale |         30% | Motore prevalentemente scalare                             |
| Multi-Camera             |                     Assente |          5% | `Scene` contiene una sola camera                           |
| Camera Presets per lente | Assente come sistema fisico |         20% | Hai FOV e zoom, ma non focal length, sensor e f-stop       |
| Camera Path Export       |            Già implementato |         85% | Mancano sub-frame, quaternioni e formati 3D                |

---

# La priorità zero: tempo continuo sub-frame

Questo è il punto più importante di tutti.

Chronon3d passa già `frame_time` alla composizione e possiede `effective_frame()`. 

Il motion blur esegue già più render per frame:

```cpp
const float t =
    (static_cast<float>(sample) / sample_count) * shutter_duration;

Scene sub_scene = composition.evaluate(frame, t);
```

e accumula i risultati con TBB e Highway. 

Il problema è che `AnimatedCamera2_5D::evaluate()` riceve ancora un `Frame` intero e anche `AnimatedValue<T>` valuta internamente usando frame interi.  

Quindi puoi chiedere otto campioni temporali, ma ottenere otto volte la stessa posizione della camera.

## Correzione architetturale

Introduci un tipo temporale comune:

```cpp
struct SampleTime {
    double frame{0.0};
    double seconds{0.0};
    double fps{30.0};

    static SampleTime from_frame(double frame, double fps) {
        return {
            .frame = frame,
            .seconds = frame / fps,
            .fps = fps
        };
    }
};
```

Poi modifica gradualmente:

```cpp
AnimatedValue<T>::evaluate(SampleTime time);
AnimatedCamera2_5D::evaluate(SampleTime time);
CameraRig::evaluate(SampleTime time, ...);
Composition::evaluate(SampleTime time);
```

Mantieni temporaneamente gli overload precedenti:

```cpp
T evaluate(Frame frame) const {
    return evaluate(SampleTime::from_frame(frame, 30.0));
}
```

`SceneBuilder` non dovrebbe più conservare soltanto:

```cpp
Frame current_frame_;
```

ma:

```cpp
SampleTime current_time_;
```

Le cache devono usare una rappresentazione deterministica del sub-frame:

```cpp
struct TemporalSampleKey {
    Frame frame{0};
    u32 subframe_tick{0};
    EvaluationVersion version{0};
    static constexpr u32 kTicksPerFrame = 65536;
};
```

Non usare direttamente `double` come chiave hash.

## Definition of done

Il test decisivo:

```cpp
camera.position
    .key(0.0, Vec3{0, 0, -1000})
    .key(1.0, Vec3{100, 0, -1000});

render frame 0 con 8 sample;
```

Le otto valutazioni devono produrre otto posizioni differenti.

Senza questo refactor, Motion Blur, expressions temporali, velocity, spatial path e camera export sub-frame resteranno tutti incompleti.

---

# 1. Motion Blur Multi-Sample reale

## Cosa hai già

La pipeline renderizza più sottocampioni della composizione, accumula in floating point e parallelizza il lavoro. È una base corretta e molto più affidabile del solo blur basato su optical flow.  

Hai anche un secondo sistema `VelocityBufferMotionBlur`, che stima il movimento confrontando frame consecutivi mediante block matching.  Questo può rimanere come effetto veloce, ma non deve sostituire il temporal supersampling.

## Cosa manca

### A. Shutter phase

Non basta lo shutter angle. Servono:

```cpp
struct MotionBlurSettings {
    bool enabled{false};
    int samples{8};

    float shutter_angle_deg{180.0f};
    float shutter_phase_deg{-90.0f};

    TemporalSamplePattern pattern{
        TemporalSamplePattern::Stratified
    };

    TemporalFilter filter{
        TemporalFilter::Box
    };
};
```

Con shutter 180° e phase -90°, l'esposizione è centrata sul frame.

Calcolo:

```cpp
double exposure_frames = shutter_angle_deg / 360.0;
double opening_offset = shutter_phase_deg / 360.0;

double sample_time =
    frame +
    opening_offset +
    ((sample_index + 0.5) / sample_count) * exposure_frames;
```

Per otto sample a shutter 180°, renderizzi una finestra temporale lunga mezzo frame.

### B. Pattern di campionamento

Implementa almeno:

```cpp
enum class TemporalSamplePattern {
    Uniform,
    Stratified,
    Halton
};
```

Per output deterministico, la sequenza deve dipendere da:

```text
composition_id
frame
sample_index
fixed_seed
```

La modalità consigliata:

```cpp
double u = (sample_index + deterministic_jitter) / sample_count;
```

### C. Pesi di ricostruzione

Inizia con box filter:

```cpp
weight = 1.0 / samples;
```

Successivamente aggiungi:

* Triangle
* Gaussian
* Custom shutter curve

```cpp
enum class TemporalFilter {
    Box,
    Triangle,
    Gaussian
};
```

### D. Accumulazione alpha corretta

Accumula sempre colori premoltiplicati:

```cpp
accum_rgb += sample.rgb * sample.a * weight;
accum_a   += sample.a * weight;
```

Se il framebuffer interno è già premultiplied, documenta il contratto e aggiungi test.

### E. Cache sub-frame

La cache di un nodo deve includere:

```text
frame integer
subframe tick
shutter sample
animation dependency version
```

Un nodo statico può essere riutilizzato tra tutti i sample. Un nodo animato no.

### F. Dirty rectangles

Durante il motion blur, il dirty rectangle deve essere l'unione dell'area occupata dal layer lungo tutta l'esposizione:

```cpp
BBox exposure_bbox;

for each temporal sample:
    exposure_bbox = union(exposure_bbox, projected_layer_bbox(sample_time));
```

Altrimenti gli oggetti veloci possono essere tagliati.

## API finale consigliata

```cpp
s.camera()
 .motion_blur({
     .enabled = true,
     .samples = 12,
     .shutter_angle_deg = 180.0f,
     .shutter_phase_deg = -90.0f,
     .pattern = TemporalSamplePattern::Stratified,
     .filter = TemporalFilter::Triangle
 });
```

## Test obbligatori

1. Una camera statica produce pixel identici con 1 e 16 sample.
2. Un movimento lineare produce una scia simmetrica con phase centrata.
3. Una rotazione produce blur anche senza traslazione.
4. Layer statici vengono renderizzati una sola volta.
5. Un oggetto veloce non viene tagliato dai dirty rect.
6. Il risultato è deterministico tra due esecuzioni.

---

# 2. Depth of Field fisicamente credibile

## Cosa hai già

Hai due livelli di implementazione:

* blur per intero layer usando il suo `world_z`; 
* blur per pixel usando un depth buffer composto. 

Il secondo è già la strada corretta. Il kernel è separabile, parallelizzato e accelerato con SIMD. 

## Problema attuale

Il raggio viene calcolato così:

```cpp
blur =
    abs(layer_world_z - focus_z) *
    aperture;
```


Questo non rappresenta una lente. Inoltre usa `world_z`, mentre dovrebbe usare la distanza lungo l'asse ottico della camera, cioè **view-space depth**.

## Nuovo modello camera fisica

```cpp
struct PhysicalLens {
    float focal_length_mm{50.0f};
    float sensor_width_mm{36.0f};
    float sensor_height_mm{20.25f};

    float focus_distance{1000.0f};
    float f_stop{2.8f};

    int iris_blades{8};
    float iris_rotation_deg{0.0f};
    float iris_roundness{1.0f};
    float anamorphic_squeeze{1.0f};

    float near_clip{1.0f};
    float far_clip{100000.0f};
};
```

L'apertura reale è:

```cpp
float aperture_diameter_mm =
    focal_length_mm / f_stop;
```

L'angolo di campo orizzontale:

```cpp
float fov_x =
    2.0f * atan(
        sensor_width_mm /
        (2.0f * focal_length_mm)
    );
```

I preset di camera di After Effects sono legati alla lunghezza focale e impostano proprietà come angolo di campo, zoom, distanza di fuoco, focal length e aperture. ([Adobe Aiuto][1])

## Circle of Confusion

Con modello thin-lens:

```cpp
float image_distance(float focal, float object_distance) {
    return focal * object_distance /
           (object_distance - focal);
}

float compute_coc_sensor_mm(
    float object_distance,
    float focus_distance,
    float focal_length,
    float f_stop
) {
    float aperture = focal_length / f_stop;

    float v  = image_distance(focal_length, object_distance);
    float vf = image_distance(focal_length, focus_distance);

    return aperture * std::abs(v - vf) / std::abs(v);
}
```

Conversione in pixel:

```cpp
float coc_px =
    coc_sensor_mm /
    sensor_width_mm *
    viewport_width;
```

Bisogna conservare anche il segno:

```cpp
signed_coc < 0  // foreground
signed_coc > 0  // background
```

## Pipeline corretta

### Pass 1: depth

Salva per ogni pixel:

```text
linear view-space depth
coverage
layer/material id opzionale
```

Non salvare semplicemente il `world_z` del layer.

### Pass 2: Circle of Confusion

Produci una texture/buffer float:

```cpp
struct CocPixel {
    float near_coc;
    float far_coc;
};
```

### Pass 3: downsample

Crea buffer a metà risoluzione:

* colore premoltiplicato;
* depth minima;
* near CoC massima;
* far CoC massima.

### Pass 4: bokeh gather

Per ogni pixel, campiona un disco o poligono dell'iride:

```cpp
for sample in iris_kernel:
    source = uv + sample.offset * coc_radius;
    reject_or_weight_using_depth();
```

Il tuo blur separabile attuale è veloce, ma non produce un bokeh realistico e può fare attraversare il colore dello sfondo sopra il foreground.

### Pass 5: near-field dilation

Il foreground sfocato deve espandersi sui bordi. Crea una maschera near:

```cpp
near_mask = dilate(max(-signed_coc, 0));
```

Poi sfoca foreground e background separatamente.

### Pass 6: compositing

Ordine:

```text
sharp color
far blur dietro
sharp in-focus
near blur davanti
highlights/bokeh
```

After Effects Advanced 3D gestisce anche focus distance, aperture, focus range, near/far blur separati e trasparenze sovrapposte; è proprio la parte che distingue un semplice Gaussian blur da un DoF credibile. ([Adobe Aiuto][2])

## Trasparenze

Un singolo depth value non basta quando più layer semitrasparenti occupano lo stesso pixel.

Soluzioni progressive:

1. **V1:** depth del contributo alpha dominante.
2. **V2:** due depth layer, foreground e background.
3. **V3:** K-buffer con 4 contributi ordinati per profondità.

Per Chronon3d CPU-first, inizierei con un K-buffer opzionale da 2 livelli.

---

# 3. Sistema luci completo

## Cosa hai adesso

`LightContext` contiene principalmente:

* ambient;
* una directional;
* Lambert diffuse;
* specular;
* rim approssimato.

 

`LightingRig` è un ottimo sistema di preset, ma rappresenta una configurazione globale con una key light principale, non una scena con più luci indipendenti. 

## Modello dati

Non allargare `LightContext` con decine di campi. Introduci un sistema comune:

```cpp
enum class LightType {
    Ambient,
    Directional,
    Point,
    Spot
};

enum class LightFalloff {
    None,
    Smooth,
    InverseSquareClamped
};

struct Light {
    LightId id;
    std::string name;
    LightType type;

    bool enabled{true};
    bool casts_shadows{false};

    Color color{1, 1, 1, 1};
    float intensity{1.0f};

    Vec3 position{0, 0, 0};
    Vec3 direction{0, 0, 1};

    float range{1000.0f};

    float inner_cone_deg{30.0f};
    float outer_cone_deg{45.0f};

    LightFalloff falloff{
        LightFalloff::InverseSquareClamped
    };

    ShadowSettings shadows;
};
```

La scena deve contenere:

```cpp
std::pmr::vector<Light> lights;
```

e non una sola luce globale. After Effects distingue Spot, Parallel, Point e Ambient, con proprietà di intensità, cone angle, feather, falloff e shadow casting. ([Adobe Aiuto][1])

## Registry

Usa la tua regola architetturale:

```cpp
LightTypeRegistry
LightEvaluatorRegistry
ShadowTechniqueRegistry
```

Evita un enorme `switch` dentro il software renderer.

## Equazioni

### Directional

```cpp
L = normalize(-light.direction);
attenuation = 1;
```

### Point

```cpp
Vec3 delta = light.position - world_position;
float distance = length(delta);
Vec3 L = delta / distance;
```

Falloff:

```cpp
float attenuation =
    1.0f /
    max(distance * distance, minimum_distance_squared);
```

Aggiungi un cutoff morbido:

```cpp
float range_factor =
    smoothstep(light.range, light.range * 0.8f, distance);
```

### Spot

Parti dal point light e moltiplica:

```cpp
float angle_cos =
    dot(-L, normalize(light.direction));

float cone =
    smoothstep(
        cos(outer_angle),
        cos(inner_angle),
        angle_cos
    );
```

### Ambient

```cpp
lighting += material.base_color *
            light.color *
            light.intensity;
```

## Materiali

Amplia `Material2_5D`:

```cpp
struct Material2_5D {
    Color base_color;

    float ambient{1.0f};
    float diffuse{1.0f};
    float specular{0.25f};
    float roughness{0.5f};
    float metallic{0.0f};
    float shininess{32.0f};

    bool accepts_lights{true};
    bool accepts_shadows{true};
    bool casts_shadows{true};
};
```

Per restare simile ad AE Classic 3D, Lambert + Blinn-Phong è sufficiente inizialmente. Non serve introdurre subito un PBR completo.

## Prestazioni CPU

Non ciclare tutte le luci su ogni pixel senza controllo.

Fasi:

1. massimo 8 luci nella prima versione;
2. calcolo per-layer per forme piane;
3. tile light lists per mesh;
4. bounding sphere/range culling;
5. SIMD su quattro o otto pixel;
6. cache del contributo delle luci statiche.

## Ombre

Separale dal sistema luci.

### V1

* directional shadow proiettata sul piano;
* point/spot shadow 2.5D approssimata;
* contact shadow.

### V2

* shadow buffer a risoluzione ridotta;
* occluder list per luce;
* soft shadow con sample deterministici.

Le vere ombre geometriche con mesh complesse sono un progetto distinto.

---

# 4. Spatial Bezier Path 3D

## Cosa manca realmente

Attualmente ogni keyframe possiede:

```cpp
Frame frame;
T value;
EasingCurve easing;
bool roving;
```


L'interpolazione è:

```cpp
value =
    start +
    (end - start) *
    temporal_easing(t);
```


Quindi la posizione segue una linea retta nello spazio, anche quando il tempo segue una curva Bezier.

After Effects separa esplicitamente **interpolazione temporale** e **interpolazione spaziale**, con Linear, Bezier, Continuous Bezier, Auto Bezier e roving keyframes. ([Adobe Aiuto][3])

## Nuovo modello keyframe

```cpp
enum class TemporalInterpolation {
    Hold,
    Linear,
    Bezier
};

enum class SpatialInterpolation {
    Linear,
    Bezier,
    ContinuousBezier,
    AutoBezier
};

template<typename T>
struct Keyframe {
    double time_frame;
    T value;

    TemporalCurve temporal;

    SpatialInterpolation spatial{
        SpatialInterpolation::Linear
    };

    T spatial_in_tangent{};
    T spatial_out_tangent{};

    bool roving{false};
};
```

Non usare `EasingCurve` per rappresentare anche il percorso spaziale.

## Cubic Bezier 3D

Per due keyframe:

```text
P0 = valore iniziale
P1 = P0 + out_tangent iniziale
P2 = P3 + in_tangent finale
P3 = valore finale
```

Valutazione:

```cpp
Vec3 cubic_bezier(
    Vec3 p0,
    Vec3 p1,
    Vec3 p2,
    Vec3 p3,
    float u
) {
    float a = 1.0f - u;

    return
        a*a*a * p0 +
        3*a*a*u * p1 +
        3*a*u*u * p2 +
        u*u*u * p3;
}
```

Derivata, necessaria per direzione e velocità:

```cpp
Vec3 cubic_bezier_derivative(...) {
    float a = 1.0f - u;

    return
        3*a*a * (p1 - p0) +
        6*a*u * (p2 - p1) +
        3*u*u * (p3 - p2);
}
```

## Separare tempo e spazio

```cpp
float temporal_u =
    evaluate_temporal_curve(local_time);

Vec3 position =
    evaluate_spatial_curve(temporal_u);
```

In questo modo l'utente può avere:

* percorso curvo;
* accelerazione lenta all'inizio;
* accelerazione veloce alla fine.

## Velocità costante e arc-length

Il parametro matematico `u` non produce velocità spaziale costante.

Per ogni segmento crea una LUT:

```cpp
struct ArcLengthSample {
    float u;
    float cumulative_length;
};
```

Con 32–128 sample per segmento:

```cpp
for u from 0 to 1:
    cumulative += distance(P(u), P(previous_u));
```

Durante la valutazione:

1. calcola la distanza desiderata;
2. cerca nella LUT;
3. interpola il relativo `u`;
4. opzionalmente fai un passaggio Newton.

Questo serve anche per i veri roving keyframe. L'implementazione attuale distribuisce i `Vec3` roving in maniera uniforme nel tempo, non in base alla distanza della curva. 

## Rotazione camera

Non interpolare Euler XYZ per movimenti complessi.

Usa:

```cpp
Quat orientation;
Quat slerp(q0, q1, t);
```

Per curve con più chiavi:

```cpp
squad(q0, control0, control1, q1, t);
```

Per two-node camera:

```cpp
forward = normalize(target - position);
orientation = look_rotation(forward, up);
```

Poi applica il roll lungo l'asse forward.

## Auto orientation

Aggiungi:

```cpp
enum class CameraOrientationMode {
    Free,
    LookAtTarget,
    OrientAlongPath
};
```

`OrientAlongPath` usa la derivata della curva.

---

# 5. Advanced Expressions

## Stato attuale

Hai già:

* `time`, `frame`, `fps`, `index`;
* `thisComp`, `thisLayer`, `thisProperty`;
* riferimenti ad altri layer;
* random, seedRandom, posterizeTime, wiggle;
* diverse funzioni matematiche.

 

Ma l'esecuzione dentro `AnimatedValue` avviene soltanto per `f32`. Per `Vec2`, `Vec3`, colori e altri tipi viene restituito il valore base. 

Inoltre metodi fondamentali come:

```javascript
layer("Target").toComp(...)
```

sono riconosciuti sintatticamente, ma attualmente saltati e restituiscono zero. 

## Non estendere ulteriormente il parser `double`

Serve un motore tipizzato.

```cpp
using ExpressionValue = std::variant<
    std::monostate,
    double,
    bool,
    std::string,
    Vec2,
    Vec3,
    Vec4,
    Color,
    ExpressionArray,
    LayerReference,
    PropertyReference,
    CompositionReference
>;
```

## Pipeline

```text
Expression source
→ Lexer
→ AST
→ Type checking
→ Bytecode
→ VM evaluation
```

Compila l'espressione una volta:

```cpp
CompiledExpression compile(std::string_view source);
```

Poi valutala molte volte:

```cpp
ExpressionValue evaluate(
    const CompiledExpression& expression,
    const ExpressionContext& context
);
```

## Dependency graph

Ogni expression property deve dichiarare le sue dipendenze:

```text
Camera.Position
 ├─ Target.Position
 └─ Controller.Slider
```

Costruisci:

```cpp
PropertyDependencyGraph
```

e usa topological sort.

Se appare:

```text
A dipende da B
B dipende da A
```

genera un errore di ciclo chiaro, non una ricorsione infinita.

## Funzioni prioritarie per le camere

Implementerei in quest'ordine:

```javascript
valueAtTime(t)
velocityAtTime(t)
speedAtTime(t)

toComp(point)
fromComp(point)
toWorld(point)
fromWorld(point)

lookAt(from, to)

length(v)
normalize(v)
dot(a, b)
cross(a, b)

linear(...)
ease(...)
easeIn(...)
easeOut(...)

wiggle(freq, amp, octaves, ampMult, time)
smooth(width, samples)

loopIn(...)
loopOut(...)
posterizeTime(...)
```

After Effects espone funzioni come `valueAtTime`, `velocityAtTime`, `speedAtTime`, `wiggle`, trasformazioni coordinate come `toComp`, e metodi vettoriali come `lookAt`. ([Adobe Aiuto][4])

## Sampling temporale

`valueAtTime()` deve accettare tempo continuo, non frame intero:

```cpp
property.evaluate(
    SampleTime{
        .seconds = requested_time,
        .frame = requested_time * fps
    }
);
```

Aggiungi un limite di ricorsione e memoizzazione:

```cpp
CacheKey {
    property_id,
    sample_time_ticks,
    dependency_version
};
```

---

# 6. Multi-Camera System

## Situazione attuale

`Scene` contiene un solo:

```cpp
Camera2_5DRuntime m_camera_2_5d;
```

 

## Modello consigliato

```cpp
using CameraId = uint32_t;

struct CameraLayer {
    CameraId id;
    std::string name;

    AnimatedCamera2_5D camera;

    double in_frame{0.0};
    double out_frame{0.0};

    int stack_order{0};
    bool enabled{true};
};
```

La scena:

```cpp
std::pmr::vector<CameraLayer> cameras;
```

## Active camera resolver

```cpp
class ActiveCameraResolver {
public:
    ResolvedCamera resolve(
        std::span<const CameraLayer> cameras,
        SampleTime time
    ) const;
};
```

Regola AE-like:

1. seleziona camere abilitate e attive nel tempo;
2. ordina per stack order;
3. usa quella più in alto;
4. se nessuna è attiva, usa la camera di default.

After Effects usa come camera attiva la camera attiva più alta nella timeline, ed è questa che viene usata per l'output finale. ([Adobe Aiuto][1])

## Camera cuts

Un taglio deve essere hard:

```cpp
struct CameraCut {
    double frame;
    CameraId camera;
};
```

Non interpolare automaticamente da una camera all'altra.

Le transizioni devono essere esplicite:

```cpp
struct CameraBlend {
    CameraId from;
    CameraId to;
    double start_frame;
    double duration;
    EasingCurve curve;
};
```

Durante un blend:

* posizione: spatial interpolation;
* orientamento: slerp;
* focal length: interpolazione logaritmica o lineare configurabile;
* focus distance: interpolazione lineare;
* f-stop: preferibilmente interpolazione in exposure stops.

## Caching

La chiave deve contenere:

```text
active_camera_id
camera_cut_version
camera_sample_time
lens fingerprint
```

Un cambio camera deve invalidare:

* projection matrix;
* depth buffer;
* dirty region;
* scene fingerprint;
* graph payload relativo alla camera.

---

# 7. Camera Presets per lente

Hai già preset di movimento come:

* hero push-in;
* orbit;
* parallax pan;
* dolly zoom;
* focus pull;
* low-angle reveal.

 

Questi però sono **motion presets**, non lens presets.

## LensPresetRegistry

```cpp
struct LensPreset {
    std::string id;
    std::string display_name;

    float focal_length_mm;
    float sensor_width_mm;
    float sensor_height_mm;
    float default_f_stop;
    float default_focus_distance;
};

class LensPresetRegistry {
public:
    void register_preset(LensPreset preset);
    const LensPreset* find(std::string_view id) const;
};
```

Preset iniziali:

```text
15mm Ultra Wide
20mm Wide
24mm Wide
28mm
35mm
50mm Standard
85mm Portrait
135mm Telephoto
200mm Telephoto
```

## Applicazione

```cpp
s.camera()
 .lens_preset("85mm")
 .focus_distance(1200.0f)
 .f_stop(2.0f);
```

Il preset deve cambiare lente/FOV, **non la posizione della camera**.

## Compatibilità con `zoom`

Mantieni:

```cpp
camera.zoom
```

come campo derivato o legacy.

```cpp
zoom_px =
    focal_length_mm *
    viewport_width /
    sensor_width_mm;
```

Definisci chiaramente se fai fit:

* horizontal;
* vertical;
* diagonal;
* fill.

```cpp
enum class SensorFit {
    Horizontal,
    Vertical,
    Fill,
    Overscan
};
```

---

# 8. Camera Path Export

Questa voce è quasi completata.

Il comando attuale esporta JSON o CSV con:

* posizione;
* rotazione;
* zoom;
* FOV;
* target;
* velocità;
* distanza cumulativa.

 

## Ciò che manca

Aggiungi:

```json
{
  "schema_version": 2,
  "fps": 30,
  "units": "chronon_world_units",
  "coordinate_system": "left_handed_y_down",
  "camera": "MainCamera",
  "sample_time": 12.375,
  "position": [],
  "orientation_quaternion": [],
  "target": [],
  "focal_length_mm": 50,
  "sensor_width_mm": 36,
  "focus_distance": 1000,
  "f_stop": 2.8,
  "shutter_angle_deg": 180,
  "shutter_phase_deg": -90
}
```

Supporta:

```bash
chronon3d_cli camera-path MyComp \
  --start 0 \
  --end 120 \
  --samples-per-frame 4 \
  --format json \
  -o camera.json
```

Formati successivi:

* glTF camera animation;
* FBX, tramite libreria dedicata;
* Alembic;
* Blender Python import script.

Per l'export usa quaternioni e non soltanto Euler, altrimenti rischi discontinuità 359° → 0°.

---

# Funzioni importanti non presenti nella tua lista

Per avvicinarti realmente all'esperienza camera di After Effects mancano anche queste.

## Camera shake professionale

Non soltanto `wiggle`.

```cpp
struct CameraShake {
    float translation_amplitude;
    float rotation_amplitude;
    float frequency;
    float roughness;
    int octaves;
    uint32_t seed;
    float fade_in;
    float fade_out;
};
```

Deve essere applicato come rig secondario sopra il movimento principale.

## Constraints

```cpp
LookAtConstraint
AimConstraint
ParentConstraint
PositionConstraint
OrientationConstraint
PathConstraint
```

Con peso animabile:

```cpp
constraint.weight = AnimatedValue<float>{1.0f};
```

## Separate dimensions

Per posizione e orientamento:

```cpp
position_x
position_y
position_z
```

devono poter avere keyframe ed expression separati.

## Camera clipping

Aggiungi near e far clipping reali, con clipping di segmenti e poligoni che attraversano il near plane.

## Overscan e gate

Servono per:

* render più grande della composizione;
* stabilizzazione;
* motion blur ai bordi;
* post crop.

## Target/up-vector stabile

`lookAt` con un up vector fisso può generare flip vicino ai poli.

Usa:

* previous-frame orientation;
* parallel transport;
* fallback axis;
* quaternion continuity.

## Focus target

Non usare solo `target.z`.

La distanza di fuoco deve essere:

```cpp
focus_distance =
    dot(
        target_world - camera_position,
        camera_forward
    );
```

Nel codice corrente il rig usa lo Z del target per il fuoco.  Questo funziona soltanto in casi molto semplici.

---

# Ordine corretto di sviluppo

## Fase 0 — Fondazione temporale

**Durata indicativa: 1–2 settimane**

* `SampleTime`;
* `AnimatedValue` sub-frame;
* camera e rig sub-frame;
* chiavi cache temporali;
* test motion blur temporali.

Questa fase sblocca tutto il resto.

## Fase 1 — Spatial Bezier 3D

**Durata: 2–3 settimane**

* temporal/spatial curve separate;
* tangenti in/out;
* arc-length LUT;
* roving su distanza;
* quaternion orientation;
* orient along path.

## Fase 2 — Motion Blur definitivo

**Durata: 1–2 settimane**

* shutter phase;
* pattern stratificato;
* cache per sottocampione;
* exposure bbox;
* alpha premoltiplicato;
* test deterministici.

## Fase 3 — Physical Camera e DoF

**Durata: 3–5 settimane**

* physical lens;
* view-space depth;
* thin-lens CoC;
* near/far buffers;
* depth-aware bokeh;
* gestione foreground;
* lens presets.

## Fase 4 — Lighting system

**Durata: 3–5 settimane**

* light list;
* point/spot/directional/ambient;
* animazione luci;
* falloff;
* material response;
* basic shadows;
* tile culling.

## Fase 5 — Expression VM

**Durata: 4–8 settimane**

* valori tipizzati;
* AST/bytecode;
* vector expressions;
* property dependency graph;
* coordinate transforms;
* value/velocity at time;
* cycle detection.

## Fase 6 — Multi-camera

**Durata: 1–2 settimane**

* camera registry;
* active camera resolver;
* cuts;
* optional blends;
* fingerprint/cache integration.

## Fase 7 — Export avanzato

**Durata: alcuni giorni–2 settimane**

* sub-frame JSON/CSV;
* quaternion;
* physical lens metadata;
* glTF;
* Blender importer.

Queste stime sono realistiche per **uno sviluppatore C++ esperto che lavora in modo concentrato**, con PR piccole e test mirati.

---

# Roadmap finale aggiornata

| Priorità | Blocco                           | Stato                     |
| -------- | -------------------------------- | ------------------------- |
| P0       | Continuous Sample Time           | Da fare immediatamente    |
| P0       | Spatial/Temporal Curves separate | Fondamentale              |
| P1       | Motion Blur con shutter phase    | Completamento             |
| P1       | Physical Lens Model              | Fondamentale              |
| P1       | Physical per-pixel DoF           | Evoluzione dell'esistente |
| P1       | Quaternion Camera Orientation    | Fondamentale              |
| P2       | Typed Multiple Lights            | Evoluzione dell'esistente |
| P2       | Multi-Camera e Camera Cuts       | Nuovo                     |
| P2       | Typed Expression VM              | Grande refactor           |
| P3       | Lens Preset Registry             | Facile dopo Physical Lens |
| P3       | Camera Path Export V2            | Estensione dell'esistente |

La scelta più importante è **non implementare queste otto funzioni come patch indipendenti**. Il nucleo comune deve essere:

```text
SampleTime
→ AnimatedProperty
→ SpatialCurve
→ CameraRig
→ PhysicalCamera
→ ActiveCameraResolver
→ RenderResources: color/depth/velocity
```

Una volta costruito questo asse, Motion Blur, DoF, Multi-Camera, Expressions e Camera Export useranno tutti lo stesso tempo, lo stesso modello camera e gli stessi resolver. È questo che farà sembrare Chronon3d un motore coerente, invece di una raccolta di effetti simili ad After Effects.

[1]: https://helpx.adobe.com/after-effects/using/cameras-lights-points-interest.html "Cameras, lights, and points of interest in After Effects | After Effects"
[2]: https://helpx.adobe.com/after-effects/using/enable-in_engine-depth-of-field-in-advanced-3d.html "Enable in‑engine Depth of Field in Advanced 3D | After Effects"
[3]: https://helpx.adobe.com/after-effects/using/keyframe-interpolation.html "Keyframe interpolation in After Effects | After Effects"
[4]: https://helpx.adobe.com/after-effects/using/expression-language-reference.html "Expression language in After Effects | After Effects"
