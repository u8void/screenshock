# screenshock

A small Linux screenshot tool written in C++ with Qt. Take a screenshot, see it
instantly in a preview window, and draw on it using keyboard shortcuts.

Works on **Wayland** (via the xdg-desktop-portal Screenshot API) and **X11**
(direct screen grab). It can also run as a background **daemon**, so a hotkey
brings up the editor immediately.

MAKE SCREENSHOT GREAT AGAIN

## Features

- Draw on the screenshot with the mouse
- Pick any pen color, change pen size with the mouse wheel
- Undo, save as PNG/JPG, copy to clipboard

## Requirements

- Qt 6 (or Qt 5) with the Widgets and DBus modules
- CMake 3.16+ and a C++17 compiler
- For Wayland: `xdg-desktop-portal` plus the backend for your desktop
  (`xdg-desktop-portal-gnome`, `-kde`, `-gtk`, `-hyprland`, `-wlr`, ...)

Debian/Ubuntu:

```bash
sudo apt install build-essential cmake qt6-base-dev xdg-desktop-portal
```

Arch:

```bash
sudo pacman -S base-devel cmake qt6-base xdg-desktop-portal
```

## Build and install

```bash
cmake -B build
cmake --build build
sudo cp build/screenshock /usr/local/bin/

# App entry (the portal uses this to identify the app)
mkdir -p ~/.local/share/applications
cp screenshock.desktop ~/.local/share/applications/
```

## Usage

```bash
screenshock            # take a screenshot and open the editor
screenshock --daemon   # run in the background
```

When the daemon is running, every later `screenshock` call just tells the
daemon to capture and exits immediately. If no daemon is running, it takes a
single screenshot as usual.

## Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+D` | Turn draw mode on or off |
| Left mouse button (hold and drag) | Draw, when draw mode is on |
| `Ctrl+C` | Choose pen color |
| Mouse wheel | Change pen size |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+C` | Copy image to clipboard |
| `Ctrl+S` | Save as PNG or JPG |
| `Ctrl+N` | Take a new screenshot |
| `Esc` | Close the editor (the daemon keeps running) |

The window title shows the current draw mode, pen size and color.

## Running as a daemon

Start it manually:

```bash
screenshock --daemon &
```

Start it automatically at login, using one of these:

```bash
# Desktop autostart
mkdir -p ~/.config/autostart
cp screenshock-daemon.desktop ~/.config/autostart/

# or systemd user service (edit the ExecStart path in the file if needed)
mkdir -p ~/.config/systemd/user
cp screenshock.service ~/.config/systemd/user/
systemctl --user enable --now screenshock.service
```

Wayland does not let apps register global hotkeys themselves. Bind a key
(for example `Print`) to the command `screenshock` in your desktop or
compositor settings.

Stop the daemon with the tray menu or `pkill screenshock`.

## Hyprland window rule

`hyprland-rule.lua` makes the editor float, centered, at 90% of the screen:

```lua
hl.window_rule({
    name = "screenshock",
    match = {
        class = "^screenshock$",
    },
    float = true,
    center = true,
    size = { "monitor_w * 0.9", "monitor_h * 0.9" },
    opacity = 1,
})
```

Check the window class with `hyprctl clients`; it should be `screenshock`.

## Troubleshooting

**"Screenshot failed" or "Portal error"**
Install `xdg-desktop-portal` and the backend for your desktop, then log out
and back in. The first capture may show a permission prompt; allow it.

**`Could not register app ID: App info not found`**
Copy `screenshock.desktop` to `~/.local/share/applications/`.

**`QSystemTrayIcon::setVisible: No Icon set`**
Harmless. No icon theme was found, so a fallback icon is used.

**`QDBusTrayIcon ... name is not activatable`**
Harmless. Nothing on your desktop is showing tray icons. The daemon works
without a tray. To get the icon, enable a tray module (for example in waybar).

**The portal saves an extra file**
The portal stores its own copy of the screenshot, usually in `~/Pictures`.
The editor works on a copy in memory, so you can delete that file.

**Black or empty image on X11**
Make sure you are running a native X11 session, or try
`QT_QPA_PLATFORM=xcb screenshock`.
