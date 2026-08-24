// ── ExprTk spike: ExpressionCompiler adapter + dual-run corpus ─────────
#include <chronon3d/math/expression.hpp>   // custom recursive-descent parser
#include <chronon3d/math/expression_types.hpp>

#include "exprtk.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

// ── Pre-processor: transforms AE-syntax to ExprTk-compatible syntax ──
//   && → and  /  || → or  /  ! → not (preserving !=)
//   thisComp.member → __comp_MEMBER
//   thisLayer.member → __layer_MEMBER
//   thisProperty.member → __prop_MEMBER
//   layer('name').prop.path → __l_N (placeholder)
//   degreesToRadians → deg2rad   /   radiansToDegrees → rad2deg

struct LayerRef {
    std::string layer_name;
    std::string prop_path;
    std::string placeholder;  // e.g. __l_0
};

struct Preprocessed {
    std::string text;
    std::vector<LayerRef> layer_refs;
};

std::string extract_ident(const char* s, size_t maxlen, size_t& out_len) {
    out_len = 0;
    while (out_len < maxlen && (std::isalnum(static_cast<unsigned char>(s[out_len])) || s[out_len] == '_'))
        ++out_len;
    return std::string(s, out_len);
}

Preprocessed preprocess(std::string_view expr) {
    std::string out; out.reserve(expr.size() + 64);
    Preprocessed pp;
    size_t i = 0;

    auto skip_string = [&]() {
        char q = expr[i]; ++i;
        while (i < expr.size() && expr[i] != q) ++i;
        if (i < expr.size()) ++i;
    };

    while (i < expr.size()) {
        // Strings
        if (expr[i] == '\'' || expr[i] == '"') { size_t s = i; skip_string(); out.append(expr.substr(s, i - s)); continue; }

        // &&
        if (i + 1 < expr.size() && expr[i] == '&' && expr[i + 1] == '&') { out += " and "; i += 2; continue; }
        // ||
        if (i + 1 < expr.size() && expr[i] == '|' && expr[i + 1] == '|') { out += " or ";  i += 2; continue; }
        // !  (not !=). ExprTk accepts `not(...)`, but not the AE-style
        // prefix form. Consume one operand so the generated parentheses are
        // unambiguous for both scalar and parenthesized predicates.
        if (expr[i] == '!' && (i + 1 >= expr.size() || expr[i + 1] != '=')) {
            out += "not(";
            ++i;
            while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) {
                out += expr[i++];
            }
            if (i < expr.size() && expr[i] == '(') {
                std::size_t depth = 0;
                do {
                    if (expr[i] == '(') ++depth;
                    if (expr[i] == ')') --depth;
                    out += expr[i++];
                } while (i < expr.size() && depth != 0);
            } else {
                while (i < expr.size() &&
                       (std::isalnum(static_cast<unsigned char>(expr[i]))
                        || expr[i] == '_' || expr[i] == '.')) {
                    out += expr[i++];
                }
            }
            out += ')';
            continue;
        }

        // thisComp.xxx
        if (expr.substr(i, 9) == "thisComp.") {
            i += 9;
            size_t n; std::string m = extract_ident(expr.data() + i, expr.size() - i, n); i += n;
            out += "c3d_comp_" + m;
            continue;
        }
        // thisLayer.xxx
        if (expr.substr(i, 10) == "thisLayer.") {
            i += 10;
            size_t n; std::string m = extract_ident(expr.data() + i, expr.size() - i, n); i += n;
            out += "c3d_layer_" + m;
            continue;
        }
        // thisProperty.xxx
        if (expr.substr(i, 13) == "thisProperty.") {
            i += 13;
            size_t n; std::string m = extract_ident(expr.data() + i, expr.size() - i, n); i += n;
            out += "c3d_prop_" + m;
            continue;
        }

        // layer('name').prop.path  →  __l_N
        if (expr.substr(i, 6) == "layer("  && i + 7 < expr.size() &&
            (expr[i + 6] == '\'' || expr[i + 6] == '"')) {
            char q = expr[i + 6];
            size_t ns = i + 7;
            size_t ne = ns;
            while (ne < expr.size() && expr[ne] != q) ++ne;
            std::string lname(expr.substr(ns, ne - ns));
            size_t rp = ne + 1; // skip closing quote
            if (rp < expr.size() && expr[rp] == ')') {
                ++rp; // skip )
                if (rp < expr.size() && expr[rp] == '.') {
                    ++rp;
                    size_t ps = rp;
                    while (rp < expr.size() &&
                           (std::isalnum(static_cast<unsigned char>(expr[rp])) || expr[rp] == '_' || expr[rp] == '.'))
                        ++rp;
                    std::string prop(expr.substr(ps, rp - ps));
                    std::string sym = "c3d_l_" + std::to_string(pp.layer_refs.size());
                    pp.layer_refs.push_back({std::move(lname), std::move(prop), sym});
                    out += sym;
                    i = rp;
                    continue;
                }
                // layer() without property → 0
                out += "0"; i = rp; continue;
            }
        }

        // degreesToRadians → deg2rad
        if (i + 16 <= expr.size() && expr.substr(i, 16) == "degreesToRadians") { out += "deg2rad"; i += 16; continue; }
        // radiansToDegrees → rad2deg
        if (i + 16 <= expr.size() && expr.substr(i, 16) == "radiansToDegrees") { out += "rad2deg"; i += 16; continue; }

        // AE builtins whose names or argument order differ from ExprTk.
        if (expr.substr(i, 6) == "clamp(") { out += "c3d_clamp("; i += 6; continue; }
        if (expr.substr(i, 5) == "sign(") { out += "c3d_sign("; i += 5; continue; }

        out += expr[i]; ++i;
    }

    pp.text = std::move(out);
    return pp;
}

