#include <doctest/doctest.h>
#include <chronon3d/core/types/result.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace chronon3d;

// ── Simple error type for testing ──────────────────────────────────────────

struct TestError {
    int code{0};
    std::string message;

    bool operator==(const TestError& o) const = default;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Construction
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Result — construction from value") {
    SUBCASE("implicit from int") {
        Result<int, TestError> r = 42;
        CHECK(r.has_value());
        CHECK(r);
        CHECK_EQ(r.value(), 42);
    }

    SUBCASE("implicit from std::string") {
        Result<std::string, TestError> r = std::string("hello");
        CHECK(r.has_value());
        CHECK_EQ(r.value(), "hello");
    }

    SUBCASE("value accessible after construction") {
        // The [[nodiscard]] attribute on the Result class is a compile-time
        // contract: the compiler warns if a Result is discarded without
        // being used.  This test verifies that a properly-used Result
        // returns the correct value.
        auto r = Result<int, TestError>{100};
        CHECK_EQ(r.value(), 100);
    }
}

TEST_CASE("Result — construction from error") {
    SUBCASE("implicit from TestError") {
        Result<int, TestError> r = TestError{404, "not found"};
        CHECK_FALSE(r.has_value());
        CHECK_FALSE(r);
        CHECK_EQ(r.error().code, 404);
        CHECK_EQ(r.error().message, "not found");
    }

    SUBCASE("multiple error kinds") {
        Result<std::string, TestError> r = TestError{500, "internal"};
        CHECK_FALSE(r.has_value());
        CHECK_EQ(r.error().code, 500);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Move semantics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Result — move constructor") {
    SUBCASE("move value") {
        Result<std::string, TestError> r1 = std::string("movable");
        CHECK(r1.has_value());

        Result<std::string, TestError> r2 = std::move(r1);
        CHECK(r2.has_value());
        CHECK_EQ(r2.value(), "movable");
    }

    SUBCASE("move error") {
        Result<int, TestError> r1 = TestError{403, "forbidden"};
        CHECK_FALSE(r1.has_value());

        Result<int, TestError> r2 = std::move(r1);
        CHECK_FALSE(r2.has_value());
        CHECK_EQ(r2.error().code, 403);
        CHECK_EQ(r2.error().message, "forbidden");
    }
}

TEST_CASE("Result — move assignment") {
    SUBCASE("assign value over value") {
        Result<std::string, TestError> r1 = std::string("first");
        Result<std::string, TestError> r2 = std::string("second");
        r1 = std::move(r2);
        CHECK_EQ(r1.value(), "second");
    }

    SUBCASE("assign error over value") {
        Result<int, TestError> r1 = 10;
        Result<int, TestError> r2 = TestError{1, "err"};
        r1 = std::move(r2);
        CHECK_FALSE(r1.has_value());
        CHECK_EQ(r1.error().code, 1);
    }

    SUBCASE("assign value over error") {
        Result<int, TestError> r1 = TestError{2, "was error"};
        Result<int, TestError> r2 = 99;
        r1 = std::move(r2);
        CHECK(r1.has_value());
        CHECK_EQ(r1.value(), 99);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  value() / error() accessors
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Result — value() accessors") {
    SUBCASE("lvalue reference") {
        Result<int, TestError> r = 7;
        int& v = r.value();
        v = 8;
        CHECK_EQ(r.value(), 8);
    }

    SUBCASE("const lvalue reference") {
        const Result<int, TestError> r = 15;
        CHECK_EQ(r.value(), 15);
    }

    SUBCASE("rvalue — move out") {
        Result<std::string, TestError> r = std::string("move me");
        std::string s = std::move(r).value();
        CHECK_EQ(s, "move me");
    }
}

TEST_CASE("Result — error() accessors") {
    SUBCASE("lvalue reference") {
        Result<int, TestError> r = TestError{401, "unauthorized"};
        TestError& e = r.error();
        e.code = 402;
        CHECK_EQ(r.error().code, 402);
    }

    SUBCASE("rvalue — move out") {
        Result<int, TestError> r = TestError{503, "unavailable"};
        TestError e = std::move(r).error();
        CHECK_EQ(e.code, 503);
        CHECK_EQ(e.message, "unavailable");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  operator-> / operator*
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Result — operator-> / operator*") {
    struct Point {
        int x{0}, y{0};
        bool operator==(const Point& o) const = default;
    };

    SUBCASE("operator->") {
        Result<Point, TestError> r = Point{3, 4};
        CHECK_EQ(r->x, 3);
        CHECK_EQ(r->y, 4);
        r->x = 10;
        CHECK_EQ(r->x, 10);
    }

    SUBCASE("operator* (lvalue)") {
        Result<Point, TestError> r = Point{5, 6};
        Point& p = *r;
        CHECK_EQ(p.x, 5);
        CHECK_EQ(p.y, 6);
        p.y = 7;
        CHECK_EQ(r->y, 7);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CHRONON_TRY macro
// ═══════════════════════════════════════════════════════════════════════════

// Helper: returns Result on success.
static Result<int, TestError> try_success(int x) {
    return x;
}

// Helper: returns Result error.
static Result<int, TestError> try_failure(int code) {
    return TestError{code, "try failed"};
}

// Helper: uses CHRONON_TRY to compose two results.
static Result<int, TestError> compose_with_try(int a, int b) {
    auto va = CHRONON_TRY(try_success(a));
    auto vb = CHRONON_TRY(try_success(b));
    return va + vb;
}

// Helper: CHRONON_TRY propagates error from first call.
static Result<int, TestError> compose_with_try_fail_first(int a, int b) {
    auto va = CHRONON_TRY(try_failure(a));  // should return error
    auto vb = CHRONON_TRY(try_success(b));  // unreachable
    return va + vb;
}

// Helper: CHRONON_TRY propagates error from second call.
static Result<int, TestError> compose_with_try_fail_second(int a, int b) {
    auto va = CHRONON_TRY(try_success(a));
    auto vb = CHRONON_TRY(try_failure(b));  // should return error
    return va + vb;
}

TEST_CASE("CHRONON_TRY — success path") {
    auto r = compose_with_try(3, 4);
    CHECK(r.has_value());
    CHECK_EQ(r.value(), 7);
}

TEST_CASE("CHRONON_TRY — error propagation (first call)") {
    auto r = compose_with_try_fail_first(401, 5);
    CHECK_FALSE(r.has_value());
    CHECK_EQ(r.error().code, 401);
    CHECK_EQ(r.error().message, "try failed");
}

TEST_CASE("CHRONON_TRY — error propagation (second call)") {
    auto r = compose_with_try_fail_second(10, 502);
    CHECK_FALSE(r.has_value());
    CHECK_EQ(r.error().code, 502);
}

TEST_CASE("CHRONON_TRY — nested error types work") {
    // Different error type but same pattern
    struct OtherError { int id{0}; };

    auto make_ok = [](int x) -> Result<int, OtherError> { return x; };
    auto make_err = [](int id) -> Result<int, OtherError> { return OtherError{id}; };

    auto compose = [&](int a, int b) -> Result<int, OtherError> {
        auto va = CHRONON_TRY(make_ok(a));
        auto vb = CHRONON_TRY(make_err(b));
        return va + vb;
    };

    auto r = compose(1, 99);
    CHECK_FALSE(r.has_value());
    CHECK_EQ(r.error().id, 99);
}

// ═══════════════════════════════════════════════════════════════════════════
//  has_value() / explicit operator bool
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Result — has_value() and operator bool") {
    SUBCASE("has_value on success") {
        Result<double, TestError> r = 3.14;
        CHECK(r.has_value());
        CHECK(static_cast<bool>(r));
    }

    SUBCASE("has_value on error") {
        Result<double, TestError> r = TestError{1, "oops"};
        CHECK_FALSE(r.has_value());
        CHECK_FALSE(static_cast<bool>(r));
    }

    SUBCASE("if (!result) pattern") {
        Result<int, TestError> r = TestError{500, "fail"};
        if (!r) {
            CHECK_EQ(r.error().code, 500);
        } else {
            FAIL("Should have entered error branch");
        }
    }

    SUBCASE("if (result) pattern") {
        Result<int, TestError> r = 42;
        if (r) {
            CHECK_EQ(r.value(), 42);
        } else {
            FAIL("Should have entered value branch");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Complex types
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Result — non-trivial types") {
    // Verify that types with non-trivial destructors work correctly.
    // std::vector has a non-trivial destructor.
    SUBCASE("std::vector as value") {
        Result<std::vector<int>, TestError> r = std::vector<int>{1, 2, 3, 4};
        CHECK(r.has_value());
        CHECK_EQ(r.value().size(), 4);
        CHECK_EQ(r.value()[2], 3);
    }

    SUBCASE("std::string in error") {
        Result<int, TestError> r = TestError{400, std::string("bad request") + " details"};
        CHECK_FALSE(r.has_value());
        CHECK_EQ(r.error().message, "bad request details");
    }

    SUBCASE("move vector out") {
        Result<std::vector<int>, TestError> r = std::vector<int>{10, 20, 30};
        auto v = std::move(r).value();
        CHECK_EQ(v.size(), 3);
        CHECK_EQ(v[1], 20);
    }
}
