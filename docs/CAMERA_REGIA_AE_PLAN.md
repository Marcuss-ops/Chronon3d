# Camera da Regia — Stato e Prossimi Passi

> **Principio fondamentale:** Chronon3D è un engine headless e deterministico.
> Non ci sarà mai una GUI, un timeline editor, o un property panel.
> Tutto è codice C++, testabile, esportabile, riproducibile.

**Aggiornato:** Giugno 2026

---

## ✅ Fase 1 — Transform Hierarchy (COMPLETATA)

**Cosa c'è:**
- `Transform3D` con `position`, `rotation`, `scale`, `anchor`, `parent_name`
- `TransformResolver` con risoluzione world matrix deterministica
- Supporto `inherits_position`, `inherits_rotation`, `inherits_scale`
- Anchor point per pivot di rotazione e scala
- Parenting annidato con cycle detection
- Test: `tests/scene/camera/test_camera_hierarchy.cpp`

**API:**
```cpp
s.null_layer("cam_ctrl", [](NullBuilder& n) {
    n.position({0, 0, 0});
    n.rotation({0, 18, 0});
});
s.layer("card", [](LayerBuilder& l) {
    l.parent("cam_ctrl").position({80, 0, 0});
});
```

---

## ✅ Fase 2 — Null Controller (COMPLETATA)

**Cosa c'è:**
- `NullBuilder` — nodo non renderizzato con transform completa
- Partecipa alla gerarchia parent-child
- Può essere parent di camera o layer
- `visible_in_diagnostics` per debug overlay
- Test: `tests/scene/layout/test_layer_hierarchy.cpp`, `test_scene_builder.cpp`

**API:**
```cpp
s.null_layer("camera_rig_null", [](NullBuilder& n) {
    n.position({0, 0, 0});
    n.rotation({0, ctx.progress() * 20.0f, 0.0f});
});

s.null_layer("camera_target", [](NullBuilder& n) {
    n.position({0, 0, 0});
    n.parent("camera_rig_null");
});
```

---

## ✅ Fase 3 — CameraRig (COMPLETATA)

**Cosa c'è:**
- `CameraRig` con modalità `OneNode` e `TwoNode`
- Orbit: `orbit_yaw`, `orbit_pitch`, `orbit_radius` (tutti animabili)
- Track: `track` (Vec3 animabile)
- Dolly: `dolly` (float animabile)
- Pan/Tilt/Roll: singole proprietà animabili
- Zoom/FOV: dual mode `Zoom` (focal length diretta) e `Fov` (angolo di visione)
- Focus: `focus_distance`, `aperture`, `max_blur`
- Parent: `parent_name` per agganciare a null controller
- Target: `target_name` per agganciare a null target
- Motion Blur: `MotionBlurSettings` con `enabled`, `samples`, `shutter_angle`
- DOF: `DepthOfFieldSettings` con `focus_z`, `aperture`, `max_blur`
- `evaluate(Frame)` → `Camera2_5D` con world matrix risolta

**CameraRig Presets (6 preset cinematografici):**
| Preset | Descrizione |
|--------|-------------|
| `orbit_reveal` | Orbit yaw + radius animato |
| `premium_push_in` | Push in con tilt + easing cubic bezier |
| `parallax_stack` | Parallax pan laterale |
| `slow_dolly_focus` | Dolly lento con focus pull |
| `card_fan_sweep` | Sweep ampio con roll |
| `hero_title_push` | Push frontale su titolo |

**CameraMotion Presets (legacy, backward compat):**
`hero_push_in`, `orbit_yaw`, `parallax_pan`, `dolly_zoom`, `focus_pull`, `low_angle_reveal`, `subtle_float`, `dolly_in`, `dolly_out`, `orbit_small`, `dolly_rotate`, `roll_reveal`

**API CameraRig:**
```cpp
CameraShotProfile shot;
shot.rig.mode = CameraRigMode::TwoNode;
shot.rig.target_name = "camera_target";
shot.rig.orbit_yaw.key(0, -15.0f).key(60, 15.0f, EasingCurve{Easing::InOutSine});
shot.rig.orbit_radius.key(0, 1400.0f).key(45, 500.0f, EasingCurve{Easing::InOutCubic}).key(90, 1400.0f);
shot.rig.fov_deg.set(54.4f);
shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
```

