# TouchFlow

**Animated touchpad gesture controls for Linux GNOME.**

TouchFlow combines a C++ touchpad gesture controller with a GNOME Shell extension to provide animated on-screen brightness and volume controls.

## Features

- **Brightness control:** Adjust screen brightness using touchpad gestures.
- **Volume control:** Adjust system volume using touchpad gestures.
- **Animated OSD:** Display smooth, animated brightness and volume indicators.
- **Screenshot gesture:** Capture screenshots using a three-finger downward swipe.
- **Automatic touchpad detection:** Detect the touchpad through Linux device properties.
- **Background service:** Run automatically through systemd.

## Architecture

TouchFlow consists of two components:

### 1. C++ Backend

Located in `backend/`.

- Uses `libevdev` to read Linux input events.
- Detects touchpad gestures.
- Controls brightness using `brightnessctl`.
- Controls audio volume using `pactl`.
- Captures screenshots using `gnome-screenshot`.
- Sends gesture updates through D-Bus.

### 2. GNOME Shell Extension

Located in `extension/`.

- Listens for TouchFlow D-Bus signals.
- Displays the animated on-screen display.
- Provides visual feedback for brightness and volume changes.

## Requirements

- Linux with GNOME Shell.
- GNOME Shell 42 (currently targeted).
- C++17 compiler.
- CMake 3.16 or newer.
- `libevdev` development headers.
- `brightnessctl`
- `pulseaudio-utils` (provides `pactl`).
- `gnome-screenshot`
- `gdbus`
- `udev`

### Install dependencies on Ubuntu

```bash
sudo apt update

sudo apt install \
    build-essential \
    cmake \
    pkg-config \
    libevdev-dev \
    brightnessctl \
    pulseaudio-utils \
    gnome-screenshot \
    libglib2.0-bin \
    udev
```

Your user must also belong to the `input` group to access touchpad input events:

```bash
sudo usermod -aG input "$USER"
```

Log out and log back in after changing group membership.

**Security note:** Membership in the `input` group grants access to input devices, which can expose keyboard and other input events. Understand this permission before enabling it.

## Installation

Clone the repository:

```bash
git clone https://github.com/Druva4444/TouchFlow.git
cd TouchFlow
```

Make the installer executable and run it:

```bash
chmod +x install.sh
./install.sh
```

The installer:

1. Checks required dependencies.
2. Builds the C++ backend using CMake.
3. Installs the executable into `~/.local/bin/`.
4. Installs the automatic touchpad detection script.
5. Configures a systemd user service.
6. Enables and starts TouchFlow.

### GNOME Shell Extension

The extension is maintained separately in the `extension/` directory.

Install it into your GNOME Shell extensions directory using its UUID:

```bash
mkdir -p ~/.local/share/gnome-shell/extensions/touchflow@druva4444.github.io

cp extension/extension.js \
   extension/metadata.json \
   ~/.local/share/gnome-shell/extensions/touchflow@druva4444.github.io/
```

Enable it:

```bash
gnome-extensions enable touchflow@druva4444.github.io
```

The extension and backend must both be running for the animated OSD to work.

## Build Manually

If you want to build the backend without running the installer:

```bash
cmake -S backend -B backend/build
cmake --build backend/build -j"$(nproc)"
```

The compiled executable will be available at:

```text
backend/build/touchflow
```

## Service Management

TouchFlow runs as a systemd user service.

Check status:

```bash
systemctl --user status touchflow.service
```

Start:

```bash
systemctl --user start touchflow.service
```

Stop:

```bash
systemctl --user stop touchflow.service
```

Restart:

```bash
systemctl --user restart touchflow.service
```

View logs:

```bash
journalctl --user -u touchflow.service -f
```

## Project Structure

```text
TouchFlow/
├── backend/
│   ├── src/
│   │   └── main.cpp
│   └── CMakeLists.txt
├── extension/
│   ├── extension.js
│   └── metadata.json
├── install.sh
├── LICENSE
└── README.md
```

## Troubleshooting

### Touchpad permission denied

Check your group membership:

```bash
id
```

Make sure `input` appears in the groups list. If you have just added yourself to the group, log out and log back in.

### Touchpad not detected

Check which devices are identified as touchpads:

```bash
for dev in /dev/input/event*; do
    udevadm info --query=property --name="$dev" 2>/dev/null |
        grep -q '^ID_INPUT_TOUCHPAD=1$' && echo "$dev"
done
```

### OSD not appearing

Check that the GNOME extension is enabled:

```bash
gnome-extensions info touchflow@druva4444.github.io
```

Check backend logs:

```bash
journalctl --user -u touchflow.service -n 50 --no-pager
```

## Contributing

Contributions, bug reports, and suggestions are welcome.

1. Fork the repository.
2. Create a feature branch.
3. Make your changes.
4. Submit a pull request.

## License

See [LICENSE](LICENSE).
