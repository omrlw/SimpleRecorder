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
- Prefers GPU capture/processing and hardware encode when available and verifiable.
- Uses Windows Graphics Capture first where appropriate, DXGI Desktop Duplication as desktop fallback, and GDI as compatibility fallback.
- Writes `.srrec/manifest.json` with backend, encoder, output, frame, latency, queue, color, and fallback telemetry.
- Accepts audio intent fields, but real desktop audio and microphone capture are not implemented yet.
- Accepts reserved HEVC/AV1 enum values, but H.264 is the active product codec.

## Options
- `frame_rate = 0` means monitor-rate capture subject to engine and H.264 safety limits.
- `resolution = 0` means source/monitor-sized output.
- Positive `resolution` values are maximum output heights such as `720`, `1080`, `1440`, and `2160`.
- Product direction includes `480p`; wire it through contracts, UI, mapper, native policy, and docs when implemented.
- Quality presets are `Quality`, `Balanced`, and `Low`, with `Balanced` as the default product posture.

## Rules For ABI Changes
- Prefer additive fields and enum values.
- Do not break existing signatures without an explicit ABI version change.
- Keep allocation and ownership rules explicit.
- Do not leak C++/WinRT, COM, STL, or Media Foundation types through the ABI.
- Update this file when structs, exports, option semantics, or manifest fields change.
