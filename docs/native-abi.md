# Native ABI

SimpleRecorder uses a small versioned C ABI between managed code and the native recorder.

## Contract
- ABI version: `3`.
- Struct version: `3`.
- Native structs are POD-friendly `sr_*` types.
- Managed declarations live under `SimpleRecorder.Infrastructure/Native`.
- No `sr_*` type is exposed above `Infrastructure`.

## Export Surface
- Create and destroy engine.
- Set status callback.
- Initialize engine.
- Prepare recording output.
- Write a compatibility report JSON.
- Start, pause, resume, and stop recording.
- Legacy screenshot exports remain only for ABI compatibility and return unsupported failure.

## Current Native Behavior
- Records display, window, and region video sources.
- Writes live H.264 MP4 output.
- Requires GPU capture/processing and verified hardware H.264 encode when a compatible hardware graphics adapter is detected.
- Uses DXGI Desktop Duplication first for display/region capture, Windows Graphics Capture for window capture and compatible display/region fallback, and GDI as compatibility fallback.
- Uses GDI/CPU compatibility only when no compatible hardware graphics adapter is detected.
- Writes `.srrec/manifest.json` with backend, encoder, output, frame, latency, queue, color, fallback, GPU detection, CPU fallback blocking, geometry, copy-integrity telemetry, and audio telemetry.
- Writes a standalone compatibility report JSON for tester machines. Schema `1` reports DXGI adapters, selected D3D adapter, D3D11 BGRA/NV12 video processor format support, WGC support, DXGI duplication startup, hardware H.264/NV12 Media Foundation encoder discovery, and negotiation probes for `1080p30`, `1080p60`, `1440p60`, `2160p60`, and `1080p120`.
- Captures audio modes `off`, `system`, `microphone`, and `system-and-microphone`. System audio uses WASAPI loopback, microphone uses WASAPI capture, and enabled audio is encoded as one AAC stereo track in the MP4.
- Accepts reserved HEVC/AV1 enum values, but H.264 is the active product codec.

## Options
- FPS presets are `24`, `30`, `60`, and product `120 fps`.
- `frame_rate = 0` means product `120 fps`: target `120` FPS while still detecting monitor refresh for telemetry and respecting stricter H.264 level limits for large resolutions.
- Positive `frame_rate` values outside `24`, `30`, and `60` are not product presets; managed settings normalize legacy high-refresh values to product `120 fps`.
- `resolution = 0` means source/monitor-sized output capped to a maximum output height of `2160` while preserving aspect ratio.
- Positive `resolution` values are maximum output heights: `480`, `720`, `1080`, `1440`, and `2160`.
- Quality presets are `Quality`, `Balanced`, and `Low`, with `Balanced` as the default product posture. Persisted numeric compatibility is `Balanced = 0`, `Quality = 1`, and `Low = 2`.
- `Balanced` uses the screen/text H.264 quality policy: High Profile, CABAC when accepted, constrained VBR, moderate bitrate uplift, quality-oriented D3D11 video processing with fallback, and mild edge enhancement only when the driver supports it. Product `120 fps` uses a faster latency-oriented variant of the policy to avoid encode backpressure.