// ── Identifier scanner: find potential variable names in expression ──
// This allows us to pre-register all legacy vars as ExprTk symbols.
std::set<std::string> scan_identifiers(std::string_view expr) {
    std::set<std::string> idents;
    // Known keywords/functions we should NOT treat as variables
    static const std::set<std::string> kw = {
        "and","or","not","xor","xnor","nand","nor","true","false",
        "if","else","switch","case","default","while","for","repeat","until","break","continue","return",
        "abs","avg","ceil","clamp","equal","erf","erfc","exp","expm1","floor","frac",
        "log","log10","log1p","log2","logn","max","min","mul","ncdf","not_equal",
        "root","round","roundn","sgn","sqrt","sum","swap","trunc","pow",
        "sin","cos","tan","asin","acos","atan","atan2","sinh","cosh","tanh",
        "asinh","acosh","atanh","cot","csc","sec","sinc","hypot",
        "deg2rad","rad2deg","deg2grad","grad2deg",
        "in","like","ilike",
        "inrange", "linear", "ease", "easeIn", "easeOut",
        "PI", "E", "c3d_clamp", "c3d_sign",
    };

    size_t i = 0;
    while (i < expr.size()) {
        if (expr[i] == '\'' || expr[i] == '"') {
            char q = expr[i++];
            while (i < expr.size() && expr[i] != q) ++i;
            if (i < expr.size()) ++i;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            size_t start = i;
            while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) ++i;
            std::string tok(expr.substr(start, i - start));
            if (!kw.count(tok) && tok.size() > 0 && !std::isdigit(static_cast<unsigned char>(tok[0])))
                idents.insert(tok);
            continue;
        }
        ++i;
    }
    return idents;
}

struct AeVarargFunction final : exprtk::ivararg_function<double> {
    enum class Kind { Linear, Ease, EaseIn, EaseOut };
    explicit AeVarargFunction(Kind kind) : m_kind(kind) {}

