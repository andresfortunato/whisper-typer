#pragma once

#include <string>

class TextOutput {
public:
    TextOutput();
    ~TextOutput();

    // Non-copyable, non-movable (no meaningful copy/move semantics)
    TextOutput(const TextOutput &) = delete;
    TextOutput & operator=(const TextOutput &) = delete;

    void set_use_clipboard(bool use_clipboard);
    void set_type_delay_ms(int delay_ms);

    // Type text into the currently focused window
    bool type(const std::string & text);

private:
    bool m_use_clipboard = true;
    int  m_type_delay_ms = 12;

    // Type via xdotool keystroke simulation
    bool type_xdotool(const std::string & text);

    // Type via clipboard paste (xclip + xdotool key ctrl+v)
    bool type_clipboard(const std::string & text);

    // Check if a window class name is a known terminal
    static bool is_terminal_class(const std::string & cls);

    // Run a command with argv, optional stdin, optional stdout capture, with timeout
    // Returns exit status, or -1 on error/timeout
    static int run_cmd(const char * const argv[], int timeout_ms,
                       const std::string * stdin_data = nullptr,
                       std::string * stdout_data = nullptr);
};
