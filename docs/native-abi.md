# Native ABI

SimpleRecorder uses a small versioned C ABI between managed code and the native recorder.

## Contract
- ABI version: `3`.
- Struct version: `2`.
- Native structs are POD-friendly `sr_*` types.
- Managed declarations live under `SimpleRecorder.Infrastructure/Native`.
- No `sr_*` type is exposed above `Infrastructure`.

## Export Surface
- Create and destroy engine.
- Set status callback.
- Initialize engine.
- Prepare recording output.
- Start, pause, resume, and stop recording.
- Legacy screenshot exports remain only for ABI compatibility and return unsupported failure.

## Current Native Behavior
- Records display, window, and region video sources.
- Writes live H.264 MP4 output.
- Requires GPU capture/processing and verified hardware H.264 encode when a compatible hardware graphics adapter is detected.
- Uses Windows Graphics Capture first where appropriate, DXGI Desktop Duplication as desktop fallback, and GDI as compatibility fallback.
- Uses GDI/CPU compatibility only when no compatible hardware graphics adapter is detected.
- Writes `.srrec/manifest.json` with backend, encoder, output, frame, latency, queue, color, fallback, GPU detection, CPU fallback blocking, and copy-integrity telemetry.
- Accepts audio intent fields, but real desktop audio and microphone capture are not implemented yet.
- Accepts reserved HEVC/AV1 enum values, but H.264 is the active product codec.

## Options
- FPS presets are `24`, `30`, `60`, and product `120 fps`.
- `frame_rate = 0` means product `120 fps`: target `120` FPS while still detecting monitor refresh for telemetry and respecting stricter H.264 level limits for large resolutions.
- Positive `frame_rate` values outside `24`, `30`, and `60` are not product presets; managed settings normalize legacy high-refresh values to product `120 fps`.
- `resolution = 0` means source/monitor-sized output capped to a maximum output height of `2160` while preserving aspect ratio.
- Positive `resolution` values are maximum output heights: `480`, `720`, `1080`, `1440`, and `2160`.
- Quality presets are `Quality`, `Balanced`, and `Low`, with `Balanced` as the default product posture. Persisted numeric compatibility is `Balanced = 0`, `Quality = 1`, and `Low = 2`.
- `Balanced` uses the screen/text H.264 quality policy: High Profile, CABAC when accepted, constrained VBR, moderate bitrate uplift, no Media Foundation low-latency flag, quality-oriented D3D11 video processing with fallback, and mild edge enhancement only when the driver supports it.

## FPS Telemetry
- Manifest schema `9` reports `requestedFrameRate`, `isMonitorFrameRateMode`, `monitorFrameRateLimit`, `targetFrameRate`, `effectiveFrameRate`, `monitorRefreshRate`, `wasMonitorFrameRateCapped`, `frameRatePolicy`, and `fpsCapReason`.
- `requestedFrameRate` is the caller's FPS intent and `targetFrameRate` is the capped H.264 encode cadence selected by native policy. In product `120 fps` mode, `monitorFrameRateLimit` is `120` and the target is normally `120` unless H.264 level limits require a lower cadence.
- `effectiveFrameRate` is computed from encoded frame count over represented media duration; with the constant-cadence encoder it should stay near `targetFrameRate` unless startup, shutdown, or encode failure truncates the timeline.
- `wallDurationMs`, `representedDurationMs`, and `representedToWallDurationRatio` compare real session time with the MP4 timeline so pacing regressions such as accelerated playback are visible.
- `frameRatePolicy` is `constant-output-cadence-max-120`: product `120 fps` mode keeps fixed MP4 sample durations at the selected target and repeats the latest frame only when capture does not produce a fresh frame for that slot.
- `fpsCapReason` is `none` when the requested FPS is used directly, or a concrete cap such as `H264Limit` or `UnsupportedPreset`.
- `duplicatedFrameCount` tracks repeated CFR frames used when the source did not produce a fresh frame for a timeline slot. In product `120 fps` mode this can be non-zero on slower or idle sources, and is expected when maintaining a 120 FPS container cadence.
- Encoder telemetry distinguishes `encodeBackend`, `hardwareEncode`, `hardwareEncodeStatus`, `encoderSelectionReason`, and `encoderFallbackReason`. Native startup treats only `verified-hardware` as a hardware encode success when GPU hardware is present; software encode is blocked in that case even if the caller requested `Auto` or `SoftwareFallback`.
- GPU policy telemetry includes `gpuHardwareDetected`, `cpuFallbackAllowed`, `cpuFallbackBlocked`, `cpuFallbackBlockReason`, and `gpuInitializationHresult`.
- Copy-integrity telemetry includes `copyIntegrityStatus`, `copyDimensionMismatchCount`, and `copyIntegrityFailureReason`.
- WGC resize telemetry uses `wgcResizeCount`, `wgcResizeStatus`, and `wgcResizeFailureReason`. A resize attempts `FramePool.Recreate` plus frame graph reconstruction so capture can continue; if that cannot be done, the manifest records the HRESULT/reason and any capture backend fallback.

## Rules For ABI Changes
- Prefer additive fields and enum values.
- Do not break existing signatures without an explicit ABI version change.
- Keep allocation and ownership rules explicit.
- Do not leak C++/WinRT, COM, STL, or Media Foundation types through the ABI.
- Update this file when structs, exports, option semantics, or manifest fields change.