    double operator()(const std::vector<double>& args) override {
        if (args.size() < 3) return 0.0;
        const double t = args[0];
        double t_min = 0.0, t_max = 1.0, v_min = args[1], v_max = args[2];
        if (args.size() >= 5) {
            t_min = args[1]; t_max = args[2]; v_min = args[3]; v_max = args[4];
        }
        if (std::abs(t_max - t_min) < 1e-12) return v_min;
        double f = std::clamp((t - t_min) / (t_max - t_min), 0.0, 1.0);
        if (m_kind == Kind::Ease) f = f * f * (3.0 - 2.0 * f);
        if (m_kind == Kind::EaseIn) f *= f;
        if (m_kind == Kind::EaseOut) f = 1.0 - (1.0 - f) * (1.0 - f);
        return v_min + f * (v_max - v_min);
    }

private:
    Kind m_kind;
};

struct AeClampFunction final : exprtk::ifunction<double> {
    AeClampFunction() : exprtk::ifunction<double>(3) {}
    double operator()(const double& value, const double& minimum,
                      const double& maximum) override {
        return std::clamp(value, minimum, maximum);
    }
};

struct AeSignFunction final : exprtk::ifunction<double> {
    AeSignFunction() : exprtk::ifunction<double>(1) {}
    double operator()(const double& value) override {
        return value > 0.0 ? 1.0 : value < 0.0 ? -1.0 : 0.0;
    }
};

// ── ExpressionCompiler ──────────────────────────────────────────────
class ExpressionCompiler {
public:
    explicit ExpressionCompiler(std::string_view raw_expr) {
        auto pp = preprocess(raw_expr);
        m_layer_refs = std::move(pp.layer_refs);

        // Collect ALL identifiers in the processed expression
        auto all_idents = scan_identifiers(pp.text);

        // Also ensure standard Chronon names are present
        static const char* kStdNames[] = {
            "frame","time","fps","index","value","value0","value1","value2",
            "width","height","numLayers","inPoint","outPoint",
            "c3d_comp_width","c3d_comp_height","c3d_comp_numLayers",
            "c3d_layer_index","c3d_layer_inPoint","c3d_layer_outPoint",
            "c3d_layer_width","c3d_layer_height","c3d_layer_opacity","c3d_layer_name",
            "c3d_prop_value","c3d_prop_value0","c3d_prop_value1","c3d_prop_value2",
            "c3d_prop_name","c3d_prop_index"
        };
        for (const auto* n : kStdNames) all_idents.insert(n);
        for (const auto* n : kStdNames) m_known_names.insert(n);
        m_known_names.insert("PI");
        m_known_names.insert("E");
        m_known_names.insert("true");
        m_known_names.insert("false");

        // Add layer placeholders
        for (size_t li = 0; li < m_layer_refs.size(); ++li)
            all_idents.insert(m_layer_refs[li].placeholder);

        // Create variable storage with name→index mapping
        m_vars.resize(all_idents.size(), 0.0);
        size_t idx = 0;
        for (const auto& name : all_idents) {
            m_var_map[name] = idx;
            m_symbol_table.add_variable(name, m_vars[idx]);
            ++idx;
        }

        // Constants (own storage so add_constant has a valid ref)
        m_const_pi = 3.14159265358979323846;
        m_const_e  = 2.71828182845904523536;
        m_const_true = 1.0;
        m_const_false = 0.0;
        m_symbol_table.add_constant("PI", m_const_pi);
        m_symbol_table.add_constant("E", m_const_e);
        m_symbol_table.add_constant("true", m_const_true);
        m_symbol_table.add_constant("false", m_const_false);

        m_expression.register_symbol_table(m_symbol_table);

        m_symbol_table.add_function("c3d_clamp", m_clamp);
        m_symbol_table.add_function("c3d_sign", m_sign);
        m_symbol_table.add_function("linear", m_linear);
        m_symbol_table.add_function("ease", m_ease);
        m_symbol_table.add_function("easeIn", m_ease_in);
        m_symbol_table.add_function("easeOut", m_ease_out);

        exprtk::parser<double> parser;
        if (!parser.compile(pp.text, m_expression)) {
            m_error = true;
            m_error_msg = parser.error();
        }
    }

