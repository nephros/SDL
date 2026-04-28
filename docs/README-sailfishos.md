Sailfish OS
=======
SDL port for the Sailfish OS mobile operating system


Building
--------
Sailfish OS is Linux, so the build instructions for Linux apply.

We do not have X11 support so this is disabled.

Also, as Sailfish OS does not have some concepts common on Dessktop Linux,
things like Tray (`SDL_TRAY`) and Dialog (`SDL_DIALOG`) are disabled for now.

You may want to toggle `wl_shell` support by toggling the `SDL_WAYLAND_WL_SHELL` cmake option.

Other options:

```sh
cmake -S. -Bbuild -DSDL_RENDER_VULKAN=ON -DSDL_RENDER_GPU=ON -DSDL_OPENVR=ON
cmake --build build
cmake --install build
```


Notes
-----
* The port includes an EXPERIMENTAL implementation of the deprecated `wl_shell`
  wayland protocol.  May be enabled by configuring with `-DSDL_WAYLAND_WL_SHELL=ON`.
* experimental support for droidmedia-based cameras. This driver may be
  required to be explicitly requested by setting the hint
  [`SDL_HINT_CAMERA_DRIVER`](https://wiki.libsdl.org/SDL3/SDL_HINT_CAMERA_DRIVER),
  or the variable `SDL_CAMERA_DRIVER=droidcamera`. Otherwise the Linux default
  (v4l2) is selected.
* OpenURL: Urls will be opened using the Lipstick OpenUrl method
* experimental/WIP: Support for sensorfw-based sensors.
* experimental/WIP: Support for display bounds (rounded corners/camera notches)
* Missing features: Maliit IME. Virtual/Touch Keyboard input may or may not work depending on the application.
