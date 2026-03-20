// Unit tests for keymap_lookup_char() — the US-QWERTY keymap
//
// Tests pure logic only (no /dev/uinput access needed).

#include "keymap.h"

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

// ── Lowercase letters ──────────────────────────────────────────────

void test_lowercase_letters() {
    printf("lowercase letters:\n");
    check("a → KEY_A, no shift",  keymap_lookup_char('a').keycode == KEY_A && !keymap_lookup_char('a').shift);
    check("z → KEY_Z, no shift",  keymap_lookup_char('z').keycode == KEY_Z && !keymap_lookup_char('z').shift);
    check("m → KEY_M, no shift",  keymap_lookup_char('m').keycode == KEY_M && !keymap_lookup_char('m').shift);
}

// ── Uppercase letters ──────────────────────────────────────────────

void test_uppercase_letters() {
    printf("uppercase letters:\n");
    check("A → KEY_A, shift",     keymap_lookup_char('A').keycode == KEY_A && keymap_lookup_char('A').shift);
    check("Z → KEY_Z, shift",     keymap_lookup_char('Z').keycode == KEY_Z && keymap_lookup_char('Z').shift);
    check("M → KEY_M, shift",     keymap_lookup_char('M').keycode == KEY_M && keymap_lookup_char('M').shift);
}

// ── Digits ─────────────────────────────────────────────────────────

void test_digits() {
    printf("digits:\n");
    check("0 → KEY_0, no shift",  keymap_lookup_char('0').keycode == KEY_0 && !keymap_lookup_char('0').shift);
    check("1 → KEY_1, no shift",  keymap_lookup_char('1').keycode == KEY_1 && !keymap_lookup_char('1').shift);
    check("9 → KEY_9, no shift",  keymap_lookup_char('9').keycode == KEY_9 && !keymap_lookup_char('9').shift);
    check("5 → KEY_5, no shift",  keymap_lookup_char('5').keycode == KEY_5 && !keymap_lookup_char('5').shift);
}

// ── Shifted digit symbols ──────────────────────────────────────────

void test_shifted_digits() {
    printf("shifted digit symbols:\n");
    check("! → KEY_1, shift",     keymap_lookup_char('!').keycode == KEY_1 && keymap_lookup_char('!').shift);
    check("@ → KEY_2, shift",     keymap_lookup_char('@').keycode == KEY_2 && keymap_lookup_char('@').shift);
    check("# → KEY_3, shift",     keymap_lookup_char('#').keycode == KEY_3 && keymap_lookup_char('#').shift);
    check("$ → KEY_4, shift",     keymap_lookup_char('$').keycode == KEY_4 && keymap_lookup_char('$').shift);
    check("% → KEY_5, shift",     keymap_lookup_char('%').keycode == KEY_5 && keymap_lookup_char('%').shift);
    check("^ → KEY_6, shift",     keymap_lookup_char('^').keycode == KEY_6 && keymap_lookup_char('^').shift);
    check("& → KEY_7, shift",     keymap_lookup_char('&').keycode == KEY_7 && keymap_lookup_char('&').shift);
    check("* → KEY_8, shift",     keymap_lookup_char('*').keycode == KEY_8 && keymap_lookup_char('*').shift);
    check("( → KEY_9, shift",     keymap_lookup_char('(').keycode == KEY_9 && keymap_lookup_char('(').shift);
    check(") → KEY_0, shift",     keymap_lookup_char(')').keycode == KEY_0 && keymap_lookup_char(')').shift);
}

// ── Punctuation ────────────────────────────────────────────────────

void test_punctuation() {
    printf("punctuation:\n");
    check(". → KEY_DOT, no shift",       keymap_lookup_char('.').keycode == KEY_DOT && !keymap_lookup_char('.').shift);
    check(", → KEY_COMMA, no shift",     keymap_lookup_char(',').keycode == KEY_COMMA && !keymap_lookup_char(',').shift);
    check("' → KEY_APOSTROPHE, no shift", keymap_lookup_char('\'').keycode == KEY_APOSTROPHE && !keymap_lookup_char('\'').shift);
    check("; → KEY_SEMICOLON, no shift", keymap_lookup_char(';').keycode == KEY_SEMICOLON && !keymap_lookup_char(';').shift);
    check("/ → KEY_SLASH, no shift",     keymap_lookup_char('/').keycode == KEY_SLASH && !keymap_lookup_char('/').shift);
    check("- → KEY_MINUS, no shift",     keymap_lookup_char('-').keycode == KEY_MINUS && !keymap_lookup_char('-').shift);
    check("= → KEY_EQUAL, no shift",     keymap_lookup_char('=').keycode == KEY_EQUAL && !keymap_lookup_char('=').shift);
    check("[ → KEY_LEFTBRACE, no shift", keymap_lookup_char('[').keycode == KEY_LEFTBRACE && !keymap_lookup_char('[').shift);
    check("] → KEY_RIGHTBRACE, no shift", keymap_lookup_char(']').keycode == KEY_RIGHTBRACE && !keymap_lookup_char(']').shift);
    check("\\ → KEY_BACKSLASH, no shift", keymap_lookup_char('\\').keycode == KEY_BACKSLASH && !keymap_lookup_char('\\').shift);
    check("` → KEY_GRAVE, no shift",     keymap_lookup_char('`').keycode == KEY_GRAVE && !keymap_lookup_char('`').shift);
}

