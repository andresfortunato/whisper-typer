#pragma once

#include <functional>
#include <memory>
#include <string>

enum class TrayState { IDLE, RECORDING, TRANSCRIBING };

struct TrayIconImpl;  // defined in tray.cpp

struct TrayCallbacks {
    std::function<void()> on_toggle;            // trigger recording via SIGUSR1
    std::function<void()> on_quit;              // set g_running = false
    std::function<std::string()> get_last_transcript;
    std::function<std::string()> get_history_path;
};

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    // Non-copyable
    TrayIcon(const TrayIcon &) = delete;
    TrayIcon & operator=(const TrayIcon &) = delete;

    bool init(const TrayCallbacks & cb);
    void set_state(TrayState state);
    void set_last_transcript(const std::string & text);
    void poll();      // non-blocking GLib iteration
    void shutdown();

private:
    std::unique_ptr<TrayIconImpl> m_impl;
};
