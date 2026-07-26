# TICKET-ANIM-SPRING-UNIFY — Spring canon unification

## Stato: CLOSED — 2026-07-26

Esiste una sola `SpringConfig` in
`include/chronon3d/animation/easing/spring.hpp`:

```cpp
struct SpringConfig {
    f32 mass{1.0f};
    f32 stiffness{100.0f};
    f32 damping{15.0f};
    f32 initial_velocity{0.0f};
};
```

Il sampler canonico è `sample_spring(TimeSeconds, from, to, config)` e
gestisce i rami underdamped, critically damped e overdamped. Gli overload
Frame/FrameRate/FrameContext delegano tutti a quel sampler; `MotionTimeline`
usa la stessa matematica durante il baking.

## Verifica

Il filtro `*spring*` di `chronon3d_core_tests` passa 17/17 test e 89/89
assertion, inclusi random access, equivalenza tra frame rate e velocità
iniziale. La ricerca produttiva trova una sola definizione di `SpringConfig`.

## Vincolo

Nuove animazioni spring devono partire da `TimeSeconds` e delegare al sampler
canonico; non introdurre formule o configurazioni locali.
