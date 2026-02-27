// Unit tests for name_to_evdev() from hotkey.cpp
//
// Include the source file directly to access the static function.

#include "hotkey.h"
#include "hotkey.cpp"

#include <cassert>
#include <cstdio>
#include <linux/input.h>

static int tests_run = 0;
static int tests_passed = 0;

static void check(const char * name, bool condition) {
    tests_run++;
    printf("  %s ... %s\n", name, condition ? "ok" : "FAILED");
    if (condition) tests_passed++;
    assert(condition);
}

void test_name_to_evdev() {
    // Letters
    check("letter_a",              name_to_evdev("a") == KEY_A);
    check("letter_z",              name_to_evdev("z") == KEY_Z);
    check("letter_uppercase",      name_to_evdev("A") == KEY_A);

    // Digits
    check("digit_0",              name_to_evdev("0") == KEY_0);
    check("digit_1",              name_to_evdev("1") == KEY_1);
    check("digit_9",              name_to_evdev("9") == KEY_9);

    // Named keys
    check("space",                name_to_evdev("space") == KEY_SPACE);
    check("period",               name_to_evdev("period") == KEY_DOT);
    check("dot_alias",            name_to_evdev("dot") == KEY_DOT);
    check("dot_char",             name_to_evdev(".") == KEY_DOT);
    check("enter",                name_to_evdev("enter") == KEY_ENTER);
    check("return_alias",         name_to_evdev("return") == KEY_ENTER);
    check("escape",               name_to_evdev("escape") == KEY_ESC);
    check("esc_alias",            name_to_evdev("esc") == KEY_ESC);
    check("tab",                  name_to_evdev("tab") == KEY_TAB);
    check("backspace",            name_to_evdev("backspace") == KEY_BACKSPACE);

    // Plus alias (Issue #23)
    check("plus_alias",           name_to_evdev("plus") == KEY_KPPLUS);
    check("kpplus_alias",         name_to_evdev("kpplus") == KEY_KPPLUS);

    // F-keys
    check("f1",                   name_to_evdev("f1") == KEY_F1);
    check("f10",                  name_to_evdev("f10") == KEY_F10);
    check("f11",                  name_to_evdev("f11") == KEY_F11);
    check("f12",                  name_to_evdev("f12") == KEY_F12);
    check("f13_invalid",          name_to_evdev("f13") == -1);

    // Unknown keys
    check("unknown_key",          name_to_evdev("notakey") == -1);
    check("empty_string",         name_to_evdev("") == -1);

    // Case insensitivity
    check("case_insensitive_space",  name_to_evdev("SPACE") == KEY_SPACE);
    check("case_insensitive_period", name_to_evdev("Period") == KEY_DOT);
}

int main() {
    printf("test_hotkey:\n");

    test_name_to_evdev();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
