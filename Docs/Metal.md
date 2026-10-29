# Native Metal renderer (macOS)

This backend calls Metal directly. Vulkan, MoltenVK and a Vulkan SDK are not
required for the macOS build. Model 3 games use Metal automatically. Pro-1000
support has been removed. Input configuration creates an ordinary SDL window;
the launcher uses SDL_Renderer with Metal (software fallback).

## Build and select

Requires macOS 10.15 or newer, a Metal-capable GPU and Apple's Xcode Command Line
Tools. The normal SDL2/SDL2_net framework dependencies still apply. The macOS
Makefile obtains them as before. Build from the repository root:

```sh
make -f Makefiles/Makefile.OSX -j4
```

Metal is the only macOS game renderer. Start `./bin/supermodel` for the launcher,
or pass a ROM path. Old backend switches and Quad Rendering options in an INI
are ignored and omitted when the launcher next saves settings. Windows/Linux
use Vulkan; see [Vulkan.md](Vulkan.md).

## Implementation and current limits

The CPU Real3D scene decoder and GPU data layouts are shared with the existing
Vulkan backend. Metal owns the command queue, drawable, buffers, textures,
render passes, pipelines and presentation. Embedded Metal Shading Language
sources are compiled by Metal during renderer initialization, and compilation
failures are written to `Supermodel.log`.

The backend includes opaque and transparent geometry, priority/overlay passes,
packed texture pages and mip decoding, lighting, spotlight and scroll/ambient
fog, depth-based line-of-sight readback, bottom/top tile compositing, all four
tile upscale modes, Retina drawable sizes and fullscreen changes.

This initial implementation uses one frame in flight and waits for GPU
completion before reusing CPU-visible resources. Texture updates merge dirty
rectangles per page and transfer only those bounds. Frame pacing and upload
performance still depend on the workload.

`XResolution` and `YResolution` bound the Metal render resolution, including in
fullscreen and on Retina displays. The drawable keeps the display's proportions
and is scaled to the window by the system compositor. Thus a 496x384 request on
a 4:3 fullscreen display renders at 496x372. `Supermodel.log` records the render
and display dimensions when they change.

To collect CPU/GPU timing averages without changing the saved configuration:

```sh
SUPERMODEL_METAL_PROFILE=1 ./bin/supermodel ROMs/VF3A.zip -res=496,384 -no-vsync -no-throttle
```

Every 120 frames, `Supermodel.log` records the frame interval, drawable acquisition,
CPU encoding, GPU completion wait, GPU execution and the CPU scene/upload/draw
stages. The GPU execution time overlaps the completion wait; do not add these
two numbers. Compare the same gameplay scene and exclude boot/loading frames.
Omit the environment variable for normal play.

Metal currently renders with one sample per pixel; MSAA,
post-processing/CRT filters, screenshots and bitmap gun crosshairs are not
implemented in this backend. Lightgun input remains available. Real gameplay
accuracy still needs testing across games. VF3A startup/attract sequences and
the ROM-free smoke test have been exercised on an Apple M1.

## ROM-free smoke test

```sh
make -f Makefiles/Makefile.OSX test-metal
```

This opens a test window with Metal API validation enabled. It compiles all six
shaders, creates the pipelines, uploads tile and packed texture pages, exercises
all four filters, alternates white flash/empty 3D frames, exercises partial and
page-crossing uploads, resizes the drawable and bounds its render resolution,
presents frames, and destroys/recreates the context. It fails on errors reported
by the renderer. Metal validation messages also appear in the terminal; review
them even if the executable reports PASS. The test does not validate game
geometry, visual accuracy, line-of-sight values or performance.

For game verification, compare reference images of the same scene. Check textures and
lighting, transparent polygons, fog, HUD orientation, normal/widescreen output,
fullscreen toggles, Retina output, and pause/resume. For a failure, retain the
compiler output or `Supermodel.log` and note the Mac/GPU, macOS version, game,
stepping and active INI settings.

## Regenerating shaders

Normal builds use checked-in `Src/Graphics/Metal3D/Shaders/*.msl.h` files.
After updating the corresponding embedded Vulkan SPIR-V shaders, regenerate:

```sh
python3 Scripts/Generate_MetalShaders.py --spirv-cross /path/to/spirv-cross
```

The script uses SPIRV-Cross to produce MSL 2.1, assigns the native resource
bindings and flips clip-space Y to match Metal's viewport convention. This is
an offline shader conversion step; the running renderer uses Metal directly.
The generated shaders and shared decoder derive from Supermodel-VK and
Supermodel under GPL-3.0-or-later, retaining the source attribution.
