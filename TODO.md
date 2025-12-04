### Bugs / Nuisances

- Minecraft, GIMP
   - The following error occasionally occurs if the window is moved during startup
      - (possibly because it's not properly responding to wayland events in that time?)
   - [wayland] Data too big for buffer (X + Y > 4096)<br>
     [wayland] error in client communication (pid Z)
- SDL3/GLFW + Vulkan (AMD) + syncobj
   - Occasional OUT-OF-DATE when resizing at extremely high frame rate (14k+fps). Appears to be a Vulkan WSI bug
      - Currently tested only with separate render thread. Need to test with syncronous rendering and other drivers
- Steam client gamma problem
   - Sometimes, after closing a steam game, the steam client's gamma is messed up.
   - Closing and reopening the Steam window does not fix it, the entire processh as to be completely restarted
- Steam client popups appearing in other XWayland windows
   - When steam and xeyes are open, can observe Steam top menu popups showing in xeyes (even when xeyes is in a completely different part of the screen)
- Occasionally a unexpected scene_tree_node with data pointing back to an existing Toplevel is created
- Discord opening in Wayland mode has several issues:
   - Sometimes produces massive amounts of nested JSON and may run out of memory on startup
   - If launches successully size of window is 0,0 and does not select preferred geometry
      - Closing and re-opening from system tray fixes this
      - When launching in X11 mode, first commit also has invalid (0, 0) geometry
- swaync takes whole screen for mouse interactivity, resulting in clicking on window outside of notification center not always pulling focus as would be expected

### To Do

- Support key repeat for bindings (e.g. volume up/down)
- Focus follows mouse?
   - Perhaps a mode that defers to key presses, E.g. focus only follows mouse when no keys are pressed
- Pseudo-fullscreen mode
   - Per-window mode to spoof permenant fullscreen state/size
      - Particularly useful for games that misbehave when resized
- Improved input and modfiier handling
   - ✅ Set default numlock state
   - Mask main modifier(s) from clients?
   - Mask all key/button inputs from clients when main modifier is down?
   - Support non-modifier keys as modifiers (E.g. mouse side buttons)
   - Custom mouse acceleration curves
   - Additional capabilities
      - Key/button remapping?
      - Joystick remapping?
- Improve logging
   - Manage spawned process standard output
      - Spawn option to either ignore, log, or pipe to separate file
   - Track log message origins
   - Use standard logging format with available log view/analysis tooling?
- Surface positions should remain fixed relative to output position when resizing other outputs
- Use updated protocol version
   - xdg_shell -> version 7
   - tell clients that all edges are constrained
- Idle protocol
- Improved zone logic
   - Have toplevels remember what zones edges they are constrained to
   - Readjust all toplevel zone constraint on workspace/output configuration change
