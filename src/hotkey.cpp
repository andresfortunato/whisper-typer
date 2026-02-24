#include "hotkey.h"

#include <linux/input.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>

// Modifier flags (internal)
enum {
    MOD_CTRL  = 1 << 0,
    MOD_SHIFT = 1 << 1,
    MOD_ALT   = 1 << 2,
    MOD_SUPER = 1 << 3,
};

struct HotkeyListener::Impl {
    std::vector<int> fds;       // open file descriptors for keyboard devices
    int              key_code  = 0;   // evdev key code for the main key
    unsigned int     modmask   = 0;   // required modifiers (MOD_CTRL etc.)

    // Current modifier state (updated from events)
    bool ctrl_l  = false;
    bool ctrl_r  = false;
    bool shift_l = false;
    bool shift_r = false;
    bool alt_l   = false;
    bool alt_r   = false;
    bool super_l = false;
    bool super_r = false;

    bool mods_match() const {
        bool ctrl  = ctrl_l  || ctrl_r;
        bool shift = shift_l || shift_r;
        bool alt   = alt_l   || alt_r;
        bool super = super_l || super_r;

        // Required modifiers must be held
        if ((modmask & MOD_CTRL)  && !ctrl)  return false;
        if ((modmask & MOD_SHIFT) && !shift) return false;
        if ((modmask & MOD_ALT)   && !alt)   return false;
        if ((modmask & MOD_SUPER) && !super) return false;

        // Extra modifiers must NOT be held
        if (!(modmask & MOD_CTRL)  && ctrl)  return false;
        if (!(modmask & MOD_SHIFT) && shift) return false;
        if (!(modmask & MOD_ALT)   && alt)   return false;
        if (!(modmask & MOD_SUPER) && super) return false;

        return true;
    }

    void update_modifier(int code, bool pressed) {
        switch (code) {
            case KEY_LEFTCTRL:   ctrl_l  = pressed; break;
            case KEY_RIGHTCTRL:  ctrl_r  = pressed; break;
            case KEY_LEFTSHIFT:  shift_l = pressed; break;
            case KEY_RIGHTSHIFT: shift_r = pressed; break;
            case KEY_LEFTALT:    alt_l   = pressed; break;
            case KEY_RIGHTALT:   alt_r   = pressed; break;
            case KEY_LEFTMETA:   super_l = pressed; break;
            case KEY_RIGHTMETA:  super_r = pressed; break;
        }
    }

    bool is_modifier(int code) const {
        return code == KEY_LEFTCTRL  || code == KEY_RIGHTCTRL  ||
               code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT ||
               code == KEY_LEFTALT   || code == KEY_RIGHTALT   ||
               code == KEY_LEFTMETA  || code == KEY_RIGHTMETA;
    }

    void close_all_fds() {
        for (int fd : fds) {
            close(fd);
        }
        fds.clear();
    }
};

// Map common key names to evdev key codes
static int name_to_evdev(const std::string & name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // Letters
    if (lower.size() == 1 && lower[0] >= 'a' && lower[0] <= 'z') {
        static const int letter_codes[] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
            KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
            KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
        };
        return letter_codes[lower[0] - 'a'];
    }

    // Digits
    if (lower.size() == 1 && lower[0] >= '0' && lower[0] <= '9') {
        if (lower[0] == '0') return KEY_0;
        return KEY_1 + (lower[0] - '1');
    }

    // Named keys
    if (lower == "space")        return KEY_SPACE;
    if (lower == "period" || lower == "dot" || lower == ".") return KEY_DOT;
    if (lower == "comma" || lower == ",")  return KEY_COMMA;
    if (lower == "slash" || lower == "/")  return KEY_SLASH;
    if (lower == "backslash" || lower == "\\") return KEY_BACKSLASH;
    if (lower == "semicolon" || lower == ";")  return KEY_SEMICOLON;
    if (lower == "apostrophe" || lower == "'") return KEY_APOSTROPHE;
    if (lower == "grave" || lower == "`")      return KEY_GRAVE;
    if (lower == "minus" || lower == "-")      return KEY_MINUS;
    if (lower == "equal" || lower == "=")      return KEY_EQUAL;
    if (lower == "leftbrace" || lower == "[")  return KEY_LEFTBRACE;
    if (lower == "rightbrace" || lower == "]") return KEY_RIGHTBRACE;
    if (lower == "enter" || lower == "return") return KEY_ENTER;
    if (lower == "tab")         return KEY_TAB;
    if (lower == "backspace")   return KEY_BACKSPACE;
    if (lower == "escape" || lower == "esc") return KEY_ESC;
    if (lower == "delete" || lower == "del") return KEY_DELETE;
    if (lower == "insert" || lower == "ins") return KEY_INSERT;
    if (lower == "home")        return KEY_HOME;
    if (lower == "end")         return KEY_END;
    if (lower == "pageup")      return KEY_PAGEUP;
    if (lower == "pagedown")    return KEY_PAGEDOWN;
    if (lower == "up")          return KEY_UP;
    if (lower == "down")        return KEY_DOWN;
    if (lower == "left")        return KEY_LEFT;
    if (lower == "right")       return KEY_RIGHT;
    if (lower == "capslock")    return KEY_CAPSLOCK;
    if (lower == "print" || lower == "sysrq") return KEY_SYSRQ;
    if (lower == "pause")       return KEY_PAUSE;

    // F-keys: KEY_F1..KEY_F10 are contiguous (59-68), but KEY_F11=87, KEY_F12=88 are NOT
    static_assert(KEY_F1 + 9 == KEY_F10, "F1-F10 evdev keycode layout assumption violated");
    if (lower.size() >= 2 && lower[0] == 'f') {
        int n = 0;
        for (size_t i = 1; i < lower.size(); i++) {
            if (!isdigit(lower[i])) return -1;
            n = n * 10 + (lower[i] - '0');
        }
        if (n >= 1 && n <= 12) {
            if (n <= 10) return KEY_F1 + (n - 1);
            if (n == 11) return KEY_F11;
            if (n == 12) return KEY_F12;
        }
    }

    return -1;
}

