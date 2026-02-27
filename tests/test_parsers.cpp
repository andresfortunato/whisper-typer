// Unit tests for parse_int() and parse_float() from typer.cpp

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Duplicate the parse functions here (they're small and static in typer.cpp)
static bool parse_int(const char * s, int32_t & out) {
    try {
        size_t pos = 0;
        out = std::stoi(s, &pos);
        if (pos != strlen(s)) {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

static bool parse_float(const char * s, float & out) {
    try {
        size_t pos = 0;
        out = std::stof(s, &pos);
        if (pos != strlen(s)) {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

static int tests_run = 0;
static int tests_passed = 0;

static void check(const char * name, bool condition) {
    tests_run++;
    printf("  %s ... %s\n", name, condition ? "ok" : "FAILED");
    if (condition) tests_passed++;
    assert(condition);
}

void test_parse_int() {
    int32_t val = 0;

    check("parse_int_valid",          parse_int("42", val) && val == 42);
    check("parse_int_negative",       parse_int("-7", val) && val == -7);
    check("parse_int_zero",           parse_int("0", val) && val == 0);
    check("parse_int_trailing_text",  !parse_int("42abc", val));
    check("parse_int_empty",          !parse_int("", val));
    check("parse_int_float_string",   !parse_int("3.14", val));
    check("parse_int_trailing_space", !parse_int("42 ", val));
}

void test_parse_float() {
    float val = 0;

    check("parse_float_valid",         parse_float("3.14", val) && val > 3.13f && val < 3.15f);
    check("parse_float_integer",       parse_float("42", val) && val > 41.9f && val < 42.1f);
    check("parse_float_negative",      parse_float("-0.5", val) && val > -0.51f && val < -0.49f);
    check("parse_float_zero",          parse_float("0.0", val) && val > -0.01f && val < 0.01f);
    check("parse_float_trailing_text", !parse_float("3.14xyz", val));
    check("parse_float_empty",         !parse_float("", val));
}

int main() {
    printf("test_parsers:\n");

    test_parse_int();
    test_parse_float();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
