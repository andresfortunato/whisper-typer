# whisper-typer

Voice-to-text typing tool for Linux. Records speech via a global hotkey, transcribes with [whisper.cpp](https://github.com/ggml-org/whisper.cpp), and types the result into the focused window. Works on both X11 and Wayland.

## Features

- **Global hotkey** via evdev (works in any window, any desktop environment)
- **Toggle and push-to-talk modes** for flexible recording control
- **Automatic silence detection** stops recording when you stop speaking
- **X11 and Wayland support** (xdotool/xclip on X11, wtype on Wayland)
- **Clipboard and keystroke modes** for text output
- **Terminal-aware pasting** (Ctrl+Shift+V for terminals, Ctrl+V otherwise)
- **Daemon mode** with `--stop` command
- **SIGUSR1 trigger** for scripting and integration
- **Desktop notifications** via notify-send (optional)
- **Config file support** for persistent settings
- **Silero VAD** integration for improved voice activity detection
- **Transcript history** in JSONL format with automatic rotation
- **System tray icon** with status, controls, and clipboard integration (optional)

## Dependencies

**Build-time:**
- CMake 3.14+
- C++17 compiler (GCC or Clang)
- SDL2 (`libsdl2-dev`)

**Runtime (X11):**
- xdotool
- xclip (for clipboard mode, the default)

**Runtime (Wayland):**
- wtype

**Optional:**
- notify-send (desktop notifications on recording/transcription events)
- libayatana-appindicator3-dev (system tray icon)

## Quick Install

Run the install script from the repo root:

```bash
git clone --recursive https://github.com/andresfortunato/whisper-typer.git
cd whisper-typer
./install.sh
```

This installs build and runtime dependencies, builds the project, downloads the
default model (base.en), creates a config file, and optionally sets up hotkey access.

## Build

```bash
git clone --recursive https://github.com/andresfortunato/whisper-typer.git
cd whisper-typer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

To reduce clone size (~176 MB for the whisper.cpp submodule), use a shallow clone:

```bash
git clone --recursive --shallow-submodules --depth 1 \
    https://github.com/andresfortunato/whisper-typer.git
```

### Build without System Tray

To build without the system tray icon (e.g., on a headless server or without GTK3):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TRAY=OFF
cmake --build build -j$(nproc)
```

### GPU Acceleration

To enable CUDA GPU acceleration, pass `-DGGML_CUDA=ON`:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON
cmake --build build -j$(nproc)
```

This requires the CUDA toolkit to be installed. See the [whisper.cpp documentation](https://github.com/ggml-org/whisper.cpp#nvidia-gpu-support) for other GPU backends (Vulkan, Metal, etc.).

## Installation

```bash
sudo cmake --install build
```

This installs:
- `whisper-typer` binary to `/usr/local/bin/`
- systemd user service to `/usr/local/lib/systemd/user/`
- desktop file to `/usr/local/share/applications/` and `/usr/local/etc/xdg/autostart/`

### Model Download

Download a whisper model using the bundled script:

```bash
./whisper.cpp/models/download-ggml-model.sh base.en
```

This places the model at `whisper.cpp/models/ggml-base.en.bin`. For a system-wide install, copy it to `~/.local/share/whisper-typer/`:

```bash
mkdir -p ~/.local/share/whisper-typer
cp whisper.cpp/models/ggml-base.en.bin ~/.local/share/whisper-typer/
```

## Configuration

### Config File

Settings can be placed in `~/.config/whisper-typer/config` (or `$XDG_CONFIG_HOME/whisper-typer/config`). Command-line flags override config file values.

```ini
# Example config
model=/home/user/.local/share/whisper-typer/ggml-base.en.bin
language=en
hotkey=ctrl+period
push-to-talk=false
silence-ms=1500
type-delay-ms=12
no-tray=false
no-history=false
max-history-mb=10
```

### CLI Options

| Flag | Default | Description |
|------|---------|-------------|
| `-m`, `--model` | `models/ggml-base.en.bin` | Path to whisper model |
| `-l`, `--language` | `en` | Spoken language (`auto` for detection) |
| `-t`, `--threads` | `4` (2 in daemon mode) | Number of inference threads |
| `-c`, `--capture` | `-1` | Audio capture device ID |
| `-ac`, `--audio-ctx` | `0` | Audio context size (0 = full) |
| `-ng`, `--no-gpu` | | Disable GPU inference |
| `-fa`, `--flash-attn` | enabled | Enable flash attention |
| `-nfa`, `--no-flash-attn` | | Disable flash attention |
| `-tr`, `--translate` | | Translate to English |
| `--hotkey` | `ctrl+period` | Global hotkey combination |
| `--push-to-talk` | | Hold-to-record mode |
| `--silence-ms` | `1500` | Silence duration to auto-stop (ms) |
| `--max-record-ms` | `30000` | Maximum recording time (ms) |
| `--vad-thold` | `0.6` | VAD energy threshold |
| `--freq-thold` | `100.0` | High-pass filter cutoff (Hz) |
| `--vad-model` | | Path to Silero VAD model |
| `--no-clipboard` | | Use keystroke simulation instead of clipboard |
| `--type-delay-ms` | `12` | Delay between keystrokes (ms) |
| `--no-tray` | | Disable system tray icon |
| `--no-history` | | Disable transcript history |
| `--history-file` | XDG default | Custom history file path |
| `--max-history-mb` | `10` | Max history file size before rotation (MB) |
| `--daemon` | | Run as background daemon |
| `--stop` | | Stop a running daemon |
| `-pe`, `--print-energy` | | Print audio energy levels |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `WHISPER_TYPER_TERMINALS` | Colon-separated list of additional window class names to treat as terminals (e.g. `cool-retro-term:extraterm`) |

### Hotkey Syntax

Hotkeys are specified as `modifier+modifier+key`. Available modifiers: `ctrl`, `shift`, `alt`, `super`. Key names include letters (`a`-`z`), digits (`0`-`9`), function keys (`f1`-`f12`), and named keys: `space`, `period`/`dot`, `comma`, `slash`, `enter`, `tab`, `backspace`, `escape`, `delete`, `insert`, `home`, `end`, `pageup`, `pagedown`, `up`/`down`/`left`/`right`, `plus` (keypad +), and more.

Examples: `ctrl+period`, `super+v`, `ctrl+shift+space`, `f9`

## Usage

### Basic

```bash
whisper-typer -m path/to/model.bin
```

Press the hotkey (default `Ctrl+.`) to start recording. Press again to stop and transcribe. The text is typed into the focused window.

### Push-to-Talk

```bash
whisper-typer -m path/to/model.bin --push-to-talk
```

Hold the hotkey to record, release to transcribe.

### Daemon Mode

```bash
whisper-typer -m path/to/model.bin --daemon
# prints PID and backgrounds

whisper-typer --stop   # stop the daemon
```

In daemon mode, threads default to 2 (instead of 4) and the process runs at nice level 10. Override with `--threads N`.

### Auto-Start on Login

**Method 1: XDG Autostart** (recommended)

Copy the desktop file to your autostart directory:

```bash
cp contrib/whisper-typer.desktop ~/.config/autostart/
```

Set your model path in the config file (`~/.config/whisper-typer/config`) so the desktop entry doesn't need it on the command line.

**Method 2: systemd**

```bash
systemctl --user enable whisper-typer
systemctl --user start whisper-typer
```

Edit the service file to customize the model path and options. The service runs with `Nice=10` and `IOSchedulingClass=idle` for low system impact.

Both methods use the single-instance lock, so only one instance runs at a time.

### SIGUSR1 Trigger

Toggle recording programmatically:

```bash
kill -USR1 $(pidof whisper-typer)
```

This works even when the hotkey is unavailable (e.g., without `input` group membership).

## Transcript History

Every transcription is logged to `~/.local/share/whisper-typer/history.jsonl` (or `$XDG_DATA_HOME/whisper-typer/history.jsonl`). Each line is a JSON object:

```json
{"ts":"2025-01-15T10:30:00Z","text":"hello world","duration_ms":2500}
```

| Field | Description |
|-------|-------------|
| `ts` | ISO 8601 UTC timestamp |
| `text` | Transcribed text |
| `duration_ms` | Audio duration in milliseconds |

The history file is automatically rotated when it exceeds `--max-history-mb` (default 10 MB), keeping the newest half of entries.

Disable with `--no-history` or `no-history=true` in the config file. Use `--history-file` to specify a custom path.

## System Tray Icon

When built with `libayatana-appindicator3-dev`, whisper-typer shows a system tray icon that reflects the current state:

| State | Icon |
|-------|------|
| Idle | Microphone |
| Recording | Record circle |
| Transcribing | Accessibility |

The tray menu provides:
- **Show Last Transcript** — display via desktop notification
- **Copy Last to Clipboard** — copy last transcription
- **Open History File** — open with default application
- **Start/Stop Recording** — toggle recording
- **Quit** — stop whisper-typer

Disable with `--no-tray` or `no-tray=true` in the config file. Build without tray support entirely with `cmake -DENABLE_TRAY=OFF`.

**Install the dependency:**

```bash
sudo apt install libayatana-appindicator3-dev
```

## Troubleshooting

### Hotkey not working

The hotkey uses evdev, which requires read access to `/dev/input/event*`. Add yourself to the `input` group:

```bash
sudo usermod -aG input $USER
# then log out and back in
```

If the hotkey is unavailable, whisper-typer will print instructions and you can still use `kill -USR1` to trigger recording.

### Wayland detection

whisper-typer detects Wayland via the `WAYLAND_DISPLAY` environment variable. If detection is incorrect, ensure the variable is set (or unset) appropriately in your session.

### No audio captured

- Check that SDL2 can access your microphone
- Try listing capture devices: use `-c` with different device IDs
- On PipeWire systems, audio should work automatically

## Known Limitations

- **Hotkey leaks to focused app**: The global hotkey keystroke is also received by the currently focused window. Using `EVIOCGRAB` would prevent this but risks locking out the keyboard entirely. This is a minor cosmetic issue (the extra keystroke is harmless in most apps).
- **Mouse button hotkeys**: Only keyboard keys are supported as hotkeys. Mouse buttons would require opening mouse device files separately.
- **Device hot-add**: New keyboards plugged in after startup are not detected until a Bluetooth disconnect/reconnect triggers a rescan. Restart whisper-typer to pick up newly added keyboards.

## License

[MIT](LICENSE)
