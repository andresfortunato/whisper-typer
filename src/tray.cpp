#include "tray.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <libayatana-appindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

struct TrayIconImpl {
    AppIndicator * indicator = nullptr;
    GtkWidget    * menu      = nullptr;
    GtkWidget    * status_item  = nullptr;
    GtkWidget    * show_item    = nullptr;
    GtkWidget    * copy_item    = nullptr;
    GtkWidget    * history_item = nullptr;
    GtkWidget    * toggle_item  = nullptr;

    TrayCallbacks  callbacks;
    std::string    last_transcript;
    TrayState      state = TrayState::IDLE;
};

// Fire-and-forget subprocess using double-fork (no zombies)
static void run_detached(const char * const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        if (fork() != 0) _exit(0);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], const_cast<char * const *>(argv));
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
}

// Copy text to clipboard using xclip or wl-copy (double-fork, no zombies)
static void copy_to_clipboard(const std::string & text) {
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

    // Parent: write data to pipe, then reap intermediate child
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
}

// GTK menu callbacks — these are called on the GLib main thread
static void on_show_last(GtkMenuItem * /*item*/, gpointer data) {
    auto * impl = static_cast<TrayIconImpl *>(data);
    if (impl->last_transcript.empty()) return;

    const char * argv[] = {
        "notify-send", "-t", "5000", "whisper-typer",
        impl->last_transcript.c_str(), nullptr
    };
    run_detached(argv);
}

static void on_copy_last(GtkMenuItem * /*item*/, gpointer data) {
    auto * impl = static_cast<TrayIconImpl *>(data);
    if (impl->last_transcript.empty()) return;
    copy_to_clipboard(impl->last_transcript);
}

static void on_open_history(GtkMenuItem * /*item*/, gpointer data) {
    auto * impl = static_cast<TrayIconImpl *>(data);
    std::string path = impl->callbacks.get_history_path();
    if (path.empty()) return;

    const char * argv[] = { "xdg-open", path.c_str(), nullptr };
    run_detached(argv);
}

static void on_toggle(GtkMenuItem * /*item*/, gpointer data) {
    auto * impl = static_cast<TrayIconImpl *>(data);
    if (impl->callbacks.on_toggle) impl->callbacks.on_toggle();
}

static void on_quit(GtkMenuItem * /*item*/, gpointer data) {
    auto * impl = static_cast<TrayIconImpl *>(data);
    if (impl->callbacks.on_quit) impl->callbacks.on_quit();
}

static const char * state_icon(TrayState s) {
    switch (s) {
        case TrayState::IDLE:         return "audio-input-microphone";
        case TrayState::RECORDING:    return "media-record";
        case TrayState::TRANSCRIBING: return "preferences-desktop-accessibility";
    }
    return "audio-input-microphone";
}

static const char * state_label(TrayState s) {
    switch (s) {
        case TrayState::IDLE:         return "Idle";
        case TrayState::RECORDING:    return "Recording...";
        case TrayState::TRANSCRIBING: return "Transcribing...";
    }
    return "Idle";
}

TrayIcon::TrayIcon() : m_impl(std::make_unique<TrayIconImpl>()) {}
TrayIcon::~TrayIcon() { shutdown(); }

bool TrayIcon::init(const TrayCallbacks & cb) {
    m_impl->callbacks = cb;

    // Initialize GTK (no window created, just for menu support)
    if (!gtk_init_check(nullptr, nullptr)) {
        fprintf(stderr, "warning: gtk_init_check failed, tray disabled\n");
        return false;
    }

    // Create app indicator
    m_impl->indicator = app_indicator_new(
        "whisper-typer",
        "audio-input-microphone",
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );
    if (!m_impl->indicator) {
        fprintf(stderr, "warning: app_indicator_new failed, tray disabled\n");
        return false;
    }

    app_indicator_set_status(m_impl->indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(m_impl->indicator, "Whisper Typer");

    // Build menu
    m_impl->menu = gtk_menu_new();

    // Status label (non-interactive)
    m_impl->status_item = gtk_menu_item_new_with_label("Status: Idle");
    gtk_widget_set_sensitive(m_impl->status_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), m_impl->status_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), gtk_separator_menu_item_new());

    // Show Last Transcript
    m_impl->show_item = gtk_menu_item_new_with_label("Show Last Transcript");
    g_signal_connect(m_impl->show_item, "activate", G_CALLBACK(on_show_last), m_impl.get());
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), m_impl->show_item);

    // Copy Last to Clipboard
    m_impl->copy_item = gtk_menu_item_new_with_label("Copy Last to Clipboard");
    g_signal_connect(m_impl->copy_item, "activate", G_CALLBACK(on_copy_last), m_impl.get());
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), m_impl->copy_item);

    // Open History File
    m_impl->history_item = gtk_menu_item_new_with_label("Open History File");
    g_signal_connect(m_impl->history_item, "activate", G_CALLBACK(on_open_history), m_impl.get());
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), m_impl->history_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), gtk_separator_menu_item_new());

    // Start/Stop Recording
    m_impl->toggle_item = gtk_menu_item_new_with_label("Start Recording");
    g_signal_connect(m_impl->toggle_item, "activate", G_CALLBACK(on_toggle), m_impl.get());
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), m_impl->toggle_item);

    // Quit
    GtkWidget * quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), m_impl.get());
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), quit_item);

    gtk_widget_show_all(m_impl->menu);
    app_indicator_set_menu(m_impl->indicator, GTK_MENU(m_impl->menu));

    return true;
}

void TrayIcon::set_state(TrayState state) {
    if (!m_impl->indicator) return;
    m_impl->state = state;

    app_indicator_set_icon(m_impl->indicator, state_icon(state));

    if (m_impl->status_item) {
        std::string label = std::string("Status: ") + state_label(state);
        gtk_menu_item_set_label(GTK_MENU_ITEM(m_impl->status_item), label.c_str());
    }

    if (m_impl->toggle_item) {
        const char * toggle_label = (state == TrayState::IDLE) ? "Start Recording" : "Stop Recording";
        gtk_menu_item_set_label(GTK_MENU_ITEM(m_impl->toggle_item), toggle_label);
    }
}

void TrayIcon::set_last_transcript(const std::string & text) {
    m_impl->last_transcript = text;
}

void TrayIcon::poll() {
    // Non-blocking GLib main context iteration
    while (g_main_context_iteration(nullptr, FALSE)) {}
}

void TrayIcon::shutdown() {
    if (m_impl->indicator) {
        app_indicator_set_status(m_impl->indicator, APP_INDICATOR_STATUS_PASSIVE);
        g_object_unref(m_impl->indicator);
        m_impl->indicator = nullptr;
    }
}
