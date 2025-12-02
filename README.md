# Zen

A minimalistic, opinionated, stacking grid-based Wayland compositor built on wlroots.

### Goals

- Focused
    - Keep as little responsibility in the compositor binary. Prefer to shift responsibilty to composable client applications.
- Simple
    - The codebase should be as small and simple as possible without sacrificing clarity or functionality
    - Easily hackable (the good kind) and maintainable
- Grid based window management
    - More scalable to larger format screens than being limited to half divides
- Intuitive hybrid keyboard/mouse interactions
    - Learning a load of complex keybinds should not be a gateway to usability
    - But neither should an interface be hampered by the requirement of usability by only a single category of input device

### Non-goals

- Customization/configuration for everything
    - To keep things simple, this is an opinionated compositor built around workflows I use. It would never be practical to write something configurable enough for everyone, and no one would use it even if it was.
- Visual effects: Animations, blurs
    - These add substantial complexity to the scene code, and more importantly - I don't use them.
- Multiple layout systems
    - There are already many quality tiling (and scrolling) wayland compositors out there, and I don't want to maintain a layout system that I'm not dogfooding myself.
- Support every Wayland protocol
- Support all (or even many) weird X11 interactions
    - This is a forwards looking wayland compositor. Electron and even WINE/Proton applications now have Wayland support for the most part (with a few remaining issues to be ironed out). With `xwayland-satellite` working as well as it does for the remainder of applications, it doesn't make much sense to dedicate a large amount of technical effort to creating and maintaining special case paths just for the dwindling number of X11-only applications.

# Running

Run with `--help` to see command line options

### Core shortcuts

- MOD + Escape - Exit
- MOD + S - Clear focus
- MOD + F - Toggle fullscreen
- MOD + Q - Close focused window
- MOD + MiddleClick - Close under cursor
- MOD + Tab - Cycle focus (shift to cycle backwards)
- MOD + Scroll - Cycle focus under cursor
- MOD + LeftClick - Start zone placement
- MOD + Shift + LeftClick - Start move
- MOD + RightClick - Start move/size

# Building

#### System dependencies (build-time)

- cmake
- ninja
- wayland-protocols
- seatd
- libliftoff
- xcb-util-errors
- mold
- clang (C++26 capable version at minimum)

#### System dependencies (runtime)

- xwayland-satellite (if `--xwayland` flag passed)

#### Quickstart

```
python build.py -I
```

# Additional applications

- `pavucontrol` - Audio
- `blueman` - Bluetooth
- `swaync` - Notifications
- `swaybg` - Background
- `waybar` - System bar
- `wlr-randr` - Output management
- `kanshi` - Dynamic profile-based output management
- `lxqt-sudo` - GUI frontend for `sudo`
- `grim` - Screenshot
- `slurp` - Region selection
- `xdg-desktop-portal-wlr` - Screenshare
- `gnome-keyring` - Secret Service
