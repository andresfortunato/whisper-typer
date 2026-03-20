#pragma once

#include <string>

#ifdef HAS_LIBEI

#include <cstdint>

struct ei;
struct ei_device;
struct oeffis;

class LibeiKbd {
public:
    LibeiKbd();
    ~LibeiKbd();

    // Non-copyable
    LibeiKbd(const LibeiKbd &) = delete;
    LibeiKbd & operator=(const LibeiKbd &) = delete;

    // Connect via RemoteDesktop portal and set up libei sender.
    // Blocks up to 30s for portal consent dialog.
    bool init();

    // Type a string by emitting keyboard events via libei.
    bool type_text(const std::string & text, int delay_ms = 12);

    bool is_initialized() const;

    // Tear down libei and oeffis contexts.
    void shutdown();

private:
    struct oeffis * m_oeffis = nullptr;
    struct ei *     m_ei     = nullptr;
    struct ei_device * m_device = nullptr;
    bool     m_initialized = false;
    uint32_t m_sequence    = 0;

    // Poll a file descriptor with a timeout in milliseconds.
    // Returns true if data is available.
    static bool poll_fd(int fd, int timeout_ms);

    // Get current CLOCK_MONOTONIC time in microseconds.
    static uint64_t now_usec();
};

#else

// Stub when libei is not available
class LibeiKbd {
public:
    bool init() { return false; }
    bool type_text(const std::string &, int = 12) { return false; }
    bool is_initialized() const { return false; }
    void shutdown() {}
};

#endif
