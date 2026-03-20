// Unit tests for window pure logic (no SDL/GL required)

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "window.h"

static int tests_run = 0;
static int tests_passed = 0;

static void check(const char * name, bool condition) {
    tests_run++;
    if (condition) {
        tests_passed++;
    } else {
        fprintf(stderr, "FAIL: %s\n", name);
    }
}

int main() {
    // --- State-to-title mapping ---
    check("idle title",         strcmp(window_title(AppState::IDLE), "Whisper Typer") == 0);
    check("recording title",    strcmp(window_title(AppState::RECORDING), "[REC] Whisper Typer") == 0);
    check("transcribing title", strcmp(window_title(AppState::TRANSCRIBING), "[...] Whisper Typer") == 0);

    // --- History line parsing ---
    {
        HistoryEntry e;
        bool ok = parse_history_line(
            R"({"ts":"2025-03-18T10:30:00Z","text":"hello world","duration_ms":2500})", e);
        check("parse valid line",       ok);
        check("parse ts",              e.timestamp == "2025-03-18T10:30:00Z");
        check("parse text",            e.text == "hello world");
        check("parse duration",        e.duration_ms == 2500);
    }
    {
        HistoryEntry e;
        check("parse empty line",      !parse_history_line("", e));
        check("parse garbage",         !parse_history_line("not json", e));
        check("parse incomplete json", !parse_history_line(R"({"ts":"x"})", e));
    }
    {
        // Text with escaped characters
        HistoryEntry e;
        bool ok = parse_history_line(
            R"({"ts":"2025-01-01T00:00:00Z","text":"say \"hello\"","duration_ms":100})", e);
        check("parse escaped quotes",  ok);
        check("escaped text value",    e.text == "say \"hello\"");
    }

    // --- parse_history (multi-line, reversed) ---
    {
        std::string content =
            R"({"ts":"2025-03-18T09:00:00Z","text":"first","duration_ms":1000})"  "\n"
            R"({"ts":"2025-03-18T10:00:00Z","text":"second","duration_ms":2000})" "\n"
            R"({"ts":"2025-03-18T11:00:00Z","text":"third","duration_ms":3000})"  "\n";
        auto entries = parse_history(content);
        check("parse_history count",   entries.size() == 3);
        check("newest first",          entries[0].text == "third");
        check("oldest last",           entries[2].text == "first");
    }
    {
        auto entries = parse_history("");
        check("empty content",         entries.empty());
    }
    {
        // Malformed lines should be skipped
        std::string content =
            R"({"ts":"2025-01-01T00:00:00Z","text":"good","duration_ms":100})" "\n"
            "bad line\n"
            R"({"ts":"2025-01-02T00:00:00Z","text":"also good","duration_ms":200})" "\n";
        auto entries = parse_history(content);
        check("skip malformed lines",  entries.size() == 2);
    }

    // --- format_duration ---
    check("format 0ms",    format_duration(0) == "0.0s");
    check("format 500ms",  format_duration(500) == "0.5s");
    check("format 2500ms", format_duration(2500) == "2.5s");
    check("format 10000ms", format_duration(10000) == "10.0s");
    check("format 61000ms", format_duration(61000) == "61.0s");

    // --- autostart_desktop_path ---
    {
        std::string path = autostart_desktop_path();
        check("autostart path not empty",    !path.empty());
        check("autostart path ends .desktop", path.rfind(".desktop") == path.size() - 8);
        check("autostart path has whisper",   path.find("whisper-typer") != std::string::npos);
    }

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
