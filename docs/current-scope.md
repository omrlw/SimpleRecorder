# Current scope

This is the active source of truth for the current SimpleRecorder slice.

## In scope
- Packaged WinUI 3 app shell with floating HUD.
- Tray integration and JSON settings persistence.
- Canonical contracts and service interfaces.
- Managed-to-native recording through ABI version `3`.
- Display, window, and region recording through the native engine.
- Deterministic display/window cycling and precision region selection in physical virtual-desktop coordinates.
- Live H.264/MP4 output with bounded queues.
- H.264 encoder quality policy tuned for text/UI capture through explicit bitrate, constrained VBR, GOP, CABAC, and quality/speed settings.
- Hardware-first Media Foundation encoder policy with `Auto`, `Hardware only`, and compatibility preferences, using verified hardware attribution only after hardware H.264/NV12 MFT discovery and D3D11 sink writer negotiation.
- Fixed SDR desktop color policy for the GPU path: BGRA full-range capture is converted to BT.709 limited-range NV12 and encoded as BT.709 H.264/MP4.
- `.srrec` session folders for manifest metadata and telemetry.
- Callback-driven HUD state transitions.
- Post-stop telemetry for requested/effective backend, fallback reason/HRESULT, effective FPS, drops, queue depth, latencies, output size, and color policy.

## Native backend policy
- Prefer `Windows.Graphics.Capture` when the selected source resolves to a capturable window or a single display-backed capture.
- Use `DXGI Desktop Duplication` as the explicit fallback for supported desktop capture.
- Do not silently degrade window capture to `DXGI`; report the `WGC` failure instead.
- Use `GDI` only as a compatibility fallback, including spanning regions that cross display boundaries.
- Treat completed telemetry as authoritative for display/window effective output. Region bounds are the only precise pre-stop bounds in the current UI.

## Not implemented yet
- Real microphone capture.
- Real loopback/system audio capture.
- Preview rendering.
- Polished source picker beyond deterministic cycling and precision region overlay.
- Packaging, signing, and release automation beyond local and CI build verification.
- Seamless multi-display GPU compositing for arbitrary spanning regions.
- Constant-FPS duplication for static `DXGI` desktop content.
- Direct NVENC, AMD AMF, and Intel oneVPL backends beyond the Media Foundation MFT route.
- HDR, Display P3, BT.2020, and tone mapping.

## Out of product scope
- Screenshot capture. The app, HUD, tray, contracts, and managed native adapter must not expose screenshot commands.

## Acceptance bar
- Restore dependencies from the solution root.
- Build `Debug | x64` and `Release | x64` with Visual Studio MSBuild.
- Launch without crashing.
- Show HUD and tray.
- Persist settings across relaunch.
- Record, pause, resume, and stop through the native path when the DLL is available.
- Keep `Presentation` free from direct native/media references.