---

## ✅ Fase 3.5 — Camera Debug Overlay (COMPLETATA OGGI)

**Cosa c'è:**
- **Jerk Graph** — grafico 2D del jerk cinematografico nel tempo
- **3D Path Trace** — percorso camera proiettato su schermo con colori jerk
- **Top-Down View** — vista dall'alto XZ con tutti i layer, camera, cono FOV, griglia, assi
- **Depth Side-View** — vista laterale XZ dove Z=altezza, barre colorate per ogni layer
- **Per-layer bbox outlines** nel rendering
- **Camera target cross** (rosso + stroke ring)
- **Screen center crosshair** (bianco)
- **Safe area rect** (giallo 90%)
- Opzioni: `show_jerk_graph`, `show_path_trace`, `show_topdown_preview`, `show_depth_side_view`
- Posizionamento diagonale rispetto all'anchor per evitare sovrapposizioni

---

## ✅ Fase 3.6 — Camera Test Suite (COMPLETATA OGGI)

**14 test camera, tutti PASS:**

| Test | Cosa valida |
|------|-------------|
| FrustumCullingPrecisionTest | 80 card, frustum culling preciso |
| KinematicJerkAndInterpolationTest | Jerk cinematografico, continuity |
| DepthSortingStressTest | 9 layer a micro-Z, depth order |
| SubpixelJitterValidationTest | 120 frame, jitter < 1px |
| MultiTargetBoundingBoxFitTest | Auto-fit su 3 card, safe area |
| OrbitTargetLockTest | Target lock durante orbit |
| DollyPerspectiveScaleTest | **FOV scaling W=2·|Z|·tan(FOV/2)**, dolly growth |
| ParentNullRigTest | Parent-child matrix, null controller |
| RollPanTiltGridTest | Roll/Pan/Tilt su griglia |
| SafeFramingAspectRatioTest (×4) | 16:9, 1:1, 4:5, 9:16 |
| PerspectiveDepthScaleDiagnosticTest | 5 layer Z-depth, drop lines, FOV validation |

**FOV Scaling Validation:**
- Formula: `expected_ratio = (Z_far/Z_near)² × (orig_near/orig_far)`
- Depth computation: view-space depth via `get_camera_view_matrix()` — identico a `project_layer_2_5d`
- Tolleranza: 20%
- DollyTest: errore 0.00% a tutti i frame (0/45/90), `fov_trend_all_consistent: True`
- DiagnosticTest: errore 3.15%, `fov_scaling_consistent: True`

---

## ⬜ Fase 4 — Keyframe Graph Cinematico (DA FARE)

**Problema:** Oggi `AnimatedValue<T>` supporta linear + easing predefiniti. Manca:
- **Bezier curves** con handle regolabili
- **Roving keys** (velocità costante tra keyframe)
- **Continuous velocity** tra keyframe adiacenti
- **Auto-bezier** (tangent automatica)
- **Path spaziali** per position/target (Bezier 3D)

**Soluzione:**
```cpp
// Nuova interpolation curve
AnimationCurve curve;
curve.add_key(0.0f, -15.0f, KeyInterp::Bezier{-0.5f, 1.2f});
curve.add_key(45.0f, 15.0f, KeyInterp::AutoBezier);
curve.add_key(90.0f, -15.0f, KeyInterp::Roving);

// Spatial bezier path per target
SpatialBezierPath path;
path.add_control({0, 0, 0}, {200, 100, 0}, {-200, 50, 0});
path.evaluate(0.5f); // → Vec3 a metà path
```

**File:**
- `include/chronon3d/animation/animation_curve.hpp` (nuovo)
- `include/chronon3d/animation/spatial_bezier_path.hpp` (nuovo)
- `src/animation/animation_curve.cpp` (nuovo)

---

## ⬜ Fase 5 — Motion Blur Multi-Sample Reale (DA FARE)