## FPS And Audio Telemetry
- Manifest schema `11` reports `requestedFrameRate`, `isMonitorFrameRateMode`, `monitorFrameRateLimit`, `targetFrameRate`, `effectiveFrameRate`, `captureFrameRate`, `encodeContainerFrameRate`, `monitorRefreshRate`, `wasMonitorFrameRateCapped`, `frameRatePolicy`, and `fpsCapReason`.
- Audio fields include `audioMode`, `audioCodec`, `audioStatus`, `audioSampleRate`, `audioChannels`, endpoint identifiers, samples/packets written, discontinuities, underflows, drift, and failure reason.
- `requestedFrameRate` is the caller's FPS intent and `targetFrameRate` is the capped H.264 encode cadence selected by native policy. In product `120 fps` mode, `monitorFrameRateLimit` is `120` and the target is normally `120` unless H.264 level limits require a lower cadence.
- `effectiveFrameRate` is computed from encoded frame count over represented media duration using the exact 100 ns Media Foundation sample timeline; with the constant-cadence encoder it should stay near `targetFrameRate` unless startup, shutdown, or encode failure truncates the timeline. Frame drops are disallowed and output frame-rate conversion is disabled. Some hardware encoders can still coalesce identical repeated pictures in MP4 metadata; `captureFrameRate` and `duplicatedFrameRatio` identify whether the source itself delivered enough fresh frames for smooth motion.
- `captureFrameRate` is measured from fresh captured frames. It can be lower than `encodeContainerFrameRate` when WGC/DXGI does not deliver new content for every cadence slot.
- `encodeContainerFrameRate` is the CFR MP4 cadence. Sample timestamps and durations are generated from frame index and target FPS, so non-integer 100 ns cadences such as 120 FPS alternate legal durations rather than accumulating QPC truncation.
- `wallDurationMs`, `representedDurationMs`, and `representedToWallDurationRatio` compare real session time with the MP4 timeline so pacing regressions such as accelerated playback are visible.
- `frameRatePolicy` is `constant-output-cadence-max-120`: all product FPS presets keep fixed MP4 sample durations at the selected target and repeat the latest frame only when capture does not produce a fresh frame for that slot.
- `fpsCapReason` is `none` when the requested FPS is used directly, or a concrete cap such as `H264Limit` or `UnsupportedPreset`.
- `duplicatedFrameCount` and `duplicatedFrameRatio` track repeated CFR frames used when the source did not produce a fresh frame for a timeline slot. In product `120 fps` mode this can be non-zero on slower or idle sources, and is expected when maintaining a 120 FPS container cadence.
- Encoder telemetry distinguishes `encodeBackend`, `hardwareEncode`, `hardwareEncodeStatus`, `encoderSelectionReason`, `encoderFallbackReason`, `encoderRealTime`, `encoderAllowFrameDrops`, and `encoderFrameRateConversion`. Native startup treats only `verified-hardware` as a hardware encode success when GPU hardware is present; software encode is blocked in that case even if the caller requested `Auto` or `SoftwareFallback`.
- H.264 output requests level `5.2` (`h264LevelValue = 52`) while native policy caps frame rate by the level 5.2 macroblock-per-second budget.
- GPU policy telemetry includes `gpuHardwareDetected`, `cpuFallbackAllowed`, `cpuFallbackBlocked`, `cpuFallbackBlockReason`, and `gpuInitializationHresult`.
- Copy-integrity telemetry includes `copyIntegrityStatus`, `copyDimensionMismatchCount`, and `copyIntegrityFailureReason`.
- Geometry telemetry includes `captureItemRect`, `sourceRect`, `outputRect`, and `cropResizeMismatchReason`. Display bounds prefer DXGI output coordinates over DPI-virtualized Win32 monitor rectangles. When WGC reports a `ContentSize` that differs from the requested item rectangle, native scales or promotes the source rectangle to the valid content rectangle before the GPU graph and encoder start. WGC frames are copied into owned D3D textures while the `Direct3D11CaptureFrame` is alive, and only the frame `ContentSize` rectangle is copied to avoid undefined pixels from the frame pool surface.
- WGC resize telemetry uses `wgcResizeCount`, `wgcResizeStatus`, and `wgcResizeFailureReason`. A resize attempts `FramePool.Recreate` plus frame graph reconstruction so capture can continue; if that cannot be done, the manifest records the HRESULT/reason and any capture backend fallback.

## Rules For ABI Changes
- Prefer additive fields and enum values.
- Do not break existing signatures without an explicit ABI version change.
- Keep allocation and ownership rules explicit.
- Do not leak C++/WinRT, COM, STL, or Media Foundation types through the ABI.
- Update this file when structs, exports, option semantics, or manifest fields change.
