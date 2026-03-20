#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AppWindowImpl;

// A single parsed history entry
struct HistoryEntry {
    std::string timestamp;  // ISO 8601 UTC
    std::string text;
    int         duration_ms = 0;
};

// Pure function: parse a single JSONL line into a HistoryEntry.
// Returns true on success, false if the line is malformed.
bool parse_history_line(const std::string & line, HistoryEntry & out);

// Pure function: parse entire JSONL content into entries (newest first).
std::vector<HistoryEntry> parse_history(const std::string & content);

// Pure function: format a duration in ms to a human-readable string (e.g. "2.5s")
std::string format_duration(int duration_ms);

// Pure function: resolve the autostart desktop file path
std::string autostart_desktop_path();

enum class AppState { IDLE, RECORDING, TRANSCRIBING };

// Pure function: map state to window title string
inline const char * window_title(AppState state) {
    switch (state) {
        case AppState::IDLE:         return "Whisper Typer";
        case AppState::RECORDING:    return "[REC] Whisper Typer";
        case AppState::TRANSCRIBING: return "[...] Whisper Typer";
    }
    return "Whisper Typer";
}

struct WindowCallbacks {
    std::function<void()> on_toggle;
    std::function<void()> on_quit;
    std::function<std::string()> get_last_transcript;
    std::function<std::string()> get_history_path;
};

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    AppWindow(const AppWindow &) = delete;
    AppWindow & operator=(const AppWindow &) = delete;

    bool init(const WindowCallbacks & cb);
    void poll();
    void set_state(AppState state);
    void set_last_transcript(const std::string & text);
    void set_hotkey(const std::string & hotkey);
    void show();
    void hide();
    bool is_visible() const;
    void shutdown();

private:
    std::unique_ptr<AppWindowImpl> m_impl;
};