    void set_context(const chronon3d::math::ExpressionContext& ctx) {
        set("frame", ctx.frame); set("time", ctx.time); set("fps", ctx.fps);
        set("index", ctx.index); set("value", ctx.value);
        set("value0", ctx.value0); set("value1", ctx.value1); set("value2", ctx.value2);
        set("width", ctx.width); set("height", ctx.height);
        set("numLayers", ctx.num_layers);
        set("inPoint", ctx.in_point); set("outPoint", ctx.out_point);
        set("c3d_comp_width", ctx.width); set("c3d_comp_height", ctx.height);
        set("c3d_comp_numLayers", ctx.num_layers);
        set("c3d_layer_index", ctx.index); set("c3d_layer_inPoint", ctx.in_point);
        set("c3d_layer_outPoint", ctx.out_point);
        set("c3d_layer_width", ctx.width_0); set("c3d_layer_height", ctx.height_0);
        set("c3d_layer_opacity", ctx.value);
        set("c3d_prop_value", ctx.value);
        set("c3d_prop_value0", ctx.value0);
        set("c3d_prop_value1", ctx.value1);
        set("c3d_prop_value2", ctx.value2);

        for (size_t i = 0; i < m_layer_refs.size(); ++i) {
            m_known_names.insert(m_layer_refs[i].placeholder);
            double r = ctx.layer_resolver
                ? ctx.layer_resolver(m_layer_refs[i].layer_name, m_layer_refs[i].prop_path, ctx.time)
                : std::numeric_limits<double>::quiet_NaN();
            set(m_layer_refs[i].placeholder, r);
        }
    }

    void set_legacy_vars(const std::unordered_map<std::string, double>& vars) {
        for (const auto& [k, v] : vars) {
            m_known_names.insert(k);
            set(k, v);
        }
    }

    double evaluate() { return m_expression.value(); }
    bool error() const {
        if (m_error) return true;
        for (const auto& name : m_var_map) {
            if (!m_known_names.count(name.first)) return true;
        }
        return false;
    }
    const std::string& error_msg() const { return m_error_msg; }

private:
    void set(const std::string& name, double v) {
        auto it = m_var_map.find(name);
        if (it != m_var_map.end()) m_vars[it->second] = v;
    }

    exprtk::symbol_table<double> m_symbol_table;
    exprtk::expression<double> m_expression;
    std::vector<double> m_vars;
    std::unordered_map<std::string, size_t> m_var_map;
    std::set<std::string> m_known_names;
    double m_const_pi, m_const_e, m_const_true, m_const_false;
    AeClampFunction m_clamp;
    AeSignFunction m_sign;
    AeVarargFunction m_linear{AeVarargFunction::Kind::Linear};
    AeVarargFunction m_ease{AeVarargFunction::Kind::Ease};
    AeVarargFunction m_ease_in{AeVarargFunction::Kind::EaseIn};
    AeVarargFunction m_ease_out{AeVarargFunction::Kind::EaseOut};
    std::vector<LayerRef> m_layer_refs;
    bool m_error{false};
    std::string m_error_msg;
};

// ═══════════════════════════════════════════════════════════════════════
// Corpus
// ═══════════════════════════════════════════════════════════════════════

struct TestCase {
    const char* name;
    const char* expression;
    std::unordered_map<std::string, double> vars;
    chronon3d::math::ExpressionContext ctx;
    bool uses_context{false};
    bool uses_layer_resolver{false};
};

static auto make_layer_resolver(const std::string& ln, const std::string& pp, double v) {
    return [=](const std::string& l, const std::string& p, double) -> double {
        return (l == ln && p == pp) ? v : std::numeric_limits<double>::quiet_NaN();
    };
}

