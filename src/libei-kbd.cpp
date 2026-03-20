#include "libei-kbd.h"

#ifdef HAS_LIBEI

#include "keymap.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <unistd.h>

#include <libei.h>
#include <liboeffis.h>

static constexpr int PORTAL_TIMEOUT_MS = 30000;
static constexpr int EI_TIMEOUT_MS     = 10000;

// ── LibeiKbd implementation ───────────────────────────────────────

LibeiKbd::LibeiKbd() {}

LibeiKbd::~LibeiKbd() {
    shutdown();
}

bool LibeiKbd::poll_fd(int fd, int timeout_ms) {
    struct pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    return poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}

uint64_t LibeiKbd::now_usec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

bool LibeiKbd::init() {
    if (m_initialized) return true;

    // Step 1: Create oeffis context and request keyboard access via portal
    m_oeffis = oeffis_new(nullptr);
    if (!m_oeffis) {
        fprintf(stderr, "libei: oeffis_new() failed\n");
        return false;
    }

    oeffis_create_session(m_oeffis, OEFFIS_DEVICE_KEYBOARD);

    // Step 2: Wait for portal response (user may need to approve dialog)
    int oeffis_fd = oeffis_get_fd(m_oeffis);
    if (oeffis_fd < 0) {
        fprintf(stderr, "libei: oeffis_get_fd() failed\n");
        shutdown();
        return false;
    }

    bool connected = false;
    // Poll in a loop — the portal negotiation involves multiple D-Bus round trips
    int remaining_ms = PORTAL_TIMEOUT_MS;
    while (remaining_ms > 0) {
        if (!poll_fd(oeffis_fd, remaining_ms)) {
            fprintf(stderr, "libei: portal timeout after %dms\n", PORTAL_TIMEOUT_MS);
            shutdown();
            return false;
        }

        oeffis_dispatch(m_oeffis);

        enum oeffis_event_type ev;
        while ((ev = oeffis_get_event(m_oeffis)) != OEFFIS_EVENT_NONE) {
            if (ev == OEFFIS_EVENT_CONNECTED_TO_EIS) {
                connected = true;
                break;
            }
            if (ev == OEFFIS_EVENT_CLOSED || ev == OEFFIS_EVENT_DISCONNECTED) {
                fprintf(stderr, "libei: portal connection closed/disconnected\n");
                shutdown();
                return false;
            }
        }

        if (connected) break;
        remaining_ms -= 500; // approximate — poll consumed some time
    }

    if (!connected) {
        fprintf(stderr, "libei: portal did not connect\n");
        shutdown();
        return false;
    }

    // Step 3: Get EIS fd and set up libei sender
    int eis_fd = oeffis_get_eis_fd(m_oeffis);
    if (eis_fd < 0) {
        fprintf(stderr, "libei: oeffis_get_eis_fd() failed\n");
        shutdown();
        return false;
    }

    m_ei = ei_new_sender(nullptr);
    if (!m_ei) {
        fprintf(stderr, "libei: ei_new_sender() failed\n");
        shutdown();
        return false;
    }

    if (ei_setup_backend_fd(m_ei, eis_fd) != 0) {
        fprintf(stderr, "libei: ei_setup_backend_fd() failed\n");
        shutdown();
        return false;
    }

    // Step 4: Process ei events until we get a keyboard device
    int ei_fd = ei_get_fd(m_ei);
    bool device_ready = false;
    remaining_ms = EI_TIMEOUT_MS;

    while (remaining_ms > 0 && !device_ready) {
        if (!poll_fd(ei_fd, remaining_ms)) {
            fprintf(stderr, "libei: ei event timeout\n");
            shutdown();
            return false;
        }

        ei_dispatch(m_ei);

        struct ei_event *event;
        while ((event = ei_get_event(m_ei)) != nullptr) {
            enum ei_event_type type = ei_event_get_type(event);

            switch (type) {
            case EI_EVENT_SEAT_ADDED: {
                struct ei_seat *seat = ei_event_get_seat(event);
                ei_seat_bind_capabilities(seat,
                    EI_DEVICE_CAP_KEYBOARD, NULL);
                break;
            }
            case EI_EVENT_DEVICE_ADDED: {
                struct ei_device *dev = ei_event_get_device(event);
                if (ei_device_has_capability(dev, EI_DEVICE_CAP_KEYBOARD)) {
                    m_device = ei_device_ref(dev);
                }
                break;
            }
            case EI_EVENT_DEVICE_RESUMED:
                if (m_device) {
                    device_ready = true;
                }
                break;
            case EI_EVENT_DISCONNECT:
                fprintf(stderr, "libei: disconnected during init\n");
                ei_event_unref(event);
                shutdown();
                return false;
            default:
                break;
            }

            ei_event_unref(event);
            if (device_ready) break;
        }

        remaining_ms -= 500;
    }

    if (!device_ready) {
        fprintf(stderr, "libei: no keyboard device available\n");
        shutdown();
        return false;
    }

    m_initialized = true;
    fprintf(stderr, "libei keyboard initialized\n");
    return true;
}

bool LibeiKbd::type_text(const std::string & text, int delay_ms) {
    if (!m_initialized || !m_device) return false;

    ei_device_start_emulating(m_device, m_sequence++);

    for (char c : text) {
        KeyMapping km = keymap_lookup_char(c);
        if (km.keycode < 0) {
            fprintf(stderr, "libei: unmapped character 0x%02x, skipping\n",
                    static_cast<unsigned char>(c));
            continue;
        }

        // Press shift if needed
        if (km.shift) {
            ei_device_keyboard_key(m_device, KEY_LEFTSHIFT, true);
            ei_device_frame(m_device, now_usec());
        }

        // Key press
        ei_device_keyboard_key(m_device, static_cast<uint32_t>(km.keycode), true);
        ei_device_frame(m_device, now_usec());

        // Inter-key delay
        if (delay_ms > 0) {
            usleep(static_cast<useconds_t>(delay_ms) * 1000);
        }

        // Key release
        ei_device_keyboard_key(m_device, static_cast<uint32_t>(km.keycode), false);
        ei_device_frame(m_device, now_usec());

        // Release shift if needed
        if (km.shift) {
            ei_device_keyboard_key(m_device, KEY_LEFTSHIFT, false);
            ei_device_frame(m_device, now_usec());
        }
    }

    ei_device_stop_emulating(m_device);
    return true;
}

bool LibeiKbd::is_initialized() const {
    return m_initialized;
}

void LibeiKbd::shutdown() {
    if (m_device) {
        ei_device_unref(m_device);
        m_device = nullptr;
    }
    if (m_ei) {
        ei_unref(m_ei);
        m_ei = nullptr;
    }
    if (m_oeffis) {
        oeffis_unref(m_oeffis);
        m_oeffis = nullptr;
    }
    m_initialized = false;
}

#endif // HAS_LIBEI
