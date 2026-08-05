#pragma once

// Minimal named-check harness. Deliberately dependency-free so the unit tests
// build and run in CI without gtest, ROS or a GPU.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace test_support
{

inline int &failureCount()
{
    static int failures = 0;
    return failures;
}

inline const char *&currentCase()
{
    static const char *name = "<none>";
    return name;
}

inline void beginCase(const char *name)
{
    currentCase() = name;
    std::printf("[ RUN      ] %s\n", name);
}

inline void expect(bool condition, const char *expr, const char *file, int line)
{
    if (condition)
        return;
    failureCount()++;
    std::printf("[  FAILED  ] %s\n            %s:%d: %s\n", currentCase(), file, line, expr);
}

inline void expectNear(double lhs, double rhs, double eps, const char *expr,
                       const char *file, int line)
{
    if (std::abs(lhs - rhs) <= eps)
        return;
    failureCount()++;
    std::printf("[  FAILED  ] %s\n            %s:%d: %s (%.17g vs %.17g)\n",
                currentCase(), file, line, expr, lhs, rhs);
}

inline int summarize()
{
    if (failureCount() == 0)
    {
        std::printf("[  PASSED  ] all checks\n");
        return 0;
    }
    std::printf("[  FAILED  ] %d check(s)\n", failureCount());
    return 1;
}

}  // namespace test_support

#define TEST_CASE(name) test_support::beginCase(name)
#define CHECK(cond) test_support::expect((cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) \
    test_support::expectNear((a), (b), (eps), #a " ~= " #b, __FILE__, __LINE__)
#define TEST_MAIN_RETURN() return test_support::summarize()
