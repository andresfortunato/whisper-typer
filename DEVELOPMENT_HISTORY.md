# whisper-typer Development History

Comprehensive record of all work done on whisper-typer while it lived inside
`whisper.cpp/examples/typer/`. This file serves as institutional knowledge for
the standalone repo.

---

## Origin

whisper-typer was developed as an example inside the [whisper.cpp](https://github.com/ggml-org/whisper.cpp) repo at `examples/typer/`. It was extracted into this standalone repo on 2026-02-23 because it is a Linux-specific end-user application, not an API example.

## Git History (2 commits)

### Commit 1: Initial implementation
```
5dc0ec9d  typer : add voice-to-text typing tool for Linux
Author: Andres Fortunato <andfortu@gmail.com>
Date:   Mon Feb 23 20:19:58 2026 -0500
Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>

 examples/CMakeLists.txt        |   2 +
 examples/typer/CMakeLists.txt  |  22 ++
 examples/typer/hotkey.cpp      | 369 +++++++++++++++
 examples/typer/hotkey.h        |  42 ++
 examples/typer/test_hotkey.cpp |  74 +++
 examples/typer/text-output.cpp | 166 +++++++
 examples/typer/text-output.h   |  31 ++
 examples/typer/typer.cpp       | 491 ++++++++++++++++++++
 8 files changed, 1197 insertions(+)
```

### Commit 2: Security and robustness fixes
```
f04aac76  typer : fix security, robustness, and code quality issues
Author: Andres Fortunato <andfortu@gmail.com>
Date:   Mon Feb 23 20:33:27 2026 -0500
Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>

 examples/typer/hotkey.cpp      |  67 ++++--
 examples/typer/hotkey.h        |   7 +-
 examples/typer/text-output.cpp | 302 +++++++++++++++++++------
 examples/typer/text-output.h   |  15 +-
 examples/typer/typer.cpp       | 111 ++++++---
 5 files changed, 377 insertions(+), 125 deletions(-)
```

---

## Bugs Found and Fixed (Commit 2)

### Security Bugs

1. **Shell injection via system()/popen()** (CRITICAL)
   - **Before**: `system("xdotool type ... " + escaped_text)` and `popen("xclip ...")` used shell to run commands. Despite `shell_escape()`, this is a shell injection surface.
   - **Fix**: Replaced ALL `system()`/`popen()` calls with `fork()+execvp()` via new `run_cmd()` function. No shell is ever invoked for subprocess execution. Removed `shell_escape()` entirely.

2. **Dependency check via `system("which ...")`** (MEDIUM)
   - **Before**: `system("which xdotool > /dev/null 2>&1")` used shell.
   - **Fix**: Replaced with `fork()+execlp("sh", "command -v", prog)` using POSIX-standard `command -v`.

### Robustness Bugs

3. **No subprocess timeout** (HIGH)
   - **Before**: `system()` and `popen()` could hang indefinitely if xdotool/xclip froze.
   - **Fix**: Added `run_cmd()` with 5-second timeout (`CMD_TIMEOUT_MS`). Uses `waitpid(WNOHANG)` polling with `SIGKILL` on timeout.

4. **Clipboard race condition** (MEDIUM)
   - **Before**: `is_terminal_focused()` called `xdotool getactivewindow getwindowclassname` as a single popen command, then the paste was sent to whatever window was focused at that later point. If the user switched windows between the two calls, paste would go to the wrong window.
   - **Fix**: Get window ID once via `xdotool getactivewindow`, then use `--window <id>` for both class check and paste keystroke. Window targeting is now atomic.

5. **Debounce was blocking** (MEDIUM)
   - **Before**: After starting recording, `sleep_for(300ms)` blocked the main thread to prevent immediate re-trigger.
   - **Fix**: Replaced with timestamp-based debounce. Records `record_start` time and ignores stop events within 300ms. Non-blocking.

6. **No single-instance protection** (MEDIUM)
   - **Before**: Multiple whisper-typer instances could run simultaneously, fighting over the hotkey and audio device.
   - **Fix**: Added file lock at `/tmp/whisper-typer.lock` using `flock(LOCK_EX | LOCK_NB)`. Second instance gets clear error message.

7. **No device disconnect recovery** (MEDIUM)
   - **Before**: If a Bluetooth keyboard disconnected, `read()` would fail silently and the hotkey listener would stop working permanently.
   - **Fix**: Detect `EIO`/`ENODEV` errors, trigger device rescan. Reset modifier state. Retry with 5-second backoff. Logs disconnect/rescan events.

8. **Daemon didn't chdir("/")** (LOW)
   - **Before**: Daemon process kept its working directory, which could prevent unmounting filesystems.
   - **Fix**: Added `chdir("/")` after `setsid()` in daemon mode.

9. **Clipboard restore delay too short** (LOW)
   - **Before**: 150ms delay between paste and clipboard restore. Some apps need more time.
   - **Fix**: Increased to 300ms (`CLIPBOARD_RESTORE_DELAY_MS`). Named constant for clarity.

### Code Quality Improvements

10. **Raw pointer for PIMPL**
    - **Before**: `Impl * m_impl = nullptr` with manual `new`/`delete`.
    - **Fix**: `std::unique_ptr<Impl> m_impl` with `std::make_unique`.

11. **No copy/move prevention**
    - **Before**: `HotkeyListener` and `TextOutput` could be accidentally copied.
    - **Fix**: Added `= delete` for copy constructor and copy assignment on both classes.

12. **Unsafe CLI argument parsing**
    - **Before**: `std::stoi(v)` / `std::stof(v)` threw exceptions on bad input with no handling.
    - **Fix**: Added `parse_int()` and `parse_float()` with try-catch, position checking, and error messages.

13. **Signal handler safety not verified**
    - **Before**: Used `std::atomic<bool>` in signal handlers without verifying lock-free guarantee.
    - **Fix**: Added `static_assert(std::atomic<bool>::is_always_lock_free)`.

14. **Terminal detection was case-sensitive and incomplete**
    - **Before**: Hardcoded both cases (`"kitty", "Kitty"`) for each terminal. Missing many terminals.
    - **Fix**: Case-insensitive `tolower()` comparison. Added: ghostty, rio, contour, hyper, tabby, sakura, guake, tilda, yakuake, terminology, gnome-terminal-server.

15. **Whitespace trimming was hand-rolled**
    - **Before**: Manual `find_first_not_of` / `substr` for leading-only trim.
    - **Fix**: Used `::trim()` from whisper.cpp's `common.h` (trims both ends).

16. **F-key mapping assumed contiguous layout**
    - **Before**: `KEY_F1 + (n - 1)` for all F-keys. F11=87 and F12=88 are NOT contiguous with F1-F10=59-68.
    - **Fix**: Added `static_assert(KEY_F1 + 9 == KEY_F10)` safety check. Special-cased F11 and F12.

17. **Centralized fd cleanup**
    - **Before**: `close(fd)` + `fds.clear()` duplicated in `stop()` and at various error paths.
    - **Fix**: Added `Impl::close_all_fds()` method, used everywhere.

---

## Architecture

### State Machine
```
IDLE ──(hotkey/SIGUSR1)──> RECORDING ──(stop/silence/timeout)──> TRANSCRIBING ──> IDLE
```

### Components

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| Main app | typer.cpp | 557 | CLI, state machine, whisper integration |
| Hotkey | hotkey.{h,cpp} | 465 | evdev-based global hotkey listener |
| Text output | text-output.{h,cpp} | 334 | Clipboard paste or xdotool typing |
| Test utility | test_hotkey.cpp | 75 | X11 keyboard debug tool (not built) |

### Key Design Decisions

1. **evdev instead of X11 for hotkey**: X11's `XQueryKeymap` doesn't work under Xwayland (tested with `test_hotkey.cpp`). evdev works everywhere but requires `input` group.

2. **Audio kept running continuously**: On PipeWire, SDL audio callbacks fail if the device is resumed after the evdev hotkey thread starts. Solution: never pause audio, just `clear()` the buffer on recording start.

3. **usleep() instead of poll()**: The hotkey thread uses `usleep(20000)` (50Hz) instead of `poll()` on evdev fds, because `poll()` interferes with SDL2's PipeWire audio thread which also uses fd polling.

4. **Clipboard paste as default**: More reliable than direct xdotool keystroke simulation. Saves/restores clipboard to avoid data loss. Detects terminals to use `Ctrl+Shift+V` instead of `Ctrl+V`.

5. **Speech detection gate**: Transcription is skipped if no speech was detected during recording, preventing whisper "hallucinations" on silence.

6. **fork()+execvp() only**: No shell invocation anywhere. All subprocess calls use direct exec with argv arrays.

---

## Configuration Reference

### CLI Options

| Flag | Default | Description |
|------|---------|-------------|
| `-t, --threads N` | min(4, cores) | Whisper threads |
| `-m, --model FNAME` | models/ggml-base.en.bin | Model path |
| `-l, --language LANG` | en | Spoken language |
| `-c, --capture ID` | -1 (auto) | SDL audio device |
| `-ng, --no-gpu` | false | Disable GPU |
| `-fa, --flash-attn` | true | Flash attention |
| `-nfa, --no-flash-attn` | false | Disable flash attention |
| `-tr, --translate` | false | Translate to English |
| `-ac, --audio-ctx N` | 0 | Audio context size |
| `--hotkey KEY` | ctrl+period | Global hotkey string |
| `--push-to-talk` | false | Hold-to-record mode |
| `--silence-ms N` | 1500 | Silence to auto-stop |
| `--max-record-ms N` | 30000 | Max recording time |
| `--vad-thold N` | 0.60 | VAD energy threshold |
| `--freq-thold N` | 100.00 | High-pass filter Hz |
| `--vad-model F` | (none) | Silero VAD model path |
| `--no-clipboard` | false | Use xdotool type |
| `--type-delay-ms N` | 12 | Keystroke delay |
| `--daemon` | false | Run as daemon |
| `-pe, --print-energy` | false | Print audio energy |

### Hotkey Syntax

Format: `[modifier+...]key`

Modifiers: `ctrl`, `shift`, `alt`, `super` (also: `control`, `meta`, `mod4`)

Keys: `a-z`, `0-9`, `f1-f12`, `space`, `period`/`dot`/`.`, `comma`/`,`, `slash`/`/`, `enter`/`return`, `tab`, `backspace`, `escape`/`esc`, `delete`/`del`, `insert`/`ins`, `home`, `end`, `pageup`, `pagedown`, `up`/`down`/`left`/`right`, `capslock`, `print`/`sysrq`, `pause`, and more.

### Terminal Detection List

Terminals that get `Ctrl+Shift+V` paste:
alacritty, kitty, gnome-terminal, gnome-terminal-server, xterm, uxterm, konsole, xfce4-terminal, terminator, tilix, urxvt, st-256color, st, foot, wezterm, terminal, ghostty, rio, contour, hyper, tabby, sakura, guake, tilda, yakuake, terminology

---

## Runtime Dependencies

| Tool | Required | Purpose |
|------|----------|---------|
| SDL2 | Always | Audio capture |
| xdotool | Always | Keystroke/paste simulation, window class detection |
| xclip | Clipboard mode | Clipboard read/write |

### Setup

```bash
# Install dependencies
sudo apt install libsdl2-dev xdotool xclip

# Add to input group (for evdev hotkey)
sudo usermod -aG input $USER
# Log out and back in

# Download a model
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin -P models/
```

---

## Known Limitations

1. **Linux-only**: Uses evdev, fork(), xdotool, xclip. No macOS/Windows support.
2. **X11/Xwayland only for text output**: xdotool and xclip require X11. Native Wayland text input (wtype, wl-clipboard) not yet supported.
3. **Hotkey requires input group**: evdev needs read access to `/dev/input/event*`.
4. **No streaming transcription**: Records full utterance, then transcribes. Not real-time.
5. **Terminal detection is heuristic**: Based on window class name list. Unknown terminals get `Ctrl+V`.
6. **Single audio device**: No runtime device switching.
7. **PipeWire audio workaround**: Audio stream runs continuously even when not recording, using slightly more CPU.

---

## Claude Code Session Context

### Project Settings (`.claude/settings.local.json`)
```json
{
  "permissions": {
    "allow": [
      "Bash(echo:*)",
      "Bash(dpkg -l:*)",
      "Bash(/home/fortu/GitHub/whisper.cpp/build/bin/whisper-typer:*)",
      "Bash(pgrep -la whisper-typer 2>/dev/null || echo \"No zombie processes\")"
    ]
  }
}
```

### Session Files
Located at `/home/fortu/.claude/projects/-home-fortu-GitHub-whisper-cpp/`:

| Session | Size | Date | Purpose |
|---------|------|------|---------|
| `e2eaaaef` | 1.5MB | Feb 19 | Early development session |
| `7a91be48` | 13.5MB | Feb 19-23 | Main development session (longest) |
| `a3f614ef` | 1.1MB | Feb 23 | Standalone repo planning session |
| `940a0d1f` | 1.2MB | Feb 23 | Current session (repo extraction) |

### Development Timeline
- **Feb 19**: Initial development of typer tool. Explored X11 vs evdev approaches. Built test_hotkey.cpp to diagnose XQueryKeymap failures under Xwayland. Decided on evdev approach. Implemented full tool.
- **Feb 23**: Code review pass identifying security/robustness issues. Rewrote text-output.cpp to eliminate all shell calls. Added instance lock, device recovery, safe parsing, debounce fix. Planned and executed extraction to standalone repo.

---

## Standalone Repo Extraction Notes

### What Changed vs In-Tree Build

1. **C++17 required** (was C++11 via DefaultTargetOptions): Source uses `std::make_unique` (C++14) and `std::atomic<bool>::is_always_lock_free` (C++17).

2. **Must build `common` and `common-sdl` ourselves**: Setting `WHISPER_BUILD_EXAMPLES=OFF` disables the examples CMakeLists.txt which defines these library targets. Our CMakeLists.txt creates them from the submodule's source files.

3. **Source files in `src/` subdirectory**: Moved from flat `examples/typer/` to `src/` for cleaner layout.

4. **`test_hotkey.cpp` not included**: It's an X11 debug utility, not part of the main build. It requires `libx11-dev` and was only used to diagnose why X11 hotkey detection failed under Xwayland (answer: XQueryKeymap doesn't work, so we use evdev).

### test_hotkey.cpp Purpose (For Reference)
Minimal X11 test that polls `XQueryKeymap` every 20ms for 10 seconds and reports key state changes. Used to prove that Ctrl+Period could not be detected under Xwayland, which motivated the switch to evdev-based hotkey detection. Not needed in production.

---

## Future Work Ideas

- [ ] Native Wayland support (wtype for text output, wl-clipboard for clipboard)
- [ ] Streaming/partial transcription for real-time feedback
- [ ] Audio device selection UI or auto-detection
- [ ] System tray indicator showing recording state
- [ ] Configurable recording indicator (visual/audio feedback)
- [ ] Support for custom post-processing (capitalize, punctuate, etc.)
- [ ] Packaging (deb, AppImage, Flatpak)
- [ ] Systemd service file for daemon mode