**Problema:** `MotionBlurSettings` esiste nella camera ma non viene usato nel rendering. Il campo `samples` e `shutter_angle` vengono letti ma il rendering multi-sample non è implementato.

**Soluzione:**
```cpp
// In render_pipeline_composition.cpp:
if (settings.motion_blur.enabled && settings.motion_blur.samples > 1) {
    for (int s = 0; s < samples; ++s) {
        float t_offset = shutter_angle / 360.0f * (s / float(samples) - 0.5f);
        Scene sub = comp.evaluate(frame, t_offset);
        auto sub_fb = render_scene(sub, ...);
        accumulate(sub_fb, weight);
    }
    normalize(output_fb);
}
```

**Ottimizzazione:** Parallelizzare samples con `tbb::parallel_for` + SIMD accumulation con Highway.

**File:**
- `src/render_graph/pipeline/render_pipeline_composition.cpp`
- `src/backends/software/software_renderer.cpp`

---

## ⬜ Fase 5.5 — DOF (Depth of Field) Reale (DA FARE)

**Problema:** `DepthOfFieldSettings` ha `focus_z`, `aperture`, `max_blur` ma il blur dipendente dalla distanza di focus non è implementato nel rendering.

**Soluzione:**
- Calcolare distanza di ogni pixel dal piano di focus
- Applicare blur proporzionale alla distanza
- Filtro: `DOFNode` nel render graph che riceve `focus_distance` dalla camera

---

## ⬜ Fase 6 — Camera Framing Auto (PARZIALE)

**Cosa c'è:**
- `fit_camera_to_layers()` — dolly automatico per fit in viewport
- `CameraFramingOptions` con `max_iterations`, `dolly_step`
- `require_inside_safe_area()` nel validator

**Cosa manca:**
- **Auto-fit con aspect ratio diversi** — testare con 9:16, 4:5
- **Framing con target prioritario** — primo layer = target, gli altri = bounds
- **Safe area configurabile** — oggi è hardcodato al 10%

---

## ⬜ Fase 7 — Camera Path Export (DA FARE)

**Problema:** Nessun modo per exportare il percorso camera come dati strutturati (JSON, CSV).

**Soluzione:**
```bash
# Export camera path come JSON
chronon3d_cli camera-path --comp MyComp --frames 0-90 --format json --output path.json

# Output:
{
  "frames": [
    {"frame": 0, "position": [0, 0, -1400], "target": [80, 0, 0], "fov": 54.4, "depth": 1400.0},
    {"frame": 1, "position": [-2.3, 0.1, -1399.8], "target": [79.8, 0.2, 0], "fov": 54.4, "depth": 1399.8},
    ...
  ]
}
```

**File:** `apps/chronon3d_cli/commands/command_camera_path.cpp` (nuovo)

---

## 📊 Riepilogo Stato

| Fase | Descrizione | Stato |
|------|-------------|-------|
| 1 | Transform Hierarchy | ✅ COMPLETATA |
| 2 | Null Controller | ✅ COMPLETATA |
| 3 | CameraRig | ✅ COMPLETATA |
| 3.5 | Camera Debug Overlay | ✅ COMPLETATA |
| 3.6 | Camera Test Suite (14 test) | ✅ COMPLETATA |
| 4 | Keyframe Graph Cinematico | ⬜ DA FARE |
| 5 | Motion Blur Multi-Sample | ⬜ DA FARE |
| 5.5 | DOF Reale | ⬜ DA FARE |
| 6 | Camera Framing Auto | 🟡 PARZIALE |
| 7 | Camera Path Export | ⬜ DA FARE |

---

## 🎯 Priorità Prossimi Passi

1. **Keyframe Graph Cinematico** (Fase 4) — Bezier curves, roving keys, spatial paths
2. **Motion Blur Multi-Sample** (Fase 5) — rendere `MotionBlurSettings` funzionante
3. **Camera Path Export** (Fase 7) — debug e validazione del percorso camera
4. **DOF Reale** (Fase 5.5) — blur basato sulla distanza di focus
