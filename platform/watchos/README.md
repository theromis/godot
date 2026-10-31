# watchOS platform port (experimental)

This folder contains the C++, Objective-C++ and Swift code for an experimental
watchOS platform port.

Unlike iOS and visionOS, this platform does **not** derive from the Apple Embedded
abstract platform ([`drivers/apple_embedded`](/drivers/apple_embedded)), because that
layer depends on UIKit, which is not available on watchOS. It only uses shared
Apple code ([`drivers/apple`](/drivers/apple)).

## Current state

- The app lifecycle is a SwiftUI `App` ([`app_watchos.swift`](app_watchos.swift)) that drives
  the engine main loop through `GDTEngineWatchOS` ([`main_watchos.mm`](main_watchos.mm)).
- watchOS has no GPU API that Godot can use (no Metal, OpenGL ES or Vulkan), so 2D is
  rendered on the CPU by `RasterizerSoftware` ([`rasterizer_software.cpp`](rasterizer_software.cpp),
  [`rasterizer_canvas_software.cpp`](rasterizer_canvas_software.cpp),
  [`texture_storage_software.cpp`](texture_storage_software.cpp)). It is built on top of the dummy
  renderer and is exposed as the `software` rendering method and driver, which `main.cpp` accepts
  on platforms that define `SOFTWARE_RENDERER_ENABLED`. It supports rects (including text),
  nine-patches, primitives, polygons, 2D meshes and multimeshes (so `CPUParticles2D` and
  `MultiMeshInstance2D` work, with mesh and multimesh data kept by
  [`mesh_storage_software.cpp`](mesh_storage_software.cpp)), with clipping, modulation, alpha blending and
  nearest/linear filtering, using integer math in the pixel pipeline. Minified textures with linear
  filtering use mipmaps that are built on first use (unlike the GPU renderers, which only use
  mipmaps with the `*_WITH_MIPMAPS` filters), which avoids cache misses and aliasing. Drawing is deferred until all canvases of a frame are submitted, and only
  the areas that changed since the previous frame are cleared and redrawn; unchanged frames are not
  presented at all. Shaders, materials, lights, GPU particles, canvas groups,
  skeletons, MSDF fonts and 3D are not supported.
- `DisplayServerWatchOS` ([`display_server_watchos.mm`](display_server_watchos.mm)) exposes the
  screen as a single full-screen window, hands every frame to SwiftUI as a `CGImage`, and turns
  single-finger SwiftUI gestures into touch events (mouse emulation from touch makes `Control`s work).
  Digital Crown rotation is sent as mouse wheel events, so `ScrollContainer` and scripts that handle
  the wheel react to it.
- `Input.vibrate_handheld()` plays a watchOS haptic: a click for short vibrations, a notification
  for long or strong ones.
- Audio plays through `AudioDriverWatchOS` ([`audio_driver_watchos.mm`](audio_driver_watchos.mm)), which
  feeds the engine's mix to `AVAudioEngine` through an `AVAudioSourceNode` (the AudioUnit-based
  CoreAudio driver used on iOS isn't available on watchOS). watchOS stops the audio engine when the
  screen dims, when the audio configuration changes and when the session is interrupted, and the
  session can't be activated while the app is launched in the background; the driver restarts the
  engine when the app becomes active again or the interruption ends.
- Note that watchOS haptics play their own system sounds, so `Input.vibrate_handheld()` is audible.
- Exporting uses the shared Apple Embedded Xcode project template
  ([`misc/dist/apple_embedded_xcode`](/misc/dist/apple_embedded_xcode)) and
  `EditorExportPlatformWatchOS` ([`export/export_plugin.cpp`](export/export_plugin.cpp)),
  which produces a standalone (watch-only) app.

## Building

```bash
scons platform=watchos simulator=yes target=template_debug generate_bundle=yes
```

This produces `bin/libgodot.watchos.template_debug.arm64.simulator.a` and, with
`generate_bundle=yes`, the `bin/godot_watchos.zip` export template. The library contains
the `@main` SwiftUI entry point, so the Xcode project only needs to link it.

Set the zip as the custom debug/release template of a watchOS export preset (or install it as
`watchos.zip` in the export templates folder), enable **Export Project Only**, and build the
exported Xcode project, e.g. for the simulator:

```bash
xcodebuild -project <name>.xcodeproj -scheme <name> -sdk watchsimulator CODE_SIGNING_ALLOWED=NO build
```

Unlike the other Apple Embedded platforms, ETC2/ASTC texture compression is optional: the software
renderer decompresses textures anyway, and lossless or lossy textures (the default for 2D) work
without it. With it disabled, the exporter warns that VRAM-compressed textures won't load.

Games designed for a fixed resolution can be scaled to the watch with the `canvas_items` stretch
mode, which draws them at the screen's resolution; the `expand` aspect avoids black bars, as long
as the game positions its content from the visible area rather than the window size.

## Testing on a device

Tested on an Apple Watch Series 7 (watchOS 26.6, `arm64_32`): a small `Control` UI with a rotating
`Sprite2D`, a `Polygon2D`, a `CPUParticles2D` fountain of 40 particles, a `Button` and an animated
`ProgressBar` runs at 60 FPS.

The renderer exposes its timings as `Performance` custom monitors under `software_renderer/`:
`raster_ms` (total), split into `track_ms` (change tracking), `clear_ms` and `draw_ms`, plus
`present_ms`, `redrawn_percent` and `items_drawn`. Drawing is further split by command type into
`draw_texture_rect_ms` (including text), `draw_color_rect_ms`, `draw_geometry_ms` (polygons,
primitives, meshes and multimeshes) and `draw_nine_patch_ms`. On the Series 7, the test scene
spends about 1.1 ms on textured rects, 1.0 ms on geometry (0.2 ms of it on the particles) and
0.2 ms on color rects per frame.
With only changed areas redrawn, the test scene measures:

| Device | Rasterization | Present | Redrawn pixels |
| --- | --- | --- | --- |
| Apple Watch Series 7 | ~2.3–2.8 ms | ~0.6–0.8 ms | ~15–20% |
| watchOS simulator on an Apple Silicon Mac | ~0.5–1.2 ms | ~0.2–0.4 ms | ~15–20% |

Build a device template with `arch=arm64_32` for older watches (Series 8 and earlier models that stay
on watchOS 26) or `arch=arm64` for watches on watchOS 27, and enable only the matching architecture
in the export preset.

Getting a watch on watchOS 26 to show up in Xcode 27 can take some effort. What worked:

1. On the paired iPhone, enable Developer Mode, then use Settings → Developer → Clear Trusted
   Computers, reconnect the iPhone by cable and trust the Mac again.
2. Turn on Airplane Mode on the iPhone (keeping Wi-Fi and Bluetooth on) until the watch shows up
   in DeviceHub as connected.
3. Select the watch as the run destination in Xcode and run once. Xcode reports that Developer Mode
   is disabled, after which Settings → Privacy & Security → Developer Mode appears on the watch.

## Roadmap

1. The one-click run/deploy flow from the editor (only exporting an Xcode project was tested).
2. Optimize the software renderer further (profile per-frame overhead such as change tracking,
   a display-synchronized main loop instead of a timer).
3. Multitouch.
4. `arm64` device templates for watches on watchOS 27 (only `arm64_32` was tested on a device).