HotkeyListener::HotkeyListener()
    : m_impl(std::make_unique<Impl>()) {}

HotkeyListener::~HotkeyListener() {
    stop();
}

bool HotkeyListener::open_keyboards() {
    // Close any previously opened fds (for rescan after device disconnect)
    m_impl->close_all_fds();

    DIR * dir = opendir("/dev/input");
    if (!dir) {
        fprintf(stderr, "hotkey: cannot open /dev/input: %s\n", strerror(errno));
        return false;
    }

    struct dirent * ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + ent->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        // Check if this device has EV_KEY capability
        unsigned long evbits[((EV_MAX + 7) / 8 + sizeof(unsigned long) - 1) / sizeof(unsigned long)] = {};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) {
            close(fd);
            continue;
        }

        bool has_ev_key = (evbits[EV_KEY / (8 * sizeof(unsigned long))] >>
                           (EV_KEY % (8 * sizeof(unsigned long)))) & 1;
        if (!has_ev_key) {
            close(fd);
            continue;
        }

        // Check if it has actual keyboard keys (KEY_A = 30)
        unsigned long keybits[((KEY_MAX + 7) / 8 + sizeof(unsigned long) - 1) / sizeof(unsigned long)] = {};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0) {
            close(fd);
            continue;
        }

        bool has_key_a = (keybits[KEY_A / (8 * sizeof(unsigned long))] >>
                          (KEY_A % (8 * sizeof(unsigned long)))) & 1;
        if (!has_key_a) {
            close(fd);
            continue;
        }

        // This looks like a keyboard
        char name[256] = "Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);

        fprintf(stderr, "hotkey: opened %s (%s)\n", path.c_str(), name);
        m_impl->fds.push_back(fd);
    }

    closedir(dir);
    return !m_impl->fds.empty();
}

