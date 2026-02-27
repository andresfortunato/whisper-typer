// Unit tests for is_terminal_class() and detect_display_backend() from text-output.cpp
//
// Include the source directly to access static/private functions.
// We use a preprocessor trick to access private members for testing.

#define private public
#include "text-output.h"
#undef private

#include "text-output.cpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %s ... ", #name);

#define PASS() \
        tests_passed++; \
        printf("ok\n"); \
    } while(0)

void test_is_terminal_class() {
    // Known terminals
    TEST(alacritty) {
        assert(TextOutput::is_terminal_class("alacritty") == true);
    } PASS();

    TEST(kitty) {
        assert(TextOutput::is_terminal_class("kitty") == true);
    } PASS();

    TEST(gnome_terminal) {
        assert(TextOutput::is_terminal_class("gnome-terminal") == true);
    } PASS();

    TEST(xterm) {
        assert(TextOutput::is_terminal_class("xterm") == true);
    } PASS();

    TEST(foot) {
        assert(TextOutput::is_terminal_class("foot") == true);
    } PASS();

    TEST(ghostty) {
        assert(TextOutput::is_terminal_class("ghostty") == true);
    } PASS();

    TEST(wezterm) {
        assert(TextOutput::is_terminal_class("wezterm") == true);
    } PASS();

    // Case insensitivity
    TEST(case_insensitive_alacritty) {
        assert(TextOutput::is_terminal_class("Alacritty") == true);
    } PASS();

    TEST(case_insensitive_kitty) {
        assert(TextOutput::is_terminal_class("KITTY") == true);
    } PASS();

    // Unknown / non-terminals
    TEST(firefox) {
        assert(TextOutput::is_terminal_class("firefox") == false);
    } PASS();

    TEST(chrome) {
        assert(TextOutput::is_terminal_class("google-chrome") == false);
    } PASS();

    TEST(empty) {
        assert(TextOutput::is_terminal_class("") == false);
    } PASS();

    TEST(random_text) {
        assert(TextOutput::is_terminal_class("not-a-terminal") == false);
    } PASS();
}

void test_detect_display_backend() {
    // Save original values
    const char * orig_wayland = getenv("WAYLAND_DISPLAY");
    const char * orig_display = getenv("DISPLAY");

    TEST(wayland_detected) {
        setenv("WAYLAND_DISPLAY", "wayland-0", 1);
        setenv("DISPLAY", ":0", 1);
        assert(detect_display_backend() == DisplayBackend::WAYLAND);
    } PASS();

    TEST(x11_detected) {
        unsetenv("WAYLAND_DISPLAY");
        setenv("DISPLAY", ":0", 1);
        assert(detect_display_backend() == DisplayBackend::X11);
    } PASS();

    TEST(unknown_detected) {
        unsetenv("WAYLAND_DISPLAY");
        unsetenv("DISPLAY");
        assert(detect_display_backend() == DisplayBackend::UNKNOWN);
    } PASS();

    // Restore original values
    if (orig_wayland) setenv("WAYLAND_DISPLAY", orig_wayland, 1);
    else unsetenv("WAYLAND_DISPLAY");
    if (orig_display) setenv("DISPLAY", orig_display, 1);
    else unsetenv("DISPLAY");
}

int main() {
    printf("test_terminal:\n");

    test_is_terminal_class();
    test_detect_display_backend();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
