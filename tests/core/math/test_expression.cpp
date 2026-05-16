#include <doctest/doctest.h>
#include <chronon3d/math/expression.hpp>

using namespace chronon3d::math;

TEST_CASE("expression: costante letterale") {
    CHECK(evaluate_expression("42", {}, 0.0) == doctest::Approx(42.0));
    CHECK(evaluate_expression("3.14", {}, 0.0) == doctest::Approx(3.14));
}

TEST_CASE("expression: operatori aritmetici") {
    CHECK(evaluate_expression("2 + 3", {}, 0.0) == doctest::Approx(5.0));
    CHECK(evaluate_expression("10 - 4", {}, 0.0) == doctest::Approx(6.0));
    CHECK(evaluate_expression("3 * 4", {}, 0.0) == doctest::Approx(12.0));
    CHECK(evaluate_expression("10 / 4", {}, 0.0) == doctest::Approx(2.5));
}

TEST_CASE("expression: precedenza e parentesi") {
    CHECK(evaluate_expression("2 + 3 * 4", {}, 0.0) == doctest::Approx(14.0));
    CHECK(evaluate_expression("(2 + 3) * 4", {}, 0.0) == doctest::Approx(20.0));
}

TEST_CASE("expression: variabili frame e time") {
    std::unordered_map<std::string, double> vars{{"frame", 30.0}, {"time", 0.5}};
    CHECK(evaluate_expression("frame", vars, 0.0) == doctest::Approx(30.0));
    CHECK(evaluate_expression("time * 100", vars, 0.0) == doctest::Approx(50.0));
    CHECK(evaluate_expression("frame + time", vars, 0.0) == doctest::Approx(30.5));
}

TEST_CASE("expression: funzioni sin/cos/abs") {
    std::unordered_map<std::string, double> vars{{"t", 0.0}};
    CHECK(evaluate_expression("sin(t)", vars, 0.0) == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(evaluate_expression("cos(t)", vars, 0.0) == doctest::Approx(1.0).epsilon(1e-6));
    CHECK(evaluate_expression("abs(-5)", {}, 0.0) == doctest::Approx(5.0));
}

TEST_CASE("expression: sin(time*2)*100 + 500") {
    // Caso d'uso reale: anima una posizione Y
    std::unordered_map<std::string, double> vars{{"time", 0.0}};
    CHECK(evaluate_expression("sin(time * 2) * 100 + 500", vars, 0.0) == doctest::Approx(500.0));
    vars["time"] = 3.14159265358979 / 4.0; // pi/4 → sin(pi/4 * 2) = sin(pi/2) = 1
    CHECK(evaluate_expression("sin(time * 2) * 100 + 500", vars, 0.0) == doctest::Approx(600.0).epsilon(0.01));
}

TEST_CASE("expression: clamp") {
    CHECK(evaluate_expression("clamp(5, 0, 3)", {}, 0.0) == doctest::Approx(3.0));
    CHECK(evaluate_expression("clamp(-1, 0, 3)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("clamp(2, 0, 3)", {}, 0.0) == doctest::Approx(2.0));
}

TEST_CASE("expression: fallback su errore") {
    // Variabile non definita → fallback
    CHECK(evaluate_expression("undefined_var", {}, -999.0) == doctest::Approx(-999.0));
    // Parentesi mancante → fallback
    CHECK(evaluate_expression("sin(1.0", {}, -1.0) == doctest::Approx(-1.0));
}

TEST_CASE("expression: risultato deterministico su chiamate ripetute") {
    std::unordered_map<std::string, double> vars{{"frame", 15.0}};
    const double first = evaluate_expression("sin(frame) * 50 + 100", vars, 0.0);
    for (int i = 0; i < 1000; ++i) {
        CHECK(evaluate_expression("sin(frame) * 50 + 100", vars, 0.0) == doctest::Approx(first));
    }
}
