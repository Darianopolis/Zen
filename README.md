# Quartz

Minimalistic Wayland compositor built on wlroots

# TODO

NOTE: Some things may already be implemented by wlroots, and just need to be tested with all relevant applications.

### Bare minimum functionality

- `[ ]` XWayland
- `[ ]` Keyboard layout
- `[ ]` Application launch shortcuts
- `[ ]` File dialog provider (for file editors)
- `[ ]` Drag-n-drop (for browser tabs)

### Core

- `[ ]` Zone based window snapping
- `[ ]` Display configuration
- `[ ]` Compositor-controlled (super key) window move/size
- `[ ]` Application launch shortcuts
- `[ ]` Server-side window decorations
- `[ ]` Screenshare
- `[ ]` Volume control
- `[ ]` Media player controls
- `[ ]` Focus stealing
- `[ ]` Application theming control (light/dark only)
- `[ ]` Numlock default state
- `[ ]` System tray
- `[ ]` Window list/selection
- `[ ]` Mouse acceleration
- `[ ]` Global keyboard/mouse remapping

### Extra applications

- `[ ]` Bluetooth device management
- `[ ]` Audio device management
- `[ ]` Bar

# Building

## System dependencies

Arch: `wayland-protocols seatd libliftoff xcb-util-errors`

## Meson

```
meson setup build
ninja -c build
sudo ninja install -c build
```
