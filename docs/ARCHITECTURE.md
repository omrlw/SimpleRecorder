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
- Capture: prefer DXGI Desktop Duplication for display/region capture, because it tracks desktop presents more closely; use Windows Graphics Capture for window capture and as the display/region fallback where DXGI cannot start.
- Fallback: use GDI only when no compatible hardware graphics adapter is detected.
- Frame processing: crop, aspect-fit scale, normalize, and convert frames in D3D11 where possible. Display geometry uses DXGI physical output bounds when Win32 monitor rectangles are DPI-virtualized, and WGC `ContentSize` can correct the source/output geometry before the GPU graph is exposed to the encoder.
- Encode: write H.264 MP4 through Media Foundation, requiring verified hardware encode whenever a real GPU/iGPU is present.
- Compatibility report: write a native JSON probe for DXGI adapter enumeration, D3D11 video processor format support, WGC support, DXGI duplication startup, Media Foundation hardware encoder discovery, and H.264/NV12 negotiation profiles.
- 120 FPS mode: `frame_rate = 0` targets `120` FPS, still records monitor refresh telemetry, requests H.264 level 5.2, uses a latency-oriented H.264 speed/bitrate policy, disables encoder frame dropping/frame-rate conversion, and writes a constant output cadence so repeated frames fill only missing capture slots.
- WGC ownership: copy each `Direct3D11CaptureFrame` surface into an owned texture while the frame is still checked out, using `ContentSize` as the valid source rectangle before queueing it for the capture loop.
- Audio: use WASAPI shared-mode capture in `Engine.Native`; desktop audio uses loopback on the default render endpoint, microphone uses the selected/default capture endpoint, and `SystemAndMicrophone` mixes both into one AAC stereo track in the MP4.
- Metadata: write `.srrec/manifest.json` next to the MP4 for backend, encoder, color, output, geometry, timing, GPU detection, CPU fallback blocking, texture-copy integrity, and audio telemetry.

## Product Constraints
- H.264 is the active product codec.
- Camera recording is not a product feature.
- Screenshot capture is not a product feature.
- Audio capture supports off, computer audio, microphone, and both mixed into one AAC track.
- The UI follows `SimpleRecorder.pen`; legacy `.fig` files are archival.

## Native Source Layout
`SimpleRecorder.Engine.Native/src/engine.cpp` owns the ABI/export implementation. Internal engine responsibilities are split into `src/engine/*.inl` partitions included by `engine.cpp`:
- `media_geometry.inl`: monitor/source geometry and D3D setup.
- `frame_graph.inl`: crop, aspect-fit scale, and pixel conversion.
- `encoder_mf.inl`: Media Foundation H.264 writer.
- `capture_backends.inl`: WGC and DXGI capture.
- `legacy_gdi_pipeline.inl`: compatibility capture.
- `audio_capture.inl`: WASAPI shared-mode capture, mixing, and AAC sample submission.
- `capture_loops.inl` and `encode_loops.inl`: runtime loops.
- `session_lifecycle.inl`: stop/failure coordination.
- `compatibility_report.inl`: GPU/API/encoder probe report writer.
- `manifest_writer.inl`: session metadata.
- `engine_initialization.inl`: backend startup.

Keep new native recording work in the closest existing partition unless a new compiled module has a clear ownership boundary.
