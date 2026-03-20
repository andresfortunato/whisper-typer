# Plan Archive: libei Backend for Wayland Text Input

**Completed:** 2026-03-20
**Phases:** 3 (all complete)

## Goal

Replace uinput as the primary Wayland text input method with libei, a compositor-mediated input emulation library. Eliminates the need for the `input` group and `/dev/uinput` udev rule. Fallback chain: libei → uinput → wtype.

## What Was Built

### Phase 1: Shared Keymap Extraction
Extracted `KeyMapping` struct, `KEYMAP[128]` table, and lookup function from `uinput-kbd.cpp` into `src/keymap.h`. Both uinput and libei backends share the same evdev keycode table. Renamed `uinput_lookup_char()` → `keymap_lookup_char()`.

### Phase 2: LibeiKbd Module
Created `src/libei-kbd.h` and `src/libei-kbd.cpp` — connects to compositor via liboeffis RemoteDesktop portal, sets up libei sender, emits keyboard events. Optional build with `#ifdef HAS_LIBEI` (same pattern as HAS_GUI/HAS_TRAY). Stub class when libei unavailable.

### Phase 3: Integration
Wired LibeiKbd into TextOutput's Wayland path as first choice. Updated typer.cpp init sequence, install.sh (libei-dev deps, conditional udev rule), CMakeLists.txt (optional pkg-config dep).

## Files Created
- `src/keymap.h` — Shared US-QWERTY keymap (header-only, constexpr)
- `src/libei-kbd.h` — LibeiKbd class declaration + stub
- `src/libei-kbd.cpp` — Full implementation: oeffis portal, libei sender, keyboard events

## Files Modified
- `src/uinput-kbd.h` — Removed KeyMapping/lookup, includes keymap.h
- `src/uinput-kbd.cpp` — Removed KEYMAP table, uses keymap_lookup_char()
- `tests/test_uinput_keymap.cpp` — Uses keymap.h directly
- `src/text-output.h` — Added LibeiKbd member and methods
- `src/text-output.cpp` — Wayland fallback chain: libei → uinput → wtype
- `src/typer.cpp` — Init libei before uinput on Wayland
- `CMakeLists.txt` — Optional libei dep, source, link, compile def
- `install.sh` — libei-dev/liboeffis-dev build deps, conditional udev rule

## Key Decisions
1. liboeffis for portal negotiation (simple, no restore tokens in v1)
2. Blocking init acceptable (30s timeout for portal consent dialog)
3. ei_device_start/stop_emulating per type_text call
4. Shared keymap avoids 128-entry table duplication
