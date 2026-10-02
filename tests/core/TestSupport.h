#pragma once

// A tiny test harness shared by the test files: CHECK macros and a registry of test cases.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace testing
{
    struct TestCase { const char* name; void (*fn)(); };

    inline int failures = 0;
    inline int checks = 0;
    inline const char* currentTest = "";

    inline std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    struct Registrar
    {
        Registrar (const char* name, void (*fn)()) { registry().push_back ({ name, fn }); }
    };

    inline void report (bool ok, const char* file, int line, const std::string& what)
    {
        ++checks;

        if (! ok)
        {
            ++failures;
            std::printf ("  FAIL [%s] %s:%d  %s\n", currentTest, file, line, what.c_str());
        }
    }
}

#define CHECK(cond) testing::report ((cond), __FILE__, __LINE__, #cond)
#define CHECK_EQ(a, b) testing::report ((a) == (b), __FILE__, __LINE__, std::string (#a " == " #b "  (") + std::to_string (a) + " vs " + std::to_string (b) + ")")
#define CHECK_NEAR(a, b, tol) testing::report (std::abs ((a) - (b)) <= (tol), __FILE__, __LINE__, std::string (#a " ~= " #b "  (") + std::to_string (a) + " vs " + std::to_string (b) + ")")
#define CHECK_STR(a, b) testing::report (std::string (a) == std::string (b), __FILE__, __LINE__, std::string (#a " == " #b "  (") + std::string (a) + " vs " + std::string (b) + ")")
