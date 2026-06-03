# Camera da Regia After Effects-like

Obiettivo: trasformare la camera di Chronon3d da camera tecnica a camera da regia, con comportamento più vicino ad After Effects:

- camera parentabile a un null controller
- target separato dalla camera
- anchor point reale per transform e pivot
- rig cinematografico con orbit, track, dolly, pan, tilt, roll, zoom
- keyframe graph con easing e curve più espressive
- DOF e motion blur guidati dalla camera
- debug e validation per controllare il path

Questo documento definisce il lavoro in ordine, con milestone piccole e verificabili.

## Stato attuale

Già presente:

- `Camera2_5D` con `position`, `rotation`, `zoom`, `fov_deg`, `point_of_interest`, `parent_name`, `target_name`, `dof`
- preset camera motion: `orbit`, `dolly`, `pan`, `tilt_roll`, `push_in_tilt`, `dolly_in`, `parallax_sweep`, `orbit_small`, `dramatic_push`, `dolly_rotate`, `roll_reveal`
- `AnimatedCamera2_5D` per animare diversi parametri della camera
- `Transform` base con `position`, `rotation`, `scale`, `anchor`, `opacity`

Manca ancora:

- gerarchia transform risolta davvero
- null controller come nodo non renderizzato
- API camera-rig più alta livello
- distinzione chiara tra camera one-node e two-node
- curve keyframe più cinematografiche
- sampling subframe per motion blur
- strumenti di debug per path e target

## Obiettivo finale

La scena deve poter essere scritta in modo simile a questo:

```cpp
SceneBuilder s(ctx);

s.null("cam_ctrl", [](NullBuilder& n) {
    n.position({0.0f, 0.0f, 0.0f});
    n.rotation({0.0f, 18.0f, 0.0f});
});

s.null("focus_target", [](NullBuilder& n) {
    n.position({0.0f, 0.0f, 0.0f});
});

CameraRig rig;
rig.mode = CameraRigMode::TwoNode;
rig.parent_name = "cam_ctrl";
rig.target_name = "focus_target";
rig.orbit_yaw.key(0, -18.0f);
rig.orbit_yaw.key(90, 18.0f);
rig.orbit_radius.key(0, 1200.0f);
rig.orbit_radius.key(90, 860.0f);
rig.focus_distance.key(0, 900.0f);
rig.focus_distance.key(90, 420.0f);
rig.motion_blur.enabled = true;
rig.motion_blur.samples = 8;

s.camera_2_5d(rig.evaluate(ctx.frame));
```

## Principi di design

- Il core deve rimanere headless e deterministico.
- La camera deve essere valutabile per frame e anche a subframe.
- Il null controller non deve essere renderizzato.
- Gli anchor devono essere espliciti e testabili.
- Il sistema deve funzionare bene sia in 16:9 sia in altri aspect ratio.
- Il debug deve essere esportabile, non solo visibile in UI.

## Fase 1. Transform hierarchy vera

Scopo:

- introdurre una transform strutturata per scene node, layer e camera
- supportare parent-child reale con anchor point
- risolvere world matrix in modo deterministico

Da introdurre:

- `include/chronon3d/scene/transform/transform_3d.hpp`
- `include/chronon3d/scene/transform/transform_resolver.hpp`
- `src/scene/transform/transform_resolver.cpp`
- `tests/scene/transform_hierarchy_tests.cpp`

API target:

```cpp
struct Transform3D {
    Vec3 position{0, 0, 0};
    Vec3 rotation{0, 0, 0};
    Vec3 scale{1, 1, 1};
    Vec3 anchor{0, 0, 0};
    std::string parent_name;
    bool inherits_position{true};
    bool inherits_rotation{true};
    bool inherits_scale{true};
};
```

Regole:

- `local_matrix = T * R * S * A`
- `world_matrix = parent_world_matrix * local_matrix`
- se il nodo non ha parent, il world coincide col local
- i flag `inherits_*` permettono comportamenti AE-like più precisi

Test da avere:

- rotazione attorno all'anchor
- scala attorno all'anchor
- parenting semplice
- parenting annidato
- cycle detection
- comportamento stabile con trasformazioni identity

## Fase 2. Null controller

Scopo:

- creare nodi non renderizzati usati come controllori camera e target
- permettere orbit e tracking senza animare direttamente la camera

