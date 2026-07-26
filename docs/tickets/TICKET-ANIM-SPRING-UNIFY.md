# TICKET-ANIM-SPRING-UNIFY — Spring canon unification (P0)

## Stato: RESOLVED (2026-07-12)

Audit "Remotion-like surface" aveva classificato la spring come
`PARTIAL — due implementazioni concorrenti` (audit §2). Il presente
chore chiude quel partial consolidando due `struct SpringConfig`
nello stesso `namespace chronon3d` in un'unica struct canonica +
un'unica math function.

## Problema

Due `struct SpringConfig` nello stesso namespace `chronon3d`:

| Variante | Path | Campi | Math |
| --- | --- | --- | --- |
| `motion::SpringConfig` (legacy) | `include/chronon3d/animation/motion/motion.hpp` | `stiffness{100}, damping{10}, duration{Frame{60}}` | closed-form critically damped con `omega = sqrt(stiffness)`; `damping` ignorato nella formula |
| `easing/spring.hpp::SpringConfig` (canonical) | `include/chronon3d/animation/easing/spring.hpp` | `stiffness{100}, damping{15}, mass{1}` | closed-form damped harmonic oscillator con 3 rami (under/over/critical); v0=0 implicito |

Problemi rilevati:
- **Doppia API**: stessa idea, nomi identici ma campi diversi.
- **Cat-3 anti-dup violation**: due struct `SpringConfig` in `include/chronon3d/`.
- **`v0` non supportato**: impossibile modellare impatti / hand-off in entrata.
- **Bug ramo overdamped nel canon**: la formula storica produce
  `y'(0) = 2·delta·r1` ≠ 0, quindi il "rest" non è davvero at-rest
  in quel regime — incoerenza con i rami under/critical.

## Soluzione

1. **Una sola struct canonica** in `animation/easing/spring.hpp`:
   ```cpp
   struct SpringConfig {
       f32 mass{1.0f};
       f32 stiffness{100.0f};
       f32 damping{15.0f};
       f32 initial_velocity{0.0f};
   };
   ```
   (Ordine campi canonico: mass, stiffness, damping, initial_velocity.
   C++20 designated-init deve rispettare l'ordine di dichiarazione.)

2. **Una sola math function** in `animation/easing/spring.hpp`:
   ```cpp
   inline f32 sample_spring(TimeSeconds time,
                            f32 from, f32 to,
                            const SpringConfig& config = {});
   ```
   Ricalcolo completo dei coefficienti con `v0 ≠ 0` per tutti e 3
   i rami (under/over/critical damped). Fix implicito del bug
   overdamped (`C2 = (v0 - r1·y0)/(r2-r1)`).

3. **Wrapper thin preservati** per ergonomia (NON nuova math, solo
   conversione `Frame/FrameContext/SequenceContext → TimeSeconds`):
   ```cpp
   inline f32 spring(Frame frame, FrameRate fps, f32 from, f32 to,
                     const SpringConfig& config = {});    // → sample_spring
   inline f32 spring(const FrameContext& ctx, f32 from, f32 to,
                     const SpringConfig& config = {});    // → sample_spring
   inline f32 spring(const SequenceContext& ctx, f32 from, f32 to,
                     const SpringConfig& config = {});    // → sample_spring
   ```

4. **`motion::SpringConfig` rimosso**:
   - Path locale in `motion/motion.hpp` eliminato.
   - `MotionTimeline<T>::spring(...)` cambia signature da
     `(const SpringConfig& config, T target)` →
     `(Frame duration, T target, const SpringConfig& config = {})`.
   - `MotionTimeline<T>::spring(...)` ora chiama `sample_spring`
     per ogni frame da bakare (1-frame spacing, `kSpringBakeFps = 60.0`).

5. **Preset aggiornati** a 4-arg positional (ordine campi canonico):
   `Spring::Gentle{1.0f, 120.0f, 14.0f, 0.0f}`,
   `Spring::Snappy{1.0f, 200.0f, 18.0f, 0.0f}`,
   `Spring::Bouncy{1.0f, 300.0f, 12.0f, 0.0f}`,
   `Spring::Heavy{1.0f,  80.0f, 20.0f, 0.0f}`.

## Call site analysis

| Call site | Status |
| --- | --- |
| `tests/core/animation/test_spring.cpp` | Aggiornato a `sample_spring(TimeSeconds,...)` + nuovo test per `initial_velocity` |
| `content/common/animation_helpers.hpp` | Non chiama `.spring(...)` direttamente — invariato |
| `motion::SpringConfig{...}` (motion.hpp docstring) | Solo doc, nessun compile |
| `MotionTimeline::spring(...)` production call site | Nessuno in `content/` — la firma nuova `(Frame, T, config)` non rompe nessun caller reale |

## Build verification

- File toccati: 3 (header canonico, header motion, test).
- API change: rimossa `spring(f32 t, f32 from, f32 to, config)`.
  Rimossa `MotionTimeline::spring(const SpringConfig&, T)`.
  Aggiunti `sample_spring(TimeSeconds, f32, f32, config)` +
  `MotionTimeline::spring(Frame, T, config)` + field `initial_velocity`.
  Aggiornata firma `MotionTimeline::spring` al path cat-3 unificato.
- Build: `cmake --build build/chronon/linux-dev` deve essere verde
  post-commit (vedi §Forward-points sotto).

## Forward-points

- **TICKET-CANONICAL-INIT-ORDER**: aggiungere `tools/check_spring_canonical.sh`
  che legge solo `include/chronon3d/animation/easing/spring.hpp` e verifica
  via grep/AST che non esistano altre struct `SpringConfig` nello stesso
  namespace (gate Cat-3 anti-dup). Implementazione deferred per AGENTS.md
  "Fare PR piccole e mirate" + regola-documentation-precedes-lint-tooling.

- **TICKET-SPRING-VEC3**: attualmente `MotionTimeline::spring` ha
  `static_assert(std::is_arithmetic_v<T>)`: la spring funziona solo
  per `T` arithmetic. Forward-point per supportare `Vec3` come
  composizione di 3 spring scalari (post-V0.2, audit §3).

- **TICKET-SPRING-CROSS-OS-BIT-IDENTITY**: laudit menziona «test
  specifici per bit-identity cross-OS di `std::sin/cos/exp`»;
  test a tolleranza 1-ULP da aggiungere quando libm divergence
  diventerà blocker (audit §1 Nota sul determinismo).

## Cross-references

- Audit source: audit "Remotion-like surface" §2 Spring + §5 readiness.
- File canonico modificato: `include/chronon3d/animation/easing/spring.hpp`.
- File rimosso (ridefinito): `include/chronon3d/animation/motion/motion.hpp`
  sezione `struct SpringConfig`.
- Test exercises: `tests/core/animation/test_spring.cpp`.
- Anti-duplicazione regole: `docs/ANTI_DUPLICATION_RULES.md` regola §10
  ("Nessuna reimplementazione di interpolazioni/animazioni").
- AGENTS.md: §Cat-3 (anti-dup), §Docs canonical update discipline
  (cronaca estesa = ticket-home).
