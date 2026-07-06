// ── Built-in function dispatch (extracted from ExpressionParser) ──────────
// Included inline within the ExpressionParser class body.
// Do NOT include this header directly; it requires the enclosing class scope.

    double parse_function(std::string name) {
        if (name == "layer") {
            return parse_layer_ref();
        }

        // Parse regular numeric arguments (up to 5)
        double a = 0.0, b = 0.0, c = 0.0, d = 0.0, e = 0.0;
        int nargs = 0;

        skip_ws();
        if (!match(')')) {
            a = parse_ternary();
            nargs = 1;
            skip_ws();
            if (match(',')) { b = parse_ternary(); nargs = 2; skip_ws();
            if (match(',')) { c = parse_ternary(); nargs = 3; skip_ws();
            if (match(',')) { d = parse_ternary(); nargs = 4; skip_ws();
            if (match(',')) { e = parse_ternary(); nargs = 5; skip_ws();
            }}}}
            expect(')');
        }

        if (m_error) return 0.0;

        if (name == "sin")    return std::sin(a);
        if (name == "cos")    return std::cos(a);
        if (name == "tan")    return std::tan(a);
        if (name == "asin")   return std::asin(std::clamp(a, -1.0, 1.0));
        if (name == "acos")   return std::acos(std::clamp(a, -1.0, 1.0));
        if (name == "atan")   return std::atan(a);
        if (name == "atan2")  return std::atan2(a, b);
        if (name == "abs")    return std::fabs(a);
        if (name == "sqrt")   return std::sqrt(std::max(0.0, a));
        if (name == "exp")    return std::exp(a);
        if (name == "log")    return std::log(std::max(1e-300, a));
        if (name == "log10")  return std::log10(std::max(1e-300, a));
        if (name == "ceil")   return std::ceil(a);
        if (name == "floor")  return std::floor(a);
        if (name == "round")  return std::round(a);
        if (name == "trunc")  return std::trunc(a);
        if (name == "sign")   return (a > 0.0) ? 1.0 : ((a < 0.0) ? -1.0 : 0.0);
        if (name == "pow")    return std::pow(a, b);

        if (name == "min")    return std::min(a, b);
        if (name == "max")    return std::max(a, b);
        if (name == "clamp")  return std::clamp(a, b, c);

        if (name == "degreesToRadians" || name == "radians") return a * (3.14159265358979323846 / 180.0);
        if (name == "radiansToDegrees" || name == "degrees") return a * (180.0 / 3.14159265358979323846);

        if (name == "random") {
            unsigned int seed = m_state ? m_state->random_seed : 0;
            if (m_state && m_state->seed_random_active) {
                ++m_state->random_seed;
                seed = m_state->random_seed;
            }
            double r = expr_detail::seeded_random(seed);
            if (nargs == 0) return r;
            if (nargs == 1) return r * a;
            return a + r * (b - a);
        }

        if (name == "seedRandom") {
            if (m_state) {
                m_state->random_seed = static_cast<unsigned int>(a);
                m_state->seed_random_active = true;
                if (nargs >= 2) {
                    // seedRandom(seed, timeless=false)
                }
            }
            return 0.0;
        }

        if (name == "posterizeTime" || name == "posterize") {
            if (m_state) {
                m_state->posterize_fps = std::max(0.001, a);
                m_state->posterize_active = true;
            }
            return a;
        }

        if (name == "wiggle") {
            double freq = std::max(0.001, a);
            double amp  = b;
            unsigned int seed = m_state ? m_state->random_seed : 0;
            double t = m_ctx ? m_ctx->time : 0.0;
            if (nargs >= 3) t = c;
            if (nargs >= 4) seed = static_cast<unsigned int>(d);
            return expr_detail::wiggle_impl(freq, amp, t, seed);
        }

        if (name == "linear") {
            if (nargs == 3) {
                return expr_detail::linear_t(a, 0.0, 1.0, b, c);
            }
            if (nargs >= 5) {
                return expr_detail::linear_t(a, b, c, d, e);
            }
            return expr_detail::linear_t(a, b, c, 0.0, d);
        }

        if (name == "ease") {
            if (nargs == 3) {
                return expr_detail::ease_t(a, 0.0, 1.0, b, c);
            }
            if (nargs >= 5) {
                return expr_detail::ease_t(a, b, c, d, e);
            }
            return expr_detail::ease_t(a, b, c, 0.0, d);
        }

        if (name == "easeIn") {
            double t_val = a, t_min = 0.0, t_max = 1.0, v_min = b, v_max = c;
            if (nargs >= 5) { t_min = b; t_max = c; v_min = d; v_max = e; }
            if (std::abs(t_max - t_min) < 1e-12) return v_min;
            double frac = std::clamp((t_val - t_min) / (t_max - t_min), 0.0, 1.0);
            frac = frac * frac;
            return v_min + frac * (v_max - v_min);
        }

        if (name == "easeOut") {
            double t_val = a, t_min = 0.0, t_max = 1.0, v_min = b, v_max = c;
            if (nargs >= 5) { t_min = b; t_max = c; v_min = d; v_max = e; }
            if (std::abs(t_max - t_min) < 1e-12) return v_min;
            double frac = std::clamp((t_val - t_min) / (t_max - t_min), 0.0, 1.0);
            frac = 1.0 - (1.0 - frac) * (1.0 - frac);
            return v_min + frac * (v_max - v_min);
        }

        if (name == "loopOut" || name == "loopIn") {
            std::string type = "cycle";
            int num_keys = 0;

            if (nargs >= 1) {
                int type_idx = static_cast<int>(a);
                switch (type_idx) {
                    case 0: type = "cycle"; break;
                    case 1: type = "pingpong"; break;
                    case 2: type = "offset"; break;
                    case 3: type = "continue"; break;
                    default: type = "cycle"; break;
                }
            }
            if (nargs >= 2) num_keys = static_cast<int>(b);

            if (m_ctx) {
                double t_min = m_ctx->in_point;
                double t_max = m_ctx->out_point;
                double t = m_ctx->time;

                if (name == "loopIn") {
                    if (t < t_min) {
                        double looped = expr_detail::loop_helper(t_min + (t_min - t), t_min, t_max, num_keys, type);
                        return m_ctx->value;
                    }
                } else {
                    if (t > t_max) {
                        double looped = expr_detail::loop_helper(t, t_min, t_max, num_keys, type);
                        return m_ctx->value;
                    }
                }
                return m_ctx->value;
            }
            return m_ctx ? m_ctx->value : 0.0;
        }

        if (name == "valueAtTime") {
            if (m_ctx && m_ctx->layer_resolver) {
                return m_ctx->value;
            }
            return m_ctx ? m_ctx->value : 0.0;
        }

        if (name == "toComp" || name == "fromComp") {
            return a;
        }

        m_error = true;
        return 0.0;
    }

    // Returns 0.0 but stores the layer name for postfix member access.
    double parse_layer_ref() {
        skip_ws();
        std::string layer_name;

        // Parse string argument
        if (peek() == '\'' || peek() == '"') {
            char quote = m_expr[m_pos];
            ++m_pos;
            size_t start = m_pos;
            while (m_pos < m_expr.size() && m_expr[m_pos] != quote) ++m_pos;
            layer_name = std::string(m_expr.substr(start, m_pos - start));
            if (m_pos < m_expr.size()) ++m_pos;  // skip closing quote
        } else {
            // Numeric layer index
            double idx = parse_ternary();
            layer_name = "__index__" + std::to_string(static_cast<int>(idx));
        }

        skip_ws();
        expect(')');

        m_current_layer_name = std::move(layer_name);
        m_current_property_path.clear();

        return 0.0;
    }
