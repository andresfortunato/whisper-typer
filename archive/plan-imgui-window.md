# Plan Archive: ImGui Window UI

**Completed:** 2026-03-20
**Phases:** 4 (all complete)

## Goal

Replace the libayatana-appindicator3 system tray with an embedded Dear ImGui window (SDL2 + OpenGL3). The window shows status, provides an autostart toggle, and displays scrollable transcript history with copy-to-clipboard.

## What Was Built

### Phase 1: Vendor ImGui and Update Build System
Vendored Dear ImGui (~15 files) into `third_party/imgui/`. Removed appindicator dependency from CMakeLists.txt. Deleted old tray.h/tray.cpp.

### Phase 2: Window Interface and Basic Rendering
Created window.h/window.cpp with the same callback interface as the old tray. Wired into typer.cpp, replacing `#ifdef HAS_TRAY` with `#ifdef HAS_GUI`. Added SDL_INIT_VIDEO alongside existing SDL_INIT_AUDIO. Window renders alongside the existing main loop without blocking it.

### Phase 3: Window Content
Built out UI panels: status indicator (Idle/Recording/Transcribing), hotkey display, how-it-works text, autostart toggle (reads/writes `~/.config/autostart/whisper-typer.desktop`), scrollable history viewer parsing history.jsonl with copy buttons, and Quit button. TDD approach for parsing logic.

### Phase 4: Polish and Desktop Entry
Updated desktop file (`NoDisplay=false`), split autostart desktop file, verified daemon mode (hidden window), ensured clean build.

## Files Created
- `third_party/imgui/` — Vendored ImGui source (imgui core + SDL2/OpenGL3 backends)
- `src/window.h` — GUI window interface
- `src/window.cpp` — GUI window implementation
- `src/window_logic.cpp` — Testable parsing/logic extracted from window

## Files Modified
- `CMakeLists.txt` — Removed APPINDICATOR, added ImGui sources + OpenGL, renamed HAS_TRAY to HAS_GUI
- `src/typer.cpp` — Swapped tray includes/calls for window includes/calls, added SDL_INIT_VIDEO
- `contrib/whisper-typer.desktop` — Set NoDisplay=false for app launcher visibility

## Files Deleted
- `src/tray.h` (old appindicator tray)
- `src/tray.cpp` (old appindicator tray)

## Key Decisions
1. ImGui + SDL2 + OpenGL3 — SDL2 already linked, ImGui vendored as source
2. Same callback pattern as old tray for clean swap in typer.cpp
3. Window hides on close (X button), app quits only via Quit button or SIGTERM
4. ImGui .ini saved to `~/.config/whisper-typer/imgui.ini`
5. History read-only from existing JSONL file, no new data structures
