#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$HOME/.local/bin"
SERVICE_DIR="$HOME/.config/systemd/user"

echo "Installing TouchFlow..."

# Check dependencies
for cmd in cmake pkg-config udevadm brightnessctl pactl gdbus gnome-screenshot; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing dependency: $cmd"
        exit 1
    fi
done

if ! pkg-config --exists libevdev; then
    echo "Missing libevdev development files."
    echo "Install with: sudo apt install libevdev-dev"
    exit 1
fi

# Check input group
if ! id -nG | grep -qw input; then
    echo "Your user is not in the input group."
    echo "Run: sudo usermod -aG input $USER"
    echo "Then log out and log in again."
    exit 1
fi

# Build
cmake -S "$PROJECT_DIR/backend" -B "$PROJECT_DIR/backend/build"
cmake --build "$PROJECT_DIR/backend/build" -j"$(nproc)"

# Install executable
mkdir -p "$BIN_DIR" "$SERVICE_DIR"
install -m 755 "$PROJECT_DIR/backend/build/touchflow" "$BIN_DIR/touchflow"

# Install automatic touchpad detection
cat > "$BIN_DIR/touchflow-start" <<'SCRIPT'
#!/usr/bin/env bash
set -u

for device in /dev/input/event*; do
    [[ -e "$device" ]] || continue

    if udevadm info --query=property --name="$device" 2>/dev/null |
       grep -qx 'ID_INPUT_TOUCHPAD=1'; then
        echo "TouchFlow: using touchpad $device"
        exec "$HOME/.local/bin/touchflow" "$device"
    fi
done

echo "TouchFlow: no touchpad detected" >&2
exit 1
SCRIPT

chmod 755 "$BIN_DIR/touchflow-start"

# Create systemd user service
cat > "$SERVICE_DIR/touchflow.service" <<'SERVICE'
[Unit]
Description=TouchFlow Gesture Controller
After=graphical-session.target

[Service]
Type=simple
ExecStart=%h/.local/bin/touchflow-start
Restart=always
RestartSec=3

[Install]
WantedBy=default.target
SERVICE

systemctl --user daemon-reload
systemctl --user enable touchflow.service
systemctl --user restart touchflow.service

echo "TouchFlow installed successfully."
systemctl --user status touchflow.service --no-pager