API target:

```cpp
s.null("cam_ctrl", [](NullBuilder& n) {
    n.position({0, 0, 0});
    n.rotation({0, 20, 0});
});
```

Comportamento:

- un null ha solo transform
- non produce output visivo
- partecipa alla gerarchia
- può essere usato come parent di camera o layer

## Fase 3. CameraRig

Scopo:

- offrire una camera di livello regia
- combinare target, orbit, dolly, pan, tilt, roll, zoom e focus in un'unica astrazione

Da introdurre:

- `include/chronon3d/scene/camera/camera_rig.hpp`
- `src/scene/camera/camera_rig.cpp`
- `tests/scene/camera_rig_tests.cpp`

Concetti:

- `OneNode`: camera basata su position + rotation
- `TwoNode`: camera basata su position + target
- `parent_name`: per il null controller
- `target_name`: per il punto di interesse

Struttura target:

```cpp
struct CameraRig {
    bool enabled{true};
    CameraRigMode mode{CameraRigMode::TwoNode};
    std::string parent_name;
    std::string target_name;
    AnimatedValue<Vec3> target{Vec3{0, 0, 0}};
    AnimatedValue<f32> orbit_yaw{0.0f};
    AnimatedValue<f32> orbit_pitch{0.0f};
    AnimatedValue<f32> orbit_radius{1000.0f};
    AnimatedValue<Vec3> track{Vec3{0, 0, 0}};
    AnimatedValue<f32> dolly{0.0f};
    AnimatedValue<f32> pan{0.0f};
    AnimatedValue<f32> tilt{0.0f};
    AnimatedValue<f32> roll{0.0f};
    AnimatedValue<f32> zoom{1000.0f};
    AnimatedValue<f32> fov_deg{50.0f};
    AnimatedValue<f32> focus_distance{1000.0f};
    AnimatedValue<f32> aperture{0.015f};
    MotionBlurSettings motion_blur{};
};
```

Uscita:

- `evaluate(Frame)` -> `Camera2_5D`
- `evaluate_at(float)` per subframe / motion blur

## Fase 4. Keyframe graph cinematico

Scopo:

- passare da interpolazione base a curve registiche
- supportare hold, linear, bezier e auto-bezier

Da introdurre:

- `include/chronon3d/animation/animation_curve.hpp`
- `include/chronon3d/animation/spatial_bezier_path.hpp`
- `src/animation/animation_curve.cpp`
- `tests/animation/animation_curve_tests.cpp`

Requisiti:

- `evaluate_at(float frame)` oltre a `evaluate(Frame)`
- easing per keyframe
- handle temporali
- path spaziali per position/target
- sampling subframe per motion blur

## Fase 5. DOF e motion blur

Scopo:

- rendere il movimento camera più cinematografico
- agganciare focus pull e blur alla camera rig

Da fare:

- integrare `MotionBlurSettings` nella camera finale
- renderizzare subframe distribuiti sull’otturatore
- calcolare blur da distanza di focus
- supportare focus target nominale e focus distance esplicito

## Fase 6. Safe transforms e aspect ratio

Scopo:

- evitare che il rig si rompa cambiando aspect ratio
- mantenere target e composizione in safe area

Da introdurre:

- coordinate space esplicito
- anchor normalizzato e anchor pixel
- helper per safe area
- debug overlay per misurare drift del target

## Fase 7. Debug e validazione

Scopo:

- verificare che il rig camera faccia davvero quello che deve

Test e strumenti:

- path sampling export JSON
- overlay del motion path
- frame-by-frame camera state dump
- test di determinismo per camera rig
- test di identità motion blur con 1 sample
- test di coerenza focus target

## Ordine consigliato di implementazione

1. Transform hierarchy + anchor
2. Null controller
3. CameraRig
4. Keyframe graph cinematografico
5. DOF e motion blur
6. Safe transforms
7. Debug/export

## Definizione di done

La parte camera è “pronta” quando:

- una scena può avere camera + target + null controller separati
- il rig si muove in modo coerente su frame diversi
- il focus pull è stabile
- il motion blur non rompe il rendering
- i test confermano pivot, parenting e determinismo

## Primo passo già avviato

Il lavoro parte dalla fase 1: gerarchia transform + anchor. Questo è il fondamento necessario per il resto.
