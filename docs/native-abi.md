# Native ABI

SimpleRecorder uses a versioned C ABI between managed code and the native recorder.

## Current contract
- ABI version: `3`.
- Struct version: `2`.
- Managed declarations live in `SimpleRecorder.Infrastructure/Native`.
- `Infrastructure.NativeStructMapper` maps contracts to `sr_*` POD structs.
- No `sr_*` type is exposed above `Infrastructure`.

## Export surface
- Create/destroy engine.
- Set status callback.
- Initialize engine.
- Prepare recording output path.
- Start, pause, resume, and stop recording.
- Legacy screenshot exports remain only for ABI compatibility and return failure.

## Current native behavior
- Records display, window, and region sources.
- Writes live H.264/MP4 output.
- Keeps `.srrec/manifest.json` for metadata and telemetry.
- Reports status through callbacks.
- Reports requested/effective capture backend, fallback reason/HRESULT, encode backend, output size, FPS, drops, queue depth, and latency after stop.
- Accepts additive encoder preference and video codec fields. The current effective codec remains H.264; HEVC and AV1 are reserved contract values until implemented.
- Applies an explicit H.264 quality policy for screen/text capture, including calibrated bitrate, constrained VBR, quality/speed, GOP, and CABAC settings when the active Media Foundation encoder accepts them.
- Uses a hardware-first Media Foundation policy: enumerate hardware H.264/NV12 MFTs and negotiate the D3D11 sink writer before verified hardware attribution, falling back through unverified hardware-requested and compatibility attempts unless hardware-only was requested.
- Reports quality preset, target/max bitrate, rate control, quality/speed, GOP, CABAC request, and encoder configuration fallback status in the manifest.
- Uses a fixed SDR desktop color policy for the GPU recording path: BGRA full-range capture, D3D11 VideoProcessor conversion to BT.709 limited-range NV12, and BT.709 H.264/MP4 Media Foundation media types.
- Reports color primaries, transfer function, YUV matrix, nominal range, and D3D input/output color-space policy additively in schema version `7` manifests.
- Reports encoder preference, selection reason, fallback reason, vendor/name, adapter LUID, codec, and input pixel format in schema version `7` manifests.
- Reports GPU pipeline shape additively in the manifest, including queue/resource sizing, D3D multithread protection, failure HRESULT, and conservative hardware encode attribution. When Media Foundation hardware transforms are requested but the active encoder cannot be verified, the manifest reports that explicitly instead of claiming hardware encode.
- Does not provide real audio capture, preview, HDR, Display P3, BT.2020, or tone mapping.

## Native source layout
The DLL exports still compile from `src/engine.cpp`. The implementation is split into `src/engine/*.inl` files that are included by `engine.cpp` inside the native engine anonymous namespace. This is deliberate: it improves reviewability without introducing new exported symbols, changing helper visibility, or changing ABI behavior. New native recording work should land in the closest existing implementation partition, and only become a separately compiled `.cpp` when ownership boundaries and linker-visible contracts are explicit.

## Backend policy
- Try `Windows.Graphics.Capture` first for capturable windows and single-display captures.
- Fall back to `DXGI Desktop Duplication` for supported desktop capture.
- Do not fall back from failed window `WGC` capture to `DXGI`.
- Use `GDI` as compatibility fallback.
- Preserve `DXGI` updates-only behavior on static desktop content.
- Prefer a D3D11/NV12 Media Foundation input path for GPU capture. GDI compatibility capture uses the same bitrate/profile/encoder-config policy as the GPU path. Hardware transforms may be requested, but manifest hardware/software attribution must stay conservative unless the engine can verify it.

## Region field
`sr_capture_source.region` uses physical virtual-desktop coordinates. It supports negative origins, clipping before capture, and output scaling/even-normalization independent of selected bounds. Within ABI `3`, it may also carry normalized bounds hints for display/window sources.

## Rules
- Keep structs POD-friendly and versionable.
- Prefer additive changes over breaking signatures.
- Keep ownership and allocation rules explicit.
- Do not leak WinRT, COM, or C++ types across the ABI.
