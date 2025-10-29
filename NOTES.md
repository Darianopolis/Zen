## Resources

- <https://wayland.app/protocols/>
- <https://absurdlysuspicious.github.io/wayland-protocols-table/>

## Surfaces

- `geometry`: the region of the surface (in surface local coordinates) that corresponds to logical client geometry, excluding border effects such as drop shadows
- `width/height`: the total region of the surface, in layout units
- `buffer_[width/height]`: the pixel size of the buffer mapped to the surface

Some clients don't correctly report surface `geometry`, and instead report a zero sized geometry box. In this case, the easy workaround is simply to adjust the geometry width/height based on the surface width/height (which *will* be specified if the client has committed anything).

## Window toolkits + Vulkan Driver WSI

 - SDL3 sets a valid `surface_size`, but does not set valid `surface_geometry`
 - Toolkits update `surface_size` based on the compositors requested size. However, this is not synced with VK driver WSI's swapchain creation, resulting in unintentional deviations between surface and buffer size.
 - GLFW + VK driver WSI + `linux-drm-syncobj` occasionally results in a syncobj bug due to surface/buffer size mismatches in unsynchronized commit state between GLFW and the Vulkan driver
    - `{Display Queue} wl_display#1.error(wp_linux_drm_syncobj_surface_v1#43, 3, "Acquire point set but no buffer attached")`
    - This appears to be either a bug in GLFW, RADV, or a fundamental issue with non-cooperative windowing + Vulkan WSI

### Handling invalid `surface_geometry`

If `surface_geometry` is not valid. Assume that the client window toolkit and surface creation are not synchronized (as is the case with toolkits + VK driver WSI).

In response, we can override the geometry to be equal to the `buffer_size`, and use that as the source of truth for the window size. Ignoring the window toolkit itself.

### Handling mismatched surface size / buffer size when geometry is valid

This is harder, as there is no way to know confidently whether the mismatch is intentional.

Additionally, even if it is unintentional, attempting to override geometry based on buffer size causes issues when allowing clients to define their window bounds. As we need to send a `wlr_xdg_toplevel_set_size` fixup with the corrected bounds. But this can result in oscillations between the last surface and buffer sizes until the client commits a surface and buffer size that agree with the compositor state.
