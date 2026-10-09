# TICKET-LIGHTS-SHADOWS-PARTICLES — luci reali, ombre portate, particelle veloci

Stato: **OPEN** (scope scelto dall'utente il 2026-10-08: luci reali con ombre
portate, e istanziamento GPU per le particelle).

## Audit di partenza (2026-10-08)

### Luci
- `LayerType::Light` (`render_plan.hpp`) disegna solo un alone radiale
  (`LightLayerPlan{radius, color, intensity}`). Il layer ha `accepts_lights(false)`
  e **non illumina nessun layer**.
- `LightContext` (`include/chronon3d/rendering/light_context.hpp`) è un modello
  2.5D globale (ambient + N·L + speculare + rim) applicato solo ai materiali
  `Material2_5D` con `accepts_lights`. Viene alimentato da `scene.light_context()`,
  non dal render plan.
- `ShadowSettings` (`rendering/shadow_settings.hpp`) descrive ombre di contatto,
  ambient e depth-aware; il nodo `ShadowNode` esiste, ma non è collegato a una
  sorgente luce.

### Particelle
- Campionatore stateless `sample_particles` (`scene/model/shape/particles.hpp`):
  emettitori Box/Ellipse/Edge/Path, vita, colore, gravità, turbolenza, sprite,
  rotazione, blend mode. Limite 1.000.000 particelle.
- `render_plan_compiler_scene.cpp` converte **ogni particella in un nodo**
  (`builder.circle` o `builder.image`). Con grandi quantità il costo è nel
  numero di nodi, non misurato.
- Test: `tests/scene/shapes/test_particles_sampling.cpp` (6 casi), più
  `tests/c_abi/*` e `tests/scene/motion/test_offline_motion_plan_adapter.cpp`.
- Nessun istanziamento GPU.

### Riferimento ChrononMotion3D
- `include/chrononmotion/motion/Lighting.hpp`: `LightKind` Ambient/Directional/
  Point/Spot; tracce animate per intensità, colore, posizione, rotazione,
  direzione, range, decay, angolo, penombra; `castsShadow`, bias, normal bias,
  raggio ombra.
- `RenderPlanEmission.hpp`: emette emettitori di particelle (condividono
  `ParticleEmitterParams` con Chronon3d) e luci per layer. Le luci globali
  (`lighting rigs`) sono rifiutate esplicitamente.
- `Lighting.hpp` è indipendente dal renderer: il consumatore è da scrivere.

## Criteri di accettazione

### A. Luci (render plan)
- A1. `LightLayerPlan` accetta `kind` ∈ {point, spot, directional}, `range`,
  `decay`, `angle`, `penumbra`, `direction`, `cast_shadow`, `shadow_radius`,
  `shadow_bias`. Default retrocompatibili: un layer `light` senza `kind` resta un
  alone radiale identico a oggi.
- A2. Decoder, validatore, fingerprint e schema JSON (`schemas/json/`) aggiornati;
  input invalidi (range ≤ 0, angle fuori [0, π], decay < 0) sono rifiutati con
  messaggio esplicito.
- A3. Un layer luce illumina i layer sottostanti con falloff: point ∝ 1/(1+d^decay)
  entro `range`; spot con cono e penombra; directional uniforme.
- A4. Tracce animate per posizione, intensità, colore, direzione (come in
  ChrononMotion3D) campionate per frame in modo deterministico.
- A5. Test per ogni tipo: valore di illuminazione in punti noti (closed form).

### B. Ombre portate
- B1. Un layer con `cast_shadow` proietta la propria silhouette (alpha) sui layer
  ricevitori nella direzione della luce (directional) o radialmente (point/spot).
- B2. Blur `shadow_radius`, bias per evitare auto-ombreggiatura, opacità.
- B3. Test: ombra di un rettangolo su sfondo bianco, posizione e dimensione attese
  entro tolleranza di 1 px; nessuna ombra se `cast_shadow` è false.

### C. Particelle
- C1. CPU: un solo nodo di disegno per emettitore (batch), output identico byte per
  byte alla versione per-nodo sullo stesso seed (test di parità).
- C2. Misura con `chrono` su 1k, 10k, 100k particelle, 1080p, 1 frame: tempi
  riportati nel ticket. Nessun claim di velocità senza questa misura.
- C3. Vulkan: disegno istanziato; parità con il percorso CPU entro tolleranza
  documentata; test che fallisce se il backend non è Vulkan (no fallback silenzioso).

### D. Riferimento ChrononMotion3D
- D1. Le tracce di luce e le particelle emesse da ChrononMotion3D
  (`tests/motion_authoring.cpp`) continuano a decodificare e a renderizzare.

## Verifica finale
- Target: `chronon3d_c_abi_tests`, `chronon3d_render_plan_*`, `chronon3d_render_graph_tests`,
  `chronon3d_cli_tests`, `chronon3d_particles` (nuovo se serve), più la suite
  ChrononMotion3D `motion_authoring`.
- Ogni claim con exit code e numero di casi.