std::vector<TestCase> build_corpus() {
    std::vector<TestCase> c;

    auto add = [&](const char* name, const char* expr,
                   std::unordered_map<std::string, double> v = {},
                   bool ctx = false, bool lr = false) {
        chronon3d::math::ExpressionContext cx;
        c.push_back({name, expr, std::move(v), cx, ctx, lr});
    };

    // Arithmetic
    add("constant", "42");          add("float", "3.14");
    add("add", "2 + 3");            add("sub", "10 - 4");
    add("mul", "3 * 4");            add("div", "10 / 4");
    add("precedence", "2 + 3 * 4"); add("paren", "(2 + 3) * 4");
    add("mod", "10 % 3");           add("exp", "2 ^ 3");
    add("exp_right", "2 ^ 3 ^ 2");  add("exp_prec", "2 * 3 ^ 2");
    add("neg_exp", "2 ^ -1");       add("unary_minus", "-5 + 3");
    add("unary_plus", "+5 + 3");    add("double_neg", "--5");
    add("div_zero", "1 / 0");       add("mod_zero", "5 % 0");

    // Variables
    add("var_frame", "frame", {{"frame", 30.0}});
    add("var_time_expr", "time * 100", {{"time", 0.5}});
    add("var_combo", "frame + time", {{"frame", 30.0}, {"time", 0.5}});

    // Comparisons
    add("lt_true", "3 < 5");    add("lt_false", "5 < 3");
    add("gt_true", "5 > 3");    add("gt_false", "3 > 5");
    add("le_true", "3 <= 5");   add("le_eq", "3 <= 3");   add("le_false", "5 <= 3");
    add("ge_true", "5 >= 3");   add("ge_eq", "3 >= 3");   add("ge_false", "3 >= 5");
    add("eq_true", "5 == 5");   add("eq_false", "5 == 3");
    add("neq_true", "5 != 3");  add("neq_false", "5 != 5");

    // Logic
    add("and_11", "1 && 1");  add("and_10", "1 && 0");
    add("and_01", "0 && 1");  add("and_00", "0 && 0");
    add("or_10",  "1 || 0");  add("or_01",  "0 || 1");
    add("or_11",  "1 || 1");  add("or_00",  "0 || 0");
    add("not_0", "!0");       add("not_1", "!1");       add("not_5", "!5");
    add("not_and", "!0 && 1"); add("not_or", "!1 || 1");
    add("not_cmp", "!(3 > 5)"); add("not_cmp2", "!(5 > 3)");

    // Ternary
    add("ternary_true", "1 ? 10 : 20");    add("ternary_false", "0 ? 10 : 20");
    add("ternary_cond", "x > 3 ? 100 : 200", {{"x", 5.0}});
    add("ternary_cond2", "x > 3 ? 100 : 200", {{"x", 1.0}});
    add("ternary_nest1", "0 ? 1 : 1 ? 2 : 3");
    add("ternary_nest2", "0 ? 1 : 0 ? 2 : 3");
    add("ternary_nest3", "1 ? 1 : 1 ? 2 : 3");

    // Functions
    add("sin_zero", "sin(0)");       add("sin_t", "sin(t)", {{"t", 0.0}});
    add("cos_zero", "cos(0)");       add("cos_t", "cos(t)", {{"t", 0.0}});
    add("abs_neg", "abs(-5)");       add("sqrt_4", "sqrt(4)");
    add("min_ab", "min(3, 7)");     add("max_ab", "max(3, 7)");
    add("clamp_hi", "clamp(5, 0, 3)"); add("clamp_lo", "clamp(-1, 0, 3)");
    add("clamp_mid", "clamp(2, 0, 3)");
    add("asin_0", "asin(0)");        add("acos_1", "acos(1)");
    add("atan_0", "atan(0)");        add("atan2_11", "atan2(1,1)");
    add("exp_0", "exp(0)");          add("log_1", "log(1)");
    add("log10_1000", "log10(1000)");
    add("ceil_23", "ceil(2.3)");    add("floor_27", "floor(2.7)");
    add("round_25", "round(2.5)");   add("round_24", "round(2.4)");
    add("trunc_27", "trunc(2.7)");   add("trunc_neg", "trunc(-2.7)");
    add("sign_neg", "sign(-5)");     add("sign_pos", "sign(5)");
    add("sign_zero", "sign(0)");     add("pow_2_10", "pow(2, 10)");

    // AE remapping — these use custom functions not in ExprTk
    add("linear_3", "linear(0.5, 0, 100)");
    add("linear_3a", "linear(0, 0, 100)");
    add("linear_3b", "linear(1, 0, 100)");
    add("linear_5", "linear(50, 0, 100, 0, 1)");
    add("ease_3", "ease(0.5, 0, 100)");
    add("ease_3a", "ease(0, 0, 100)");
    add("ease_3b", "ease(1, 0, 100)");
    add("easeIn_half", "easeIn(0.5, 0, 100)");
    add("easeOut_half", "easeOut(0.5, 0, 100)");

    // Degrees/radians — use ExprTk's deg2rad/rad2deg
    add("deg2rad_180", "degreesToRadians(180)");
    add("rad2deg_pi", "radiansToDegrees(3.14159265358979)");
    add("deg2rad_90", "degreesToRadians(90)");

    // Constants
    add("pi", "PI"); add("e", "E"); add("true_c", "true"); add("false_c", "false");

    // Context variables
    {
        chronon3d::math::ExpressionContext cx;
        cx.frame = 30; cx.time = 1.0; cx.fps = 30;
        cx.width = 1920; cx.height = 1080; cx.index = 5;
        cx.num_layers = 10; cx.value = 42;
        c.push_back({"ctx_frame", "frame", {}, cx, true});
        c.push_back({"ctx_time", "time", {}, cx, true});
        c.push_back({"ctx_fps", "fps", {}, cx, true});
        c.push_back({"ctx_width", "width", {}, cx, true});
        c.push_back({"ctx_height", "height", {}, cx, true});
        c.push_back({"ctx_index", "index", {}, cx, true});
        c.push_back({"ctx_numLayers", "numLayers", {}, cx, true});
        c.push_back({"ctx_value", "value", {}, cx, true});
    }

    // thisComp/thisLayer/thisProperty
    {
        chronon3d::math::ExpressionContext cx;
        cx.width = 1920; cx.height = 1080; cx.num_layers = 5;
        cx.index = 3; cx.in_point = 0.5; cx.out_point = 5.0;
        cx.width_0 = 1920; cx.height_0 = 1080; cx.value = 75;
        cx.value0 = 10; cx.value1 = 20; cx.value2 = 30;
        c.push_back({"comp_width", "thisComp.width", {}, cx, true});
        c.push_back({"comp_height", "thisComp.height", {}, cx, true});
        c.push_back({"comp_numL", "thisComp.numLayers", {}, cx, true});
        c.push_back({"layer_idx", "thisLayer.index", {}, cx, true});
        c.push_back({"layer_in", "thisLayer.inPoint", {}, cx, true});
        c.push_back({"layer_out", "thisLayer.outPoint", {}, cx, true});
        c.push_back({"layer_w", "thisLayer.width", {}, cx, true});
        c.push_back({"layer_h", "thisLayer.height", {}, cx, true});
        c.push_back({"layer_opac", "thisLayer.opacity", {}, cx, true});
        c.push_back({"prop_val", "thisProperty.value", {}, cx, true});
        c.push_back({"prop_v0", "thisProperty.value0", {}, cx, true});
        c.push_back({"prop_v1", "thisProperty.value1", {}, cx, true});
        c.push_back({"prop_v2", "thisProperty.value2", {}, cx, true});
    }

    // layer('name').prop
    {
        chronon3d::math::ExpressionContext cx;
        cx.time = 1.0; cx.value = 50;
        cx.layer_resolver = make_layer_resolver("bg", "opacity", 0.8);
        c.push_back({"layer_prop", "layer('bg').opacity", {}, cx, true, true});
        cx.layer_resolver = make_layer_resolver("bg", "position.x", 100.0);
        c.push_back({"layer_posx", "layer('bg').position.x", {}, cx, true, true});
    }

    // Complex
    add("cmp_ternary", "frame > 30 ? 100 : 0", {{"frame", 60.0}});
    add("cmp_ternary2", "frame > 30 ? 100 : 0", {{"frame", 10.0}});
    add("prec_chain", "2 + 3 * 4 > 10 && 1 || 0");
    add("real_world", "sin(time * 2) * 100 + 500", {{"time", 0.0}});

    // Error: undefined var / bad parens
    add("undef_var", "undefined_var");
    add("bad_paren", "sin(1.0");

    return c;
}

