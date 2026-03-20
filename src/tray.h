#pragma once

#include <functional>
#include <memory>

enum class TrayState { IDLE, RECORDING, TRANSCRIBING };

struct TrayIconImpl;

struct TrayCallbacks {
    std::function<void()> on_show_window;  // toggle window visibility
    std::function<void()> on_quit;         // exit the app
};

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon & operator=(const TrayIcon &) = delete;

    bool init(const TrayCallbacks & cb);
    void set_state(TrayState state);
    void poll();
    void shutdown();

private:
    std::unique_ptr<TrayIconImpl> m_impl;
};
