# Vulkan renderer (Windows/Linux)

Vulkan is the only Windows/Linux game renderer. macOS uses Metal. The platform
build selects the renderer automatically; no INI switch is needed. OpenGL,
Quad Rendering and Pro-1000 support have been removed.

The launcher and input configuration remain available without OpenGL. The
launcher uses SDL_Renderer with Direct3D on Windows (software fallback) and
software rendering on Linux. This is separate from Vulkan game rendering.

This port includes Vulkan 2D/3D rendering and presentation. Post-processing/CRT
filters, supersampling, screenshots and gun crosshair overlays are unavailable.
Lightgun input remains available. Initialization failures are reported in the log;
a compatible GPU/driver is required because there is no fallback game renderer.

The 2D tile layers keep their native 496x384 resolution. The existing
`UpscaleMode` setting applies in Vulkan too: 0 nearest, 1 biquintic, 2 bilinear
(default), 3 bicubic. The 3D scene renders at the output resolution and does not
receive the tile upscale filter. This is separate from post-processing effects.

Detailed texture checks and 600-frame texture/geometry reports are disabled by
default. They add CPU work and synchronous file logging on the rendering thread.
For investigations only, set `VulkanDiagnostics = true` in the INI to enable them.

The Vulkan path requires a compatible GPU and driver (Vulkan 1.3, or the backend's
supported Vulkan 1.2 extension path). The Vulkan loader is loaded dynamically; no Vulkan import library is required.

## Building

### macOS

The Mac build downloads the official MoltenVK 1.4.2 runtime and Vulkan headers
automatically through `Libraries/Get_MoltenVK.sh`, with a pinned SHA256 check.
Homebrew is not required. To prepare this dependency separately, run:

```sh
sh Libraries/Get_MoltenVK.sh
```

`make -f Makefiles/Makefile.OSX pkg` includes `Frameworks/libMoltenVK.dylib` and
its license/source notices in the package. Vulkan startup locates this library
relative to the executable, including when running from the source tree's `bin/`
directory. No environment variables or system-wide installation are needed.
An explicit `SDL_VULKAN_LIBRARY` setting remains available for developers.

### Windows and other builds

Windows/MSVC: install the Vulkan SDK and set `VULKAN_SDK` (the SDK installer
normally sets it). Its `Include` directory supplies the Vulkan headers. The
runtime is loaded through the bundled MIT-licensed volk loader, so no Vulkan
import library is linked. Alternatively, place Vulkan-Headers in
`Libraries/Vulkan-Headers`.

Makefile builds also accept `Libraries/Vulkan-Headers/include` and
`VULKAN_SDK/Include` (or `include`), or system-installed Vulkan headers. Linux additionally
links `libdl`. The normal SDL2 and other build dependencies still apply.
Embedded SPIR-V headers are included, so shader compilation is unnecessary for
normal builds. After changing GLSL, regenerate its corresponding `*.spv.h` with
glslangValidator and retain the array's `<name>_spv_size = sizeof(<name>_spv)`.

For a ROM-free Windows GPU smoke test, first build x64 Release, then run
`Scripts/Build_Test_VulkanRenderer.cmd` and
`MSVC/x64/Release/Test_VulkanRenderer.exe`. It requires the Vulkan validation
layer, exercises all four tile upscale modes at native and 1920x1200 resolution,
crosses the 600-frame reporting interval with diagnostics disabled, resizes the
swapchain and recreates the context. It does not verify game geometry, visual
accuracy, or reproduce gameplay frame-time spikes.

For the launcher scaling regression test on Windows, build x64 Release, run
`Scripts/Build_Test_LauncherScale.cmd`, then run
`MSVC/x64/Release/Test_LauncherScale.exe`. This checks geometry coverage and
clipping at 1x, 1.5x and 2x without a game ROM.

## Source and attribution

The Vulkan renderer, shaders and volk loader were imported from
[Igloo-Garage/Supermodel-VK](https://github.com/Igloo-Garage/Supermodel-VK),
revision `a44717117305d3e821e8672266426c0376a880b6`.
The imported renderer is Copyright 2026 The Supermodel-VK authors, derived from
Supermodel, Copyright 2003–2026 The Supermodel Team, under GPL-3.0-or-later.
Original notices are retained in the source files. volk is Copyright 2018–2024
Arseny Kapoulkine, under the MIT license reproduced in `Src/Pkgs/volk/volk.h`.

Local adaptations retain the original frontend and emulation interfaces, map the
current hardware stepping enum, generalize the tile-generator renderer hooks, and
remove post-processing, bezel, overlay and screenshot support from the imported
compositor/context. The composite shader has three image slots for the bottom
tiles, 3D scene and top tiles.
