# Architecture

SimpleRecorder is a five-project WinUI 3 desktop app with a native recorder behind a versioned C ABI.

## Product Shape
The app is intentionally simple at the UI layer and technical in the engine. Users choose a source, quality, resolution, and audio intent, then record to compatible H.264 MP4 without watermarks or artificial time limits.

## Modules
- `SimpleRecorder.App`: WinUI startup, main window, lifetime, dependency composition.
- `SimpleRecorder.Presentation`: HUD, settings UI, viewmodels, reducer/state, theme, motion.
- `SimpleRecorder.Contracts`: shared enums, DTOs, models, service interfaces.
- `SimpleRecorder.Infrastructure`: settings, tray, source selection, audio device discovery, native adapter.
- `SimpleRecorder.Engine.Native`: x64 DLL, C ABI, capture backends, GPU processing, H.264 encoding, telemetry.

## Boundaries
- `Contracts` is the canonical model layer.
- `App` composes dependencies; it does not own recorder logic.
- `Presentation` consumes contracts and services only.
- `Infrastructure` maps contracts to native POD structs and is the only managed caller of the DLL.
- `Engine.Native` owns Win32, WGC, DXGI, D3D11, Media Foundation, and future WASAPI internals.

## Recording Pipeline
- Source: display, window, or physical virtual-desktop region.
- Capture: prefer Windows Graphics Capture where it fits.
- Fallback: use DXGI Desktop Duplication for supported desktop capture and GDI only for compatibility.
- Frame processing: crop, scale, normalize, and convert frames in D3D11 where possible.
- Encode: write H.264 MP4 through Media Foundation, preferring hardware encode when it can be verified.
- Metadata: write `.srrec/manifest.json` next to the MP4 for backend, encoder, color, output, and timing telemetry.

## Product Constraints
- H.264 is the active product codec.
- Camera recording is not a product feature.
- Screenshot capture is not a product feature.
- Audio capture is a product goal but not yet complete in the native engine.
- The UI follows `SimpleRecorder.pen`; legacy `.fig` files are archival.

## Native Source Layout
`SimpleRecorder.Engine.Native/src/engine.cpp` owns the ABI/export implementation. Internal engine responsibilities are split into `src/engine/*.inl` partitions included by `engine.cpp`:
- `media_geometry.inl`: monitor/source geometry and D3D setup.
- `frame_graph.inl`: crop, scale, and pixel conversion.
- `encoder_mf.inl`: Media Foundation H.264 writer.
- `capture_backends.inl`: WGC and DXGI capture.
- `legacy_gdi_pipeline.inl`: compatibility capture.
- `capture_loops.inl` and `encode_loops.inl`: runtime loops.
- `session_lifecycle.inl`: stop/failure coordination.
- `manifest_writer.inl`: session metadata.
- `engine_initialization.inl`: backend startup.

Keep new native recording work in the closest existing partition unless a new compiled module has a clear ownership boundary.