bool HotkeyListener::init(const std::string & hotkey_str) {
    // Split on '+' to get parts
    std::vector<std::string> parts;
    std::istringstream ss(hotkey_str);
    std::string part;
    while (std::getline(ss, part, '+')) {
        while (!part.empty() && std::isspace((unsigned char)part.front())) part.erase(part.begin());
        while (!part.empty() && std::isspace((unsigned char)part.back()))  part.pop_back();
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    if (parts.empty()) {
        fprintf(stderr, "hotkey: empty hotkey string\n");
        return false;
    }

    // Last part is the key, rest are modifiers
    std::string key_name = parts.back();
    unsigned int modmask = 0;

    for (size_t i = 0; i + 1 < parts.size(); i++) {
        std::string mod_lower = parts[i];
        std::transform(mod_lower.begin(), mod_lower.end(), mod_lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        if (mod_lower == "ctrl" || mod_lower == "control") {
            modmask |= MOD_CTRL;
        } else if (mod_lower == "shift") {
            modmask |= MOD_SHIFT;
        } else if (mod_lower == "alt") {
            modmask |= MOD_ALT;
        } else if (mod_lower == "super" || mod_lower == "super_l" || mod_lower == "super_r" || mod_lower == "mod4" || mod_lower == "meta") {
            modmask |= MOD_SUPER;
        } else {
            fprintf(stderr, "hotkey: unknown modifier '%s'\n", parts[i].c_str());
            return false;
        }
    }

    int key_code = name_to_evdev(key_name);
    if (key_code < 0) {
        fprintf(stderr, "hotkey: unknown key '%s'\n", key_name.c_str());
        return false;
    }

    m_impl->key_code = key_code;
    m_impl->modmask  = modmask;

    // Open keyboard devices
    if (!open_keyboards()) {
        fprintf(stderr, "hotkey: no keyboard devices found!\n");
        fprintf(stderr, "hotkey: make sure you are in the 'input' group:\n");
        fprintf(stderr, "hotkey:   sudo usermod -aG input $USER\n");
        fprintf(stderr, "hotkey: then log out and back in.\n");
        return false;
    }

    fprintf(stderr, "hotkey: registered '%s' (evdev keycode=%d, modmask=0x%x)\n",
            hotkey_str.c_str(), key_code, modmask);

    return true;
}

bool HotkeyListener::start(HotkeyCallback callback) {
    if (m_running) return false;
    if (m_impl->fds.empty()) return false;

    m_callback = callback;
    m_running = true;

    m_thread = std::thread(&HotkeyListener::listen_thread, this);

    return true;
}

void HotkeyListener::stop() {
    if (!m_running && m_impl->fds.empty()) return;

    m_running = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_impl->close_all_fds();
}

bool HotkeyListener::poll_pressed() {
    return m_event_press.exchange(false);
}

bool HotkeyListener::poll_released() {
    return m_event_release.exchange(false);
}

void HotkeyListener::listen_thread() {
    bool hotkey_active = false;
    bool needs_rescan = false;

    while (m_running) {
        // Rescan devices if a read error was detected (e.g. Bluetooth disconnect)
        if (needs_rescan) {
            needs_rescan = false;
            // Reset modifier state since we lost track
            m_impl->ctrl_l = m_impl->ctrl_r = false;
            m_impl->shift_l = m_impl->shift_r = false;
            m_impl->alt_l = m_impl->alt_r = false;
            m_impl->super_l = m_impl->super_r = false;
            hotkey_active = false;

            fprintf(stderr, "hotkey: device change detected, rescanning...\n");
            if (open_keyboards()) {
                fprintf(stderr, "hotkey: rescan complete, %zu device(s)\n", m_impl->fds.size());
            } else {
                fprintf(stderr, "hotkey: rescan found no devices, will retry...\n");
                // Wait longer before next rescan attempt
                for (int j = 0; j < 50 && m_running; j++) {
                    usleep(100000); // 5 seconds total
                }
                needs_rescan = true;
            }
            continue;
        }

        // Use usleep instead of poll() to avoid interfering with
        // SDL2's PipeWire audio thread which also uses fd polling
        usleep(20000); // 20ms = 50Hz polling rate

        for (size_t i = 0; i < m_impl->fds.size(); i++) {
            struct input_event ev;
            // fds are O_NONBLOCK, so read returns immediately if no data
            while (true) {
                ssize_t n = read(m_impl->fds[i], &ev, sizeof(ev));
                if (n == (ssize_t)sizeof(ev)) {
                    // Successfully read an event
                } else {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break; // No more events, normal for O_NONBLOCK
                    }
                    if (n < 0 && (errno == EIO || errno == ENODEV)) {
                        // Device disconnected
                        fprintf(stderr, "hotkey: device disconnected (fd %d)\n", m_impl->fds[i]);
                        needs_rescan = true;
                    }
                    break;
                }

                if (ev.type != EV_KEY) continue;

                bool pressed = (ev.value == 1); // 1 = press, 0 = release, 2 = repeat
                bool released = (ev.value == 0);

                if (!pressed && !released) continue; // skip repeat events

                // Update modifier state
                if (m_impl->is_modifier(ev.code)) {
                    m_impl->update_modifier(ev.code, pressed);
                }

                // Check the main key
                if (ev.code == m_impl->key_code) {
                    if (pressed && m_impl->mods_match() && !hotkey_active) {
                        hotkey_active = true;
                        m_event_press = true;
                        if (m_callback) {
                            m_callback(true);
                        }
                    } else if (released && hotkey_active) {
                        hotkey_active = false;
                        m_event_release = true;
                        if (m_callback) {
                            m_callback(false);
                        }
                    }
                }

                // If a modifier is released while hotkey is active, deactivate
                if (released && m_impl->is_modifier(ev.code) && hotkey_active) {
                    if (!m_impl->mods_match()) {
                        hotkey_active = false;
                        m_event_release = true;
                        if (m_callback) {
                            m_callback(false);
                        }
                    }
                }
            }
        }
    }
}
