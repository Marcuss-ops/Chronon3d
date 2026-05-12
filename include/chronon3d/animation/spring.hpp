#pragma once

#include <chronon3d/core/types.hpp>
#include <cmath>

namespace chronon3d {

struct SpringParams {
    f32 stiffness{170.0f};
    f32 damping{26.0f};
    f32 mass{1.0f};
};

template <typename T>
class SpringValue {
public:
    SpringValue() = default;
    explicit SpringValue(T initial_value) : m_current(initial_value), m_target(initial_value) {}

    void set_target(T target, T velocity = T{}) {
        m_target = target;
        m_velocity = velocity;
        m_initial_offset = m_current - m_target;
    }

    // Analytical solution for the Spring equation (Mass-Spring-Damper)
    // Based on the implementation from Android/React-Spring
    T evaluate(f32 dt) {
        if (dt <= 0.0f) return m_current;

        f32 k = m_params.stiffness;
        f32 b = m_params.damping;
        f32 m = m_params.mass;

        f32 zeta = b / (2.0f * std::sqrt(k * m)); // Damping ratio
        f32 omega_n = std::sqrt(k / m);           // Undamped natural frequency

        T result;
        if (zeta < 1.0f) {
            // Underdamped
            f32 omega_d = omega_n * std::sqrt(1.0f - zeta * zeta);
            f32 envelope = std::exp(-zeta * omega_n * dt);
            
            // x(t) = exp(-zeta*wn*t) * (c1*cos(wd*t) + c2*sin(wd*t))
            T c1 = m_initial_offset;
            T c2 = (m_velocity + m_initial_offset * (zeta * omega_n)) * (1.0f / omega_d);
            
            result = m_target + envelope * (c1 * std::cos(omega_d * dt) + c2 * std::sin(omega_d * dt));
        } else if (zeta == 1.0f) {
            // Critically damped
            f32 envelope = std::exp(-omega_n * dt);
            T c1 = m_initial_offset;
            T c2 = m_velocity + m_initial_offset * omega_n;
            
            result = m_target + envelope * (c1 + c2 * dt);
        } else {
            // Overdamped
            f32 omega_d = omega_n * std::sqrt(zeta * zeta - 1.0f);
            f32 r1 = -omega_n * (zeta - std::sqrt(zeta * zeta - 1.0f));
            f32 r2 = -omega_n * (zeta + std::sqrt(zeta * zeta - 1.0f));
            
            T c2 = (m_velocity - m_initial_offset * r1) * (1.0f / (r2 - r1));
            T c1 = m_initial_offset - c2;
            
            result = m_target + c1 * std::exp(r1 * dt) + c2 * std::exp(r2 * dt);
        }

        m_current = result;
        return result;
    }

    void set_params(SpringParams p) { m_params = p; }
    [[nodiscard]] T current() const { return m_current; }
    [[nodiscard]] T target() const { return m_target; }

private:
    T m_current{};
    T m_target{};
    T m_velocity{};
    T m_initial_offset{};
    SpringParams m_params;
};

} // namespace chronon3d