// ── Dual-run engine ──────────────────────────────────────────────────

struct Result {
    double custom_v, exprtk_v;
    bool custom_ok, exprtk_ok;
};

Result dual_run(const TestCase& tc) {
    using namespace chronon3d::math;

    // Custom
    double cv; bool cok;
    if (tc.uses_context) {
        ExpressionState st;
        cv = evaluate_expression(tc.expression, tc.ctx, tc.vars, st, -999.0);
        cok = (cv != -999.0 || tc.name == std::string_view("undef_var") || tc.name == std::string_view("bad_paren"));
    } else {
        ExpressionParser p(tc.expression, tc.vars);
        cv = p.parse();
        cok = !p.error();
    }

    // For error cases, both should fail
    if (std::strcmp(tc.name, "undef_var") == 0 || std::strcmp(tc.name, "bad_paren") == 0) {
        cok = false;
        cv = -999.0;
    }

    // ExprTk
    double ev; bool eok;
    ExpressionCompiler compiler(tc.expression);
    if (tc.uses_context) compiler.set_context(tc.ctx);
    compiler.set_legacy_vars(tc.vars);
    eok = !compiler.error();
    if (eok) {
        ev = compiler.evaluate();
    } else {
        ev = -999.0;
    }

    return {cv, ev, cok, eok};
}

} // anonymous namespace

