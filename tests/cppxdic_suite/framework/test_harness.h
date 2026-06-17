/**
 * @file test_harness.h
 * @brief Minimal in-tree unit-test harness for CPPxDIC (zero external dependencies).
 *
 * This is a deliberately tiny assertion + registration framework so the test suite needs
 * no GoogleTest / Catch2 (honouring the project's "prefer in-tree, no heavy deps" rule).
 * It integrates with CTest: each test executable that includes this header and calls
 * `cppxdic::test::run_all()` from `main` returns non-zero on any failure, which CTest reports.
 *
 * Usage:
 * @code
 *   #include "test_harness.h"
 *   TEST(my_group, does_a_thing) {
 *       CHECK_EQ(1 + 1, 2);
 *       CHECK_TRUE(some_condition);
 *   }
 *   TEST_MAIN()   // expands to a main() that runs every registered TEST
 * @endcode
 *
 * Macros:
 *   - TEST(group, name)        register a test case.
 *   - CHECK_TRUE(x)            non-fatal: record failure, continue the test.
 *   - CHECK_FALSE(x)
 *   - CHECK_EQ(a, b)
 *   - CHECK_NE(a, b)
 *   - CHECK_NEAR(a, b, tol)    floating-point comparison within an absolute tolerance.
 *   - REQUIRE_TRUE(x)          fatal: throw to abort the current test immediately.
 *   - FAIL_TEST(msg)           unconditional failure with a message.
 *   - SKIP_TEST(reason)        mark the current test SKIPPED (counts as passed, prints reason).
 */

#pragma once

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace cppxdic {
namespace test {

/// Thrown by REQUIRE_* / SKIP_TEST to unwind the current test body.
/// NOTE: the message field is deliberately NOT named `reason` — some third-party headers
/// (e.g. pulled in via OpenCV) define `reason` as a macro, which would corrupt `a.reason`.
struct TestAbort {
    bool skipped = false;
    std::string message;
};

/// State for a single test invocation.
struct TestContext {
    int failures = 0;
    bool skipped = false;
    std::string skip_reason;
};

/// A registered test case.
struct TestCase {
    std::string group;
    std::string name;
    std::function<void(TestContext&)> fn;
};

/// Global registry (function-local static to avoid static-init ordering issues).
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

/// Helper used by the TEST macro to register at static-init time.
struct Registrar {
    Registrar(const std::string& group, const std::string& name,
              std::function<void(TestContext&)> fn) {
        registry().push_back(TestCase{group, name, std::move(fn)});
    }
};

/// Run every registered test. Returns process exit code (0 == all passed).
inline int run_all() {
    int passed = 0, failed = 0, skipped = 0;
    std::vector<std::string> failed_names;
    for (auto& tc : registry()) {
        TestContext ctx;
        const std::string full = tc.group + "." + tc.name;
        try {
            tc.fn(ctx);
        } catch (const TestAbort& a) {
            if (a.skipped) {
                ctx.skipped = true;
                ctx.skip_reason = a.message;
            } else {
                ctx.failures++;
            }
        } catch (const std::exception& e) {
            std::cerr << "  [EXCEPTION] " << full << ": " << e.what() << "\n";
            ctx.failures++;
        } catch (...) {
            std::cerr << "  [EXCEPTION] " << full << ": unknown\n";
            ctx.failures++;
        }
        if (ctx.skipped) {
            std::cout << "  [SKIP] " << full << " - " << ctx.skip_reason << "\n";
            skipped++;
        } else if (ctx.failures == 0) {
            std::cout << "  [PASS] " << full << "\n";
            passed++;
        } else {
            std::cout << "  [FAIL] " << full << " (" << ctx.failures << " check(s) failed)\n";
            failed++;
            failed_names.push_back(full);
        }
    }
    std::cout << "\n==== " << passed << " passed, " << failed << " failed, " << skipped
              << " skipped ====\n";
    for (const auto& n : failed_names) std::cout << "  FAILED: " << n << "\n";
    return failed == 0 ? 0 : 1;
}

} // namespace test
} // namespace cppxdic

// ----------------------------------------------------------------------------
// Registration macro
// ----------------------------------------------------------------------------
#define CPPXDIC_TEST_CAT2(a, b) a##b
#define CPPXDIC_TEST_CAT(a, b) CPPXDIC_TEST_CAT2(a, b)

#define TEST(group, name)                                                                          \
    static void CPPXDIC_TEST_CAT(cppxdic_test_fn_, __LINE__)(::cppxdic::test::TestContext & _ctx); \
    static ::cppxdic::test::Registrar CPPXDIC_TEST_CAT(cppxdic_test_reg_, __LINE__)(               \
        #group, #name, &CPPXDIC_TEST_CAT(cppxdic_test_fn_, __LINE__));                             \
    static void CPPXDIC_TEST_CAT(cppxdic_test_fn_, __LINE__)(::cppxdic::test::TestContext & _ctx)

// ----------------------------------------------------------------------------
// Assertion macros (use _ctx provided by the TEST body)
// ----------------------------------------------------------------------------
#define CPPXDIC_REPORT_FAIL(msg_expr)                                                  \
    do {                                                                               \
        std::ostringstream _oss;                                                       \
        _oss << msg_expr;                                                              \
        std::cerr << "    assertion failed at " << __FILE__ << ":" << __LINE__ << ": " \
                  << _oss.str() << "\n";                                               \
        _ctx.failures++;                                                               \
    } while (0)

#define CHECK_TRUE(x)                                        \
    do {                                                     \
        if (!(x)) CPPXDIC_REPORT_FAIL("CHECK_TRUE(" #x ")"); \
    } while (0)

#define CHECK_FALSE(x)                                     \
    do {                                                   \
        if (x) CPPXDIC_REPORT_FAIL("CHECK_FALSE(" #x ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        auto _va = (a);                                                                  \
        auto _vb = (b);                                                                  \
        if (!(_va == _vb))                                                               \
            CPPXDIC_REPORT_FAIL("CHECK_EQ(" #a ", " #b ") -> " << _va << " != " << _vb); \
    } while (0)

#define CHECK_NE(a, b)                                                                      \
    do {                                                                                    \
        auto _va = (a);                                                                     \
        auto _vb = (b);                                                                     \
        if (!(_va != _vb)) CPPXDIC_REPORT_FAIL("CHECK_NE(" #a ", " #b ") -> both " << _va); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                      \
    do {                                                                                           \
        double _va = static_cast<double>(a);                                                       \
        double _vb = static_cast<double>(b);                                                       \
        double _t = static_cast<double>(tol);                                                      \
        if (std::fabs(_va - _vb) > _t)                                                             \
            CPPXDIC_REPORT_FAIL("CHECK_NEAR(" #a ", " #b ", " #tol ") -> |" << _va << " - " << _vb \
                                                                            << "| > " << _t);      \
    } while (0)

#define REQUIRE_TRUE(x)                                  \
    do {                                                 \
        if (!(x)) {                                      \
            CPPXDIC_REPORT_FAIL("REQUIRE_TRUE(" #x ")"); \
            throw ::cppxdic::test::TestAbort{};          \
        }                                                \
    } while (0)

#define FAIL_TEST(msg)            \
    do {                          \
        CPPXDIC_REPORT_FAIL(msg); \
    } while (0)

#define SKIP_TEST(msg)                 \
    do {                               \
        ::cppxdic::test::TestAbort _a; \
        _a.skipped = true;             \
        _a.message = (msg);            \
        throw _a;                      \
    } while (0)

#define TEST_MAIN()                        \
    int main() {                           \
        return ::cppxdic::test::run_all(); \
    }
