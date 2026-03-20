#include "tray.h"

#include <cstdio>

#include <libayatana-appindicator/app-indicator.h>
#include <gtk/gtk.h>

struct TrayIconImpl {
    AppIndicator * indicator = nullptr;
    GtkWidget    * menu      = nullptr;
    TrayCallbacks  callbacks;
    TrayState      state = TrayState::IDLE;
};

static const char * state_icon(TrayState s) {
    switch (s) {
        case TrayState::IDLE:         return "audio-input-microphone";
        case TrayState::RECORDING:    return "media-record";
        case TrayState::TRANSCRIBING: return "preferences-desktop-accessibility";
    }
    return "audio-input-microphone";
}

static void on_show_window(GtkMenuItem * /*item*/, gpointer data) {
    auto * impl = static_cast<TrayIconImpl *>(data);
    if (impl->callbacks.on_show_window) impl->callbacks.on_show_window();
}

static void on_quit(GtkMenuItem * /*item*/, gpointer data) {
    auto * impl = static_cast<TrayIconImpl *>(data);
    if (impl->callbacks.on_quit) impl->callbacks.on_quit();
}

TrayIcon::TrayIcon() = default;
TrayIcon::~TrayIcon() { shutdown(); }

bool TrayIcon::init(const TrayCallbacks & cb) {
    m_impl = std::make_unique<TrayIconImpl>();
    m_impl->callbacks = cb;

    if (!gtk_init_check(nullptr, nullptr)) {
        fprintf(stderr, "warning: gtk_init_check failed, tray disabled\n");
        return false;
    }

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

    // Minimal right-click menu
    m_impl->menu = gtk_menu_new();

    GtkWidget * show_item = gtk_menu_item_new_with_label("Show Window");
    g_signal_connect(show_item, "activate", G_CALLBACK(on_show_window), m_impl.get());
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), show_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), gtk_separator_menu_item_new());

    GtkWidget * quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), m_impl.get());
    gtk_menu_shell_append(GTK_MENU_SHELL(m_impl->menu), quit_item);

    gtk_widget_show_all(m_impl->menu);
    app_indicator_set_menu(m_impl->indicator, GTK_MENU(m_impl->menu));

    return true;
}

void TrayIcon::set_state(TrayState state) {
    if (!m_impl || !m_impl->indicator) return;
    m_impl->state = state;
    app_indicator_set_icon(m_impl->indicator, state_icon(state));
}

void TrayIcon::poll() {
    while (g_main_context_iteration(nullptr, FALSE)) {}
}

void TrayIcon::shutdown() {
    if (!m_impl || !m_impl->indicator) return;
    app_indicator_set_status(m_impl->indicator, APP_INDICATOR_STATUS_PASSIVE);
    g_object_unref(m_impl->indicator);
    m_impl->indicator = nullptr;
}
