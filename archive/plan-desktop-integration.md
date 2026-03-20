# Plan Archive: Desktop Integration

**Completed:** 2026-03-20
**Phases:** 3 (all complete)

## Goal

Make whisper-typer a proper desktop application: tray icon in Ubuntu's top bar, shows in app launcher, starts on login, never requires a terminal. Second launch signals existing instance to show window instead of erroring.

## What Was Built

### Phase 1: Single-Instance Show-Window + Daemon Rework
Added SIGUSR2 handler to show the ImGui window. Second instance reads PID from lock file, sends SIGUSR2, then exits cleanly. Reworked daemon mode: removed fork() behavior, app always creates window (hidden in daemon mode, visible otherwise).

### Phase 2: Minimal Tray Icon
Re-added libayatana-appindicator3 as an optional tray icon (~90 lines). Left-click shows/hides window. Right-click menu with Show Window / Quit. State-dependent theme icons: `audio-input-microphone` (idle), `media-record` (recording), `preferences-desktop-accessibility` (transcribing).

### Phase 3: Install and Autostart Polish
Updated desktop files and install.sh. Split desktop entries: `whisper-typer.desktop` for app launcher (runs normally, signals existing instance), `whisper-typer-autostart.desktop` for XDG autostart (uses `--daemon` for hidden window + tray).

## Files Created
- `src/tray.h` — Minimal tray icon interface (init, set_state, shutdown, set_on_show_window)
- `src/tray.cpp` — AppIndicator tray icon implementation
- `contrib/whisper-typer-autostart.desktop` — XDG autostart desktop entry

## Files Modified
- `CMakeLists.txt` — Added ENABLE_TRAY option, link libayatana-appindicator3
- `src/typer.cpp` — SIGUSR2 handler, single-instance signal-then-exit, daemon rework (no fork, hidden window)
- `install.sh` — Added libayatana-appindicator3-dev to optional build deps
- `contrib/whisper-typer.desktop` — Runs `whisper-typer` (second instance signals existing)

## Key Decisions
1. SIGUSR2 for show-window (SIGUSR1 already taken for toggle recording)
2. Daemon mode = hidden window, not fork() — XDG autostart doesn't need daemonization
3. Theme icons for tray — adapts to light/dark themes automatically
4. Tray optional at build time via ENABLE_TRAY cmake option
