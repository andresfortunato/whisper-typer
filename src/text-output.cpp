#include "text-output.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <array>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>

// Timeout for subprocess calls (xclip, xdotool)
static constexpr int CMD_TIMEOUT_MS = 5000;

// Delay after setting clipboard before sending paste keystroke
static constexpr int CLIPBOARD_SET_DELAY_MS = 50;

// Delay after paste before restoring the original clipboard
static constexpr int CLIPBOARD_RESTORE_DELAY_MS = 300;

TextOutput::TextOutput() {}
TextOutput::~TextOutput() {}

void TextOutput::set_use_clipboard(bool use_clipboard) {
    m_use_clipboard = use_clipboard;
}

void TextOutput::set_type_delay_ms(int delay_ms) {
    m_type_delay_ms = delay_ms;
}

bool TextOutput::type(const std::string & text) {
    if (text.empty()) return true;

    if (m_use_clipboard) {
        return type_clipboard(text);
    } else {
        return type_xdotool(text);
    }
}

// Run a command with explicit argv (no shell involved).
// Optionally writes stdin_data to the child's stdin.
// Optionally captures child's stdout into stdout_data.
// Returns child exit status, or -1 on error/timeout.
int TextOutput::run_cmd(const char * const argv[], int timeout_ms,
                        const std::string * stdin_data,
                        std::string * stdout_data) {
    int stdin_pipe[2]  = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (stdin_data) {
        if (pipe(stdin_pipe) < 0) return -1;
    }
    if (stdout_data) {
        if (pipe(stdout_pipe) < 0) {
            if (stdin_data) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (stdin_data)  { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        if (stdout_data) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
        return -1;
    }

    if (pid == 0) {
        // Child process
        if (stdin_data) {
            close(stdin_pipe[1]);
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
        }
        if (stdout_data) {
            close(stdout_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            close(stdout_pipe[1]);
        }
        // Suppress stderr from child commands
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp(argv[0], const_cast<char * const *>(argv));
        _exit(127);
    }

    // Parent process
    if (stdin_data) {
        close(stdin_pipe[0]);
        // Write data to child stdin, ignoring partial writes for simplicity
        const char * data = stdin_data->c_str();
        size_t remaining = stdin_data->size();
        while (remaining > 0) {
            ssize_t written = write(stdin_pipe[1], data, remaining);
            if (written <= 0) break;
            data += written;
            remaining -= written;
        }
        close(stdin_pipe[1]);
    }

    if (stdout_data) {
        close(stdout_pipe[1]);
        stdout_data->clear();
        std::array<char, 4096> buf;
        while (true) {
            ssize_t n = read(stdout_pipe[0], buf.data(), buf.size());
            if (n <= 0) break;
            stdout_data->append(buf.data(), n);
        }
        close(stdout_pipe[0]);
    }

    // Wait for child with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        if (ret < 0) return -1;

        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            fprintf(stderr, "text-output: command timed out after %d ms: %s\n", timeout_ms, argv[0]);
            return -1;
        }
        usleep(10000); // 10ms poll interval
    }
}

bool TextOutput::type_xdotool(const std::string & text) {
    std::string delay_str = std::to_string(m_type_delay_ms);
    const char * argv[] = {
        "xdotool", "type", "--clearmodifiers",
        "--delay", delay_str.c_str(),
        "--", text.c_str(), nullptr
    };

    int ret = run_cmd(argv, CMD_TIMEOUT_MS);
    if (ret != 0) {
        fprintf(stderr, "text-output: xdotool type failed (exit %d)\n", ret);
        return false;
    }
    return true;
}

bool TextOutput::type_clipboard(const std::string & text) {
    // 1. Save current clipboard
    std::string saved_clipboard;
    {
        const char * argv[] = {"xclip", "-selection", "clipboard", "-o", nullptr};
        run_cmd(argv, CMD_TIMEOUT_MS, nullptr, &saved_clipboard);
        // Failure is OK here — clipboard may be empty/unowned
    }

    // 2. Set clipboard to transcribed text
    {
        const char * argv[] = {"xclip", "-selection", "clipboard", nullptr};
        int ret = run_cmd(argv, CMD_TIMEOUT_MS, &text);
        if (ret != 0) {
            fprintf(stderr, "text-output: xclip write failed (exit %d)\n", ret);
            return false;
        }
    }

    // 3. Small delay to ensure clipboard is set
    std::this_thread::sleep_for(std::chrono::milliseconds(CLIPBOARD_SET_DELAY_MS));

    // 4. Get active window ID once and use it for both terminal check and paste
    std::string window_id;
    {
        const char * argv[] = {"xdotool", "getactivewindow", nullptr};
        int ret = run_cmd(argv, CMD_TIMEOUT_MS, nullptr, &window_id);
        if (ret != 0) {
            // Fallback: paste without window targeting
            window_id.clear();
        }
        // Strip trailing newline
        while (!window_id.empty() && (window_id.back() == '\n' || window_id.back() == '\r')) {
            window_id.pop_back();
        }
    }

    // 5. Check if the target window is a terminal
    bool is_terminal = false;
    if (!window_id.empty()) {
        std::string window_class;
        const char * argv[] = {"xdotool", "getwindowclassname", window_id.c_str(), nullptr};
        int ret = run_cmd(argv, CMD_TIMEOUT_MS, nullptr, &window_class);
        if (ret == 0) {
            while (!window_class.empty() && (window_class.back() == '\n' || window_class.back() == '\r')) {
                window_class.pop_back();
            }
            is_terminal = is_terminal_class(window_class);
        }
    }

    // 6. Send paste keystroke to the specific window
    int paste_ret;
    if (!window_id.empty()) {
        if (is_terminal) {
            const char * argv[] = {"xdotool", "key", "--clearmodifiers",
                                   "--window", window_id.c_str(),
                                   "ctrl+shift+v", nullptr};
            paste_ret = run_cmd(argv, CMD_TIMEOUT_MS);
        } else {
            const char * argv[] = {"xdotool", "key", "--clearmodifiers",
                                   "--window", window_id.c_str(),
                                   "ctrl+v", nullptr};
            paste_ret = run_cmd(argv, CMD_TIMEOUT_MS);
        }
    } else {
        // Fallback: no window targeting
        const char * paste_key = is_terminal ? "ctrl+shift+v" : "ctrl+v";
        const char * argv[] = {"xdotool", "key", "--clearmodifiers", paste_key, nullptr};
        paste_ret = run_cmd(argv, CMD_TIMEOUT_MS);
    }

    if (paste_ret != 0) {
        fprintf(stderr, "text-output: paste simulation failed (exit %d)\n", paste_ret);
    }

    // 7. Wait for paste to be processed, then restore original clipboard
    std::this_thread::sleep_for(std::chrono::milliseconds(CLIPBOARD_RESTORE_DELAY_MS));

    {
        const char * argv[] = {"xclip", "-selection", "clipboard", nullptr};
        // Restore saved clipboard (or write empty to clear)
        run_cmd(argv, CMD_TIMEOUT_MS, &saved_clipboard);
    }

    return paste_ret == 0;
}

bool TextOutput::is_terminal_class(const std::string & cls) {
    // Case-insensitive comparison against known terminal class names
    std::string lower = cls;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    static const char * terminals[] = {
        "alacritty",
        "kitty",
        "gnome-terminal",
        "gnome-terminal-server",
        "xterm",
        "uxterm",
        "konsole",
        "xfce4-terminal",
        "terminator",
        "tilix",
        "urxvt",
        "st-256color",
        "st",
        "foot",
        "wezterm",
        "terminal",
        "ghostty",
        "rio",
        "contour",
        "hyper",
        "tabby",
        "sakura",
        "guake",
        "tilda",
        "yakuake",
        "terminology",
        nullptr
    };

    for (int i = 0; terminals[i]; i++) {
        if (lower == terminals[i]) {
            return true;
        }
    }

    return false;
}
