# whisper-typer

Voice-to-text typing tool for Linux. Records speech via global hotkey, transcribes with [whisper.cpp](https://github.com/ggml-org/whisper.cpp), and types the result into the focused window.

## Dependencies

- SDL2 (`libsdl2-dev`)
- xdotool
- xclip (for clipboard mode, the default)

## Build

```bash
git clone --recursive https://github.com/YOUR_USER/whisper-typer.git
cd whisper-typer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Model

Download a whisper model using the bundled script:

```bash
./whisper.cpp/models/download-ggml-model.sh base.en
```

This places the model at `whisper.cpp/models/ggml-base.en.bin`.

## Usage

```bash
./build/whisper-typer -m whisper.cpp/models/ggml-base.en.bin
```

Press `Ctrl+.` (default hotkey) to start/stop recording. Run `--help` for all options.

The hotkey uses evdev and requires the user to be in the `input` group:

```bash
sudo usermod -aG input $USER
# then log out and back in
```