// ── Shifted punctuation ────────────────────────────────────────────

void test_shifted_punctuation() {
    printf("shifted punctuation:\n");
    check(": → KEY_SEMICOLON, shift",    keymap_lookup_char(':').keycode == KEY_SEMICOLON && keymap_lookup_char(':').shift);
    check("\" → KEY_APOSTROPHE, shift",  keymap_lookup_char('"').keycode == KEY_APOSTROPHE && keymap_lookup_char('"').shift);
    check("< → KEY_COMMA, shift",        keymap_lookup_char('<').keycode == KEY_COMMA && keymap_lookup_char('<').shift);
    check("> → KEY_DOT, shift",          keymap_lookup_char('>').keycode == KEY_DOT && keymap_lookup_char('>').shift);
    check("? → KEY_SLASH, shift",        keymap_lookup_char('?').keycode == KEY_SLASH && keymap_lookup_char('?').shift);
    check("_ → KEY_MINUS, shift",        keymap_lookup_char('_').keycode == KEY_MINUS && keymap_lookup_char('_').shift);
    check("+ → KEY_EQUAL, shift",        keymap_lookup_char('+').keycode == KEY_EQUAL && keymap_lookup_char('+').shift);
    check("{ → KEY_LEFTBRACE, shift",    keymap_lookup_char('{').keycode == KEY_LEFTBRACE && keymap_lookup_char('{').shift);
    check("} → KEY_RIGHTBRACE, shift",   keymap_lookup_char('}').keycode == KEY_RIGHTBRACE && keymap_lookup_char('}').shift);
    check("| → KEY_BACKSLASH, shift",    keymap_lookup_char('|').keycode == KEY_BACKSLASH && keymap_lookup_char('|').shift);
    check("~ → KEY_GRAVE, shift",        keymap_lookup_char('~').keycode == KEY_GRAVE && keymap_lookup_char('~').shift);
}

// ── Special characters ─────────────────────────────────────────────

void test_special() {
    printf("special:\n");
    check("space → KEY_SPACE, no shift", keymap_lookup_char(' ').keycode == KEY_SPACE && !keymap_lookup_char(' ').shift);
    check("newline → KEY_ENTER, no shift", keymap_lookup_char('\n').keycode == KEY_ENTER && !keymap_lookup_char('\n').shift);
}

// ── Coverage: all printable ASCII mapped ───────────────────────────

void test_all_printable_mapped() {
    printf("coverage:\n");
    int unmapped = 0;
    for (int c = 32; c <= 126; c++) {
        KeyMapping km = keymap_lookup_char(static_cast<char>(c));
        if (km.keycode < 0) {
            printf("    UNMAPPED: 0x%02x '%c'\n", c, c);
            unmapped++;
        }
    }
    check("all printable ASCII (32-126) have mappings", unmapped == 0);
}

// ── Boundary: non-ASCII and control chars ──────────────────────────

void test_unmapped() {
    printf("unmapped:\n");
    check("NUL returns -1",       keymap_lookup_char('\0').keycode == -1);
    check("TAB returns -1",       keymap_lookup_char('\t').keycode == -1);
    check("DEL (0x7F) returns -1", keymap_lookup_char('\x7f').keycode == -1);
    // Non-ASCII (cast to char, unsigned > 127)
    check("0x80 returns -1",      keymap_lookup_char(static_cast<char>(0x80)).keycode == -1);
    check("0xFF returns -1",      keymap_lookup_char(static_cast<char>(0xFF)).keycode == -1);
}

int main() {
    printf("test_uinput_keymap\n");
    printf("==================\n\n");

    test_lowercase_letters();
    test_uppercase_letters();
    test_digits();
    test_shifted_digits();
    test_punctuation();
    test_shifted_punctuation();
    test_special();
    test_all_printable_mapped();
    test_unmapped();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
