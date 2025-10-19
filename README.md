# Zen

A minimalistic stacking zone-based Wayland compositor built on wlroots.

### Goals

- Simple codebase
    - Keep codebase as small and simple as possible without sacrificing clarity or functionality
    - Easily hackable (the good kind) and maintainable
- Grid based window management
    - More scalable to larger format screens than being limited to half divides
- Intuitive mouse-based interactions
    - Learning a load of complex keybinds should not be a gateway to usability
    - Most interactions can be done quickly with only a handful of keybinds
- Input remapping
    - Mouse acceleration
    - Keyboard shortcuts and macros
    - Joystick remapping?

### Non-goals

- Customization/configuration for everything
- Fancy graphics: Animations, rounded corners, blurs
- Multiple types of layout system
- Support every Wayland protocol

# Running

Run with `--help` to see command line options

### Default shortcuts

- MOD + T - Launch `konsole`
- MOD + G - Launch `dolphin`
- MOD + H - Launch `kalk`
- MOD + D - Launch `rofi` (shift for `run`, unshifted for `drun`)
- MOD + V - Launch `pavucontrol`
- MOD + I - Launch `xeyes` (debug purposes)
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

#### CMake Options

 - `USE_GIT_WLROOTS=ON/OFF` (default `OFF`) - Enables latest development git version of wlroots

#### Commands

```
python configure.py
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=MOLD
cmake --build build
```

# Additional recommended applications

- `mako` - Notification client
- `swaybg` - Background client
- `waybar` - System bar
- `kanshi` - Dynamic output profile management (required protocol not hooked up yet)
- `lxqt-sudo` - GUI `sudo` frontend
