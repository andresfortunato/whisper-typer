#include "window.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <SDL_opengl.h>
#endif

#ifdef __linux__
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

struct AppWindowImpl {
    SDL_Window   * sdl_window = nullptr;
    SDL_GLContext   gl_context = nullptr;
    Uint32         window_id  = 0;

    WindowCallbacks callbacks;
    AppState        state = AppState::IDLE;
    std::string     last_transcript;
    bool            visible = false;
    bool            initialized = false;
    std::string     ini_path;
    std::string     hotkey_display;

    // History cache — reloaded when window becomes visible
    std::vector<HistoryEntry> history;
    bool history_dirty = true;
};

// Read an entire file into a string
static std::string read_file(const std::string & path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Create directories recursively (like mkdir -p)
static void mkdir_p(const std::string & path) {
#ifdef __linux__
    std::string accum;
    for (size_t i = 0; i < path.size(); i++) {
        accum += path[i];
        if (path[i] == '/' && i > 0) {
            mkdir(accum.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
#endif
}

// Copy text to clipboard using xclip or wl-copy (double-fork, no zombies)
static void copy_to_clipboard(const std::string & text) {
#ifdef __linux__
    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    const char * wayland = getenv("WAYLAND_DISPLAY");

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (pid == 0) {
        close(pipefd[1]);
        if (fork() != 0) {
            close(pipefd[0]);
            _exit(0);
        }
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        if (wayland && wayland[0] != '\0') {
            execlp("wl-copy", "wl-copy", nullptr);
        } else {
            execlp("xclip", "xclip", "-selection", "clipboard", nullptr);
        }
        _exit(127);
    }

    close(pipefd[0]);
    const char * data = text.c_str();
    size_t remaining = text.size();
    while (remaining > 0) {
        ssize_t n = write(pipefd[1], data, remaining);
        if (n > 0) { data += n; remaining -= n; }
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    close(pipefd[1]);

    int status;
    waitpid(pid, &status, 0);
#else
    (void)text;
#endif
}

// Check if autostart desktop file exists
static bool autostart_enabled() {
    std::string path = autostart_desktop_path();
    if (path.empty()) return false;
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// Write autostart desktop file
static void autostart_enable() {
    std::string path = autostart_desktop_path();
    if (path.empty()) return;

    // Create parent dir
    auto slash = path.rfind('/');
    if (slash != std::string::npos) mkdir_p(path.substr(0, slash));

    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=Whisper Typer\n"
      << "Comment=Voice-to-text typing tool\n"
      << "Exec=whisper-typer --daemon\n"
      << "Terminal=false\n"
      << "Categories=Utility;Accessibility;\n";
}

// Remove autostart desktop file
static void autostart_disable() {
    std::string path = autostart_desktop_path();
    if (!path.empty()) {
        remove(path.c_str());
    }
}

// Delete a history entry by rewriting the JSONL file without the matching line.
// Matches on timestamp + text to identify the entry.
static void delete_history_entry(const std::string & path, const HistoryEntry & entry) {
    if (path.empty()) return;

    std::ifstream in(path);
    if (!in.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    bool removed = false;
    while (std::getline(in, line)) {
        if (!removed) {
            HistoryEntry e;
            if (parse_history_line(line, e) &&
                e.timestamp == entry.timestamp && e.text == entry.text) {
                removed = true;
                continue;  // skip this line
            }
        }
        lines.push_back(std::move(line));
    }
    in.close();

    if (removed) {
        std::ofstream out(path, std::ios::trunc);
        for (const auto & l : lines) {
            out << l << "\n";
        }
    }
}

// Render the full UI content within an ImGui frame
static void render_ui(AppWindowImpl * impl) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // --- Status indicator ---
    {
        ImVec4 color;
        const char * label;
        switch (impl->state) {
            case AppState::RECORDING:
                color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                label = "Recording...";
                break;
            case AppState::TRANSCRIBING:
                color = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
                label = "Transcribing...";
                break;
            default:
                color = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
                label = "Idle";
                break;
        }
        ImGui::TextColored(color, "%s", label);
    }

    // --- Hotkey display and help ---
    if (!impl->hotkey_display.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Hotkey: %s", impl->hotkey_display.c_str());
    }

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Press the hotkey to start recording. Speak, then press again to stop. "
        "Text will be typed into the focused window.");
    ImGui::Spacing();

    // --- Autostart toggle ---
    ImGui::Separator();
    {
        bool enabled = autostart_enabled();
        if (ImGui::Checkbox("Start with system login", &enabled)) {
            if (enabled) {
                autostart_enable();
            } else {
                autostart_disable();
            }
        }
    }
    ImGui::Spacing();

    // --- History ---
    ImGui::Separator();
    ImGui::Text("History");
    if (!impl->history.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear All")) {
            if (impl->callbacks.get_history_path) {
                std::string path = impl->callbacks.get_history_path();
                if (!path.empty()) {
                    std::ofstream out(path, std::ios::trunc);
                    // truncate to empty
                }
                impl->history_dirty = true;
            }
        }
    }
    ImGui::Spacing();

    if (impl->history.empty()) {
        ImGui::TextDisabled("(no history)");
    } else {
        float avail_h = ImGui::GetContentRegionAvail().y - 35.0f; // leave room for Quit button
        if (avail_h < 60.0f) avail_h = 60.0f;
        ImGui::BeginChild("history", ImVec2(0, avail_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

        for (size_t i = 0; i < impl->history.size(); i++) {
            const auto & entry = impl->history[i];
            ImGui::PushID((int)i);

            // Timestamp + duration
            ImGui::TextDisabled("%s  (%s)", entry.timestamp.c_str(),
                format_duration(entry.duration_ms).c_str());

            // Text (truncated if long)
            if (entry.text.size() > 120) {
                ImGui::TextWrapped("%s...", entry.text.substr(0, 120).c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.text.c_str());
                }
            } else {
                ImGui::TextWrapped("%s", entry.text.c_str());
            }

            // Copy button
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                copy_to_clipboard(entry.text);
            }

            // Delete button
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                if (impl->callbacks.get_history_path) {
                    std::string path = impl->callbacks.get_history_path();
                    delete_history_entry(path, entry);
                    impl->history_dirty = true;
                }
            }

            if (i + 1 < impl->history.size()) {
                ImGui::Separator();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    // --- Quit button ---
    ImGui::Spacing();
    if (ImGui::Button("Quit")) {
        if (impl->callbacks.on_quit) {
            impl->callbacks.on_quit();
        }
    }

    ImGui::End();
}

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() { shutdown(); }

bool AppWindow::init(const WindowCallbacks & cb) {
    m_impl = std::make_unique<AppWindowImpl>();
    m_impl->callbacks = cb;

    // Init SDL video subsystem (additive — audio is already init'd)
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "warning: SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        return false;
    }

    // GL 3.0 + GLSL 130 (ImGui's Linux defaults)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    m_impl->sdl_window = SDL_CreateWindow(
        window_title(AppState::IDLE),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        520, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN
    );
    if (!m_impl->sdl_window) {
        fprintf(stderr, "warning: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    m_impl->window_id = SDL_GetWindowID(m_impl->sdl_window);

    m_impl->gl_context = SDL_GL_CreateContext(m_impl->sdl_window);
    if (!m_impl->gl_context) {
        fprintf(stderr, "warning: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(m_impl->sdl_window);
        m_impl->sdl_window = nullptr;
        return false;
    }
    SDL_GL_MakeCurrent(m_impl->sdl_window, m_impl->gl_context);
    SDL_GL_SetSwapInterval(1);  // vsync

    // ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO & io = ImGui::GetIO();

    // Set ini file path to ~/.config/whisper-typer/imgui.ini
    const char * xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        m_impl->ini_path = std::string(xdg_config) + "/whisper-typer/imgui.ini";
    } else {
        const char * home = getenv("HOME");
        if (home) {
            m_impl->ini_path = std::string(home) + "/.config/whisper-typer/imgui.ini";
        }
    }
    if (!m_impl->ini_path.empty()) {
        io.IniFilename = m_impl->ini_path.c_str();
    }

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Init backends
    ImGui_ImplSDL2_InitForOpenGL(m_impl->sdl_window, m_impl->gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");

    m_impl->initialized = true;

    // Show the window
    show();

    return true;
}

void AppWindow::poll() {
    if (!m_impl || !m_impl->initialized) return;

    // SDL_PollEvent calls SDL_PumpEvents internally, which gathers OS-level
    // input events. Without pumping, the window manager gets no response to
    // its pings and declares the app "not responding."
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            // Re-push SDL_QUIT so the main loop's sdl_poll_events() can catch it
            SDL_PushEvent(&event);
            return;
        }

        // Let ImGui process all events (keyboard, mouse, window resize, etc.)
        ImGui_ImplSDL2_ProcessEvent(&event);

        // Handle our window's close button → hide, don't quit
        if (event.type == SDL_WINDOWEVENT &&
            event.window.windowID == m_impl->window_id &&
            event.window.event == SDL_WINDOWEVENT_CLOSE) {
            hide();
        }
    }

    if (!m_impl->visible) return;

    // Reload history when window becomes visible (marked dirty on show())
    if (m_impl->history_dirty) {
        m_impl->history_dirty = false;
        if (m_impl->callbacks.get_history_path) {
            std::string path = m_impl->callbacks.get_history_path();
            if (!path.empty()) {
                m_impl->history = parse_history(read_file(path));
            }
        }
    }

    // Check if minimized
    Uint32 flags = SDL_GetWindowFlags(m_impl->sdl_window);
    if (flags & SDL_WINDOW_MINIMIZED) return;

    // Ensure GL context is current (can be lost after hide/show cycles)
    SDL_GL_MakeCurrent(m_impl->sdl_window, m_impl->gl_context);

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    render_ui(m_impl.get());

    // Render
    ImGui::Render();
    ImGuiIO & io = ImGui::GetIO();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(m_impl->sdl_window);
}

void AppWindow::set_state(AppState state) {
    if (!m_impl) return;
    m_impl->state = state;
    if (m_impl->sdl_window) {
        SDL_SetWindowTitle(m_impl->sdl_window, window_title(state));
    }
}

void AppWindow::set_last_transcript(const std::string & text) {
    if (!m_impl) return;
    m_impl->last_transcript = text;
    m_impl->history_dirty = true;  // new transcript → reload history next frame
}

void AppWindow::set_hotkey(const std::string & hotkey) {
    if (!m_impl) return;
    m_impl->hotkey_display = hotkey;
}

void AppWindow::show() {
    if (!m_impl || !m_impl->sdl_window) return;
    SDL_ShowWindow(m_impl->sdl_window);
    SDL_RaiseWindow(m_impl->sdl_window);
    // Re-activate GL context — required after SDL_ShowWindow on some compositors
    SDL_GL_MakeCurrent(m_impl->sdl_window, m_impl->gl_context);
    m_impl->visible = true;
    m_impl->history_dirty = true;  // reload history when shown
}

void AppWindow::hide() {
    if (!m_impl || !m_impl->sdl_window) return;
    SDL_HideWindow(m_impl->sdl_window);
    m_impl->visible = false;
}

bool AppWindow::is_visible() const {
    return m_impl && m_impl->visible;
}

void AppWindow::shutdown() {
    if (!m_impl || !m_impl->initialized) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (m_impl->gl_context) {
        SDL_GL_DeleteContext(m_impl->gl_context);
        m_impl->gl_context = nullptr;
    }
    if (m_impl->sdl_window) {
        SDL_DestroyWindow(m_impl->sdl_window);
        m_impl->sdl_window = nullptr;
    }

    m_impl->initialized = false;
}
