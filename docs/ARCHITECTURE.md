# Architecture

SimpleRecorder is a five-module WinUI 3 desktop app with a native recorder behind a stable C ABI.

## Modules
- `SimpleRecorder.App`: packaged WinUI entry point, window lifetime, dependency composition.
- `SimpleRecorder.Presentation`: HUD, settings UI, viewmodels, reducer/state, theme, motion.
- `SimpleRecorder.Contracts`: canonical enums, DTOs, models, and service interfaces.
- `SimpleRecorder.Infrastructure`: settings persistence, tray, source discovery, region overlay, native adapter.
- `SimpleRecorder.Engine.Native`: x64 DLL, ABI exports, capture backends, frame processing, encoding, telemetry.

## Boundaries
- `Contracts` is the only shared model source of truth.
- `App` composes services; it does not own business logic or interop.
- `Presentation` consumes contracts and services only.
- `Infrastructure` maps contracts to native POD structs and is the only managed layer that calls the DLL.
- `Engine.Native` may use Win32, WGC, DXGI, D3D11, Media Foundation, and future WASAPI internally.

## Native pipeline shape
- `CaptureBackend`: `WGC` primary path, `DXGI` desktop fallback, `GDI` compatibility fallback.
- `FrameGraph`: crop/scale/color conversion over bounded resources.
- `EncoderBackend`: H.264/MP4 output through Media Foundation with hardware-first MFT selection over the active D3D11 adapter.
- `Writer`: live `.mp4` plus `.srrec/manifest.json`.
- `Telemetry`: backend, fallback, output, drops, queue depth, and latency reporting.

## Native implementation layout
The native engine keeps a single compiled translation unit, `src/engine.cpp`, so the exported C ABI, anonymous-namespace helpers, and initialization behavior remain stable. The implementation is partitioned into internal include files under `src/engine/` by responsibility:
- `media_geometry.inl`: Media Foundation startup, monitor enumeration, source geometry, and D3D context setup.
- `frame_graph.inl`: D3D11 crop, scale, and BGRA-to-NV12 processing.
- `encoder_mf.inl`: Media Foundation H.264 writer and GPU sample submission.
- `capture_backends.inl`: WGC and DXGI capture backends.
- `legacy_gdi_pipeline.inl`: GDI compatibility capture and RGB32 writer.
- `capture_loops.inl`: capture pacing, backpressure, fallback handling, and queueing.
- `encode_loops.inl`: encode workers and sample timing.
- `session_lifecycle.inl`: stop/failure coordination and telemetry helper calculations.
- `manifest_writer.inl`: `.srrec/manifest.json` writer.
- `engine_initialization.inl`: recording-session backend initialization.

## Coordinate rules
- `ScreenRegion` uses physical pixels in the Windows virtual desktop.
- Negative `X`/`Y` values are valid.
- Display/window discovery and coordinate normalization stay below `Presentation`.
- Encoder output may be scaled and even-normalized without changing selected bounds.
- `sr_capture_source.region` remains physical bounds and may carry normalized hints for display/window sources under ABI `3`.
