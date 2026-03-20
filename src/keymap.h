#pragma once

#ifdef __linux__

#include <linux/input-event-codes.h>

// Key mapping: character → (evdev keycode, needs shift)
struct KeyMapping {
    int  keycode;
    bool shift;
};

// ── US-QWERTY static keymap ────────────────────────────────────────
// Maps printable ASCII (32–126) + newline to evdev keycodes.
// {keycode, shift_required}

static constexpr KeyMapping UNMAPPED = {-1, false};

// clang-format off
static constexpr KeyMapping KEYMAP[128] = {
    // 0x00–0x09: control chars (unmapped except newline at 0x0A)
    UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED,
    UNMAPPED, UNMAPPED,
    // 0x0A = '\n' → KEY_ENTER
    {KEY_ENTER, false},
    // 0x0B–0x1F: control chars
    UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED,
    UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED,
    UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED,

    // 0x20 ' '   0x21 '!'   0x22 '"'         0x23 '#'         0x24 '$'
    {KEY_SPACE, false}, {KEY_1, true}, {KEY_APOSTROPHE, true}, {KEY_3, true}, {KEY_4, true},
    // 0x25 '%'      0x26 '&'      0x27 '''             0x28 '('      0x29 ')'
    {KEY_5, true}, {KEY_7, true}, {KEY_APOSTROPHE, false}, {KEY_9, true}, {KEY_0, true},
    // 0x2A '*'      0x2B '+'         0x2C ','          0x2D '-'          0x2E '.'
    {KEY_8, true}, {KEY_EQUAL, true}, {KEY_COMMA, false}, {KEY_MINUS, false}, {KEY_DOT, false},
    // 0x2F '/'
    {KEY_SLASH, false},

    // 0x30–0x39: digits 0–9
    {KEY_0, false}, {KEY_1, false}, {KEY_2, false}, {KEY_3, false}, {KEY_4, false},
    {KEY_5, false}, {KEY_6, false}, {KEY_7, false}, {KEY_8, false}, {KEY_9, false},

    // 0x3A ':'         0x3B ';'              0x3C '<'          0x3D '='
    {KEY_SEMICOLON, true}, {KEY_SEMICOLON, false}, {KEY_COMMA, true}, {KEY_EQUAL, false},
    // 0x3E '>'       0x3F '?'         0x40 '@'
    {KEY_DOT, true}, {KEY_SLASH, true}, {KEY_2, true},

    // 0x41–0x5A: uppercase A–Z (KEY_A–KEY_Z + shift)
    {KEY_A, true}, {KEY_B, true}, {KEY_C, true}, {KEY_D, true}, {KEY_E, true},
    {KEY_F, true}, {KEY_G, true}, {KEY_H, true}, {KEY_I, true}, {KEY_J, true},
    {KEY_K, true}, {KEY_L, true}, {KEY_M, true}, {KEY_N, true}, {KEY_O, true},
    {KEY_P, true}, {KEY_Q, true}, {KEY_R, true}, {KEY_S, true}, {KEY_T, true},
    {KEY_U, true}, {KEY_V, true}, {KEY_W, true}, {KEY_X, true}, {KEY_Y, true},
    {KEY_Z, true},

    // 0x5B '['              0x5C '\'                0x5D ']'               0x5E '^'      0x5F '_'
    {KEY_LEFTBRACE, false}, {KEY_BACKSLASH, false}, {KEY_RIGHTBRACE, false}, {KEY_6, true}, {KEY_MINUS, true},

    // 0x60 '`'
    {KEY_GRAVE, false},

    // 0x61–0x7A: lowercase a–z (KEY_A–KEY_Z, no shift)
    {KEY_A, false}, {KEY_B, false}, {KEY_C, false}, {KEY_D, false}, {KEY_E, false},
    {KEY_F, false}, {KEY_G, false}, {KEY_H, false}, {KEY_I, false}, {KEY_J, false},
    {KEY_K, false}, {KEY_L, false}, {KEY_M, false}, {KEY_N, false}, {KEY_O, false},
    {KEY_P, false}, {KEY_Q, false}, {KEY_R, false}, {KEY_S, false}, {KEY_T, false},
    {KEY_U, false}, {KEY_V, false}, {KEY_W, false}, {KEY_X, false}, {KEY_Y, false},
    {KEY_Z, false},

    // 0x7B '{'              0x7C '|'              0x7D '}'               0x7E '~'
    {KEY_LEFTBRACE, true}, {KEY_BACKSLASH, true}, {KEY_RIGHTBRACE, true}, {KEY_GRAVE, true},

    // 0x7F DEL
    UNMAPPED,
};
// clang-format on

// Lookup a printable ASCII character in the US-QWERTY keymap.
// Returns {-1, false} if unmapped.
inline KeyMapping keymap_lookup_char(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) return UNMAPPED;
    return KEYMAP[uc];
}

#endif // __linux__
