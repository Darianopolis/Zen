# Quartz

A minimalistic stacking zone-based Wayland compositor built on wlroots.

### Goals

- Simple codebase
    - Keep codebase as small and simple as possible (without sacrificing clarity)
    - Easily hackable and maintainable
- Zone based window management
    - Grid based window snapping (similar to grid layout from FancyZones)
- Input remapping
    - Mouse acceleration
    - Keyboard shortcuts and macros
    - Joystick remapping?

### Non-goals

- Customization/configuration for everything
- Support multiple types of layout system
- Support every protocol

# Building

### System dependencies

Arch: `wayland-protocols` `seatd` `libliftoff` `xcb-util-errors`

### CMake

Options

 - `USE_GIT_WLROOTS=ON/OFF` (default `OFF`) - Enables latest git (0.20) version of wlroots

```
python configure.py
cmake -B build [-G Ninja]
cmake --build build
```
