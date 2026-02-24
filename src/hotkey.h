#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Callback: key_down = true on press, false on release
using HotkeyCallback = std::function<void(bool key_down)>;

class HotkeyListener {
public:
    HotkeyListener();
    ~HotkeyListener();

    // Non-copyable, non-movable (owns thread + file descriptors)
    HotkeyListener(const HotkeyListener &) = delete;
    HotkeyListener & operator=(const HotkeyListener &) = delete;

    // Parse hotkey string (e.g. "ctrl+period", "super+v", "ctrl+shift+space")
    bool init(const std::string & hotkey_str);

    // Start listening thread
    bool start(HotkeyCallback callback);

    // Stop listening thread
    void stop();

    // Poll-based interface: returns true once per event, consuming the flag
    bool poll_pressed();
    bool poll_released();

private:
    void listen_thread();
    bool open_keyboards();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::thread      m_thread;
    std::atomic_bool m_running{false};
    std::atomic_bool m_event_press{false};
    std::atomic_bool m_event_release{false};
    HotkeyCallback   m_callback;
};
