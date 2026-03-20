#!/usr/bin/env bash
set -euo pipefail

# --- Color helpers ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
RESET='\033[0m'

info()  { printf "${BOLD}==> %s${RESET}\n" "$*"; }
ok()    { printf "${GREEN}==> %s${RESET}\n" "$*"; }
warn()  { printf "${YELLOW}==> %s${RESET}\n" "$*"; }
error() { printf "${RED}==> %s${RESET}\n" "$*" >&2; }

ask() {
    printf "${BOLD}==> %s [Y/n] ${RESET}" "$1"
    read -r ans
    case "$ans" in
        [nN]*) return 1 ;;
        *) return 0 ;;
    esac
}

# --- Validate repo root ---
if [[ ! -f CMakeLists.txt ]] || [[ ! -d whisper.cpp ]]; then
    error "This script must be run from the whisper-typer repo root."
    error "Expected to find CMakeLists.txt and whisper.cpp/ in the current directory."
    exit 1
fi

MODEL_NAME="base.en"
MODEL_FILE="ggml-${MODEL_NAME}.bin"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/whisper-typer"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/whisper-typer"
CONFIG_FILE="$CONFIG_DIR/config"
MODEL_PATH="$DATA_DIR/$MODEL_FILE"

echo ""
info "whisper-typer installer"
echo ""

# --- Step 1: Build dependencies ---
info "Step 1: Build dependencies"
NEED_BUILD_DEPS=0
for cmd in cmake pkg-config; do
    if ! command -v "$cmd" &>/dev/null; then
        NEED_BUILD_DEPS=1
        break
    fi
done
# Also check for libraries via pkg-config
if command -v pkg-config &>/dev/null; then
    for lib in sdl2 gl; do
        if ! pkg-config --exists "$lib" 2>/dev/null; then
            NEED_BUILD_DEPS=1
            break
        fi
    done
fi

if [[ $NEED_BUILD_DEPS -eq 1 ]]; then
    if ask "Install build dependencies (cmake, libsdl2-dev, libgl-dev, libei-dev, etc.)?"; then
        sudo apt install -y cmake build-essential libsdl2-dev libgl-dev \
            libayatana-appindicator3-dev libgtk-3-dev pkg-config \
            libei-dev liboeffis-dev
        ok "Build dependencies installed."
    else
        warn "Skipping build dependencies. Build may fail if they are missing."
    fi
else
    ok "Build dependencies already installed."
fi

# --- Step 2: Build ---
echo ""
info "Step 2: Build whisper-typer"
if ask "Build whisper-typer?"; then
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j"$(nproc)"
    ok "Build complete."
else
    warn "Skipping build."
fi

# --- Step 3: Install binary + system files ---
echo ""
info "Step 3: Install binary and system files"
if ask "Install whisper-typer to /usr/local? (requires sudo)"; then
    sudo cmake --install build
    ok "Installed whisper-typer binary and system files."
else
    warn "Skipping installation. You can run ./build/bin/whisper-typer directly."
fi

# --- Step 4: Download whisper model ---
echo ""
info "Step 4: Download whisper model ($MODEL_NAME)"
if [[ -f "$MODEL_PATH" ]]; then
    ok "Model already exists at $MODEL_PATH"
else
    if ask "Download the $MODEL_NAME model (~142 MB)?"; then
        # Download using the bundled script
        bash whisper.cpp/models/download-ggml-model.sh "$MODEL_NAME"

        # --- Step 5: Install model to user dir ---
        echo ""
        info "Step 5: Install model to $DATA_DIR"
        mkdir -p "$DATA_DIR"
        cp "whisper.cpp/models/$MODEL_FILE" "$MODEL_PATH"
        ok "Model installed to $MODEL_PATH"
    else
        warn "Skipping model download. You can download it later with:"
        warn "  bash whisper.cpp/models/download-ggml-model.sh $MODEL_NAME"
    fi
fi

# If model was already present, skip step 5 messaging but still show it
if [[ -f "$MODEL_PATH" ]] && [[ ! -f "whisper.cpp/models/$MODEL_FILE" ]] 2>/dev/null; then
    echo ""
    ok "Step 5: Model already in place at $MODEL_PATH"
fi

# --- Step 6: Runtime dependencies ---
echo ""
info "Step 6: Runtime dependencies"
RUNTIME_PKGS="libnotify-bin"
if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    info "Detected Wayland session."
    info "  libei is the primary typing backend (built-in, no runtime deps)."
    info "  wtype is an opt-in fallback (--allow-wtype). See security notes."
elif [[ -n "${DISPLAY:-}" ]]; then
    info "Detected X11 session."
    RUNTIME_PKGS="$RUNTIME_PKGS xdotool xclip"
else
    warn "No display session detected. Installing both X11 and Wayland tools."
    RUNTIME_PKGS="$RUNTIME_PKGS xdotool xclip wtype wl-clipboard"
fi

NEED_RUNTIME=0
for pkg in $RUNTIME_PKGS; do
    if ! dpkg -s "$pkg" &>/dev/null; then
        NEED_RUNTIME=1
        break
    fi
done

if [[ $NEED_RUNTIME -eq 1 ]]; then
    if ask "Install runtime dependencies ($RUNTIME_PKGS)?"; then
        sudo apt install -y $RUNTIME_PKGS
        ok "Runtime dependencies installed."
    else
        warn "Skipping runtime dependencies."
    fi
else
    ok "Runtime dependencies already installed."
fi

# --- Step 7: Config file ---
echo ""
info "Step 7: Config file"
if [[ -f "$CONFIG_FILE" ]]; then
    ok "Config already exists at $CONFIG_FILE"
else
    if ask "Create default config at $CONFIG_FILE?"; then
        mkdir -p "$CONFIG_DIR"
        cat > "$CONFIG_FILE" <<EOF
# whisper-typer config
# See: whisper-typer --help for all options
model=$MODEL_PATH
language=en
hotkey=ctrl+period
push-to-talk=false
silence-ms=1500
type-delay-ms=12
no-gui=false
no-history=false
max-history-mb=10
EOF
        ok "Config file created."
    else
        warn "Skipping config file creation."
    fi
fi

# --- Summary ---
echo ""
echo "============================================"
ok "Installation complete!"
echo "============================================"
echo ""
info "To start whisper-typer:"
echo "    whisper-typer"
echo ""
info "Or run in daemon mode:"
echo "    whisper-typer --daemon"
echo ""
info "Default hotkey: Ctrl+."
echo ""
