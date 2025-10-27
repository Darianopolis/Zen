# Zen

A minimalistic, opinionated, stacking zone-based Wayland compositor built on wlroots.

### Why?

This is a project to find a happy medium between the absolute limit of code minimalism and a reasonably functional forward-looking compositor that is most importantly as trivial as possible to maintain.

### Goals

- Focused
    - Handle only compositor related concerns
    - Anything substantial that is orthogonal to the compositor should be handled in separate projects and compose together.
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
- Visual effects: Animations, rounded corners, blurs
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
- MOD + N - Sleep/Suspend
- MOD + S - Clear focus
- MOD + F - Toggle fullscreen
- MOD + Q - Close focused window
- MOD + MiddleClick - Close under cursor
- MOD + Tab - Cycle focus (shift to cycle backwards)
- MOD + Scroll - Cycle focus under cursor
- MOD + LeftClick - Start zone placement
- MOD + Shift + LeftCLick - Start move
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

#### Expected CMake options

These are the options that the project is normally built against. Other configurations may work but are not tested.

 - `CMAKE_C_COMPILER` = `clang`
 - `CMAKE_CXX_COMPILER` = `clang++`
 - `CMAKE_LINKER_TYPE` = `MOLD`
 - Generator: `Ninja`

#### Additional options

 - `USE_GIT_WLROOTS` = `ON`/`OFF` (default `OFF`) - Enables latest development git version of wlroots

#### Steps

1. `python configure.py` (Grab dependencies and configure subprojects)
2. Configure and build (and install) CMake project in root directory

#### Additional installation steps (optional)

1. Copy `${CMAKE_INSTALL_PREFIX}/xdg-desktop-portal` to `~/.config/xdg-desktop-portal`
2. Edit portal selection as desired

# Additional applications

- `pavucontrol` - Audio
- `blueman` - Bluetooth
- `swaync` - Notifications
- `swaybg` - Background
- `waybar` - Highly configurably system bar
- `kanshi` - Dynamic output profile management (required protocol not hooked up yet)
- `lxqt-sudo` - GUI frontend for `sudo`