int main() {
    auto corpus = build_corpus();
    int total = 0, pass = 0, partial = 0, fail = 0;
    int compile_fail = 0, mismatch = 0;

    std::printf("%-22s %6s %10s %10s %10s %10s\n",
                "TEST", "EXPRTK", "CUSTOM", "EXPRTK_V", "CUSTOM_V", "STATUS");
    std::printf("%s\n", std::string(82, '-').c_str());

    for (const auto& tc : corpus) {
        ++total;
        auto r = dual_run(tc);

        const char* status;
        bool is_pass = false;
        bool is_partial = false;

        if (!r.custom_ok && !r.exprtk_ok) {
            status = "PASS(err)"; is_pass = true;
        } else if (!r.exprtk_ok && r.custom_ok) {
            status = "PARTIAL(xk-err)"; is_partial = true; ++compile_fail;
        } else if (r.exprtk_ok && !r.custom_ok) {
            status = "PARTIAL(cu-err)"; is_partial = true; ++compile_fail;
        } else {
            double d = std::abs(r.custom_v - r.exprtk_v);
            bool ok = (std::isinf(r.custom_v) && std::isinf(r.exprtk_v)) ||
                      (std::isnan(r.custom_v) && std::isnan(r.exprtk_v)) ||
                      (d < 1e-6);
            if (ok) {
                status = "PASS"; is_pass = true;
            } else {
                status = "FAIL(mismatch)"; ++mismatch;
            }
        }

        if (is_pass) ++pass; else if (is_partial) ++partial; else ++fail;

        std::printf("%-22s %6s %10.4f %10.4f %10.4f %10s\n",
                    tc.name,
                    r.exprtk_ok ? "OK" : "ERR",
                    r.custom_ok ? r.custom_v : 0.0,
                    r.exprtk_ok ? r.exprtk_v : 0.0,
                    r.custom_ok ? r.custom_v : 0.0,
                    status);
    }

    std::printf("\n%s\n", std::string(82, '-').c_str());
    std::printf("TOTAL=%d  PASS=%d  PARTIAL=%d  FAIL=%d\n", total, pass, partial, fail);
    std::printf("  compile-errors=%d  value-mismatches=%d\n", compile_fail, mismatch);
    return fail > 0 ? 1 : 0;
}
