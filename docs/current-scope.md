# Current scope

This is the active scope document for the current recording/export vertical slice.
Use it as the source of truth for prompt planning, task execution, and delivery checks.

## In scope
- Packaged WinUI 3 app shell and floating HUD.
- Tray integration and persisted settings.
- Canonical contracts and service interfaces.
- Managed-to-native wiring through the stable C ABI.
- Real native display, window, and region frame capture through the active backend.
- Deterministic display/window source selection plus precision region selection using physical virtual-desktop coordinates.
- Region selection is the only source mode whose requested bounds/output can currently be reported as precise before stop; display/window remain backend-dependent best-effort under the current GDI slice, and completed telemetry is the authoritative effective output.
- Live native H.264/MP4 encoding with bounded queues, session telemetry, and `.srrec` session folders kept for metadata.
- GPU-first native capture/processing is now the preferred path when the selected source resolves cleanly to a single display or a capturable window:
  - `Windows.Graphics.Capture` is attempted first.
  - `DXGI Desktop Duplication` is the technical fallback for single-output desktop capture.
  - `Window` capture does not silently degrade to `DXGI`; if `WGC` cannot sustain a window session, the engine reports that explicitly.
  - `GDI` remains only as the compatibility fallback, including regions that span multiple displays under the current slice.
- Contract-visible post-stop telemetry includes the requested capture backend, effective capture backend, explicit fallback reason/HRESULT when a fallback occurred, and explicit `WGC` failure reason/HRESULT with a null fallback target when `Window` capture fails without an allowed fallback. It also reports effective FPS, drops, queue depth, and capture/convert/encode latency so the app can report real performance honestly.
- HUD state transitions driven by service callbacks.

## Still intentionally incomplete
- Real microphone capture.
- Real loopback/system audio capture.
- Preview rendering.
- Advanced source-picker polish beyond the current deterministic cycle picker and precision region overlay.
- Packaging, signing, and release automation beyond local and CI build verification.
- Seamless multi-display GPU compositing for arbitrary spanning regions. Under the current slice, cross-monitor regions fall back to the compatibility path instead of a multi-output GPU compositor.
- Reliable hardware-vs-software encode attribution beyond conservative manifest reporting. The encode path is D3D11/MF-friendly, but the telemetry only claims hardware encode when it can be determined safely.
- Constant-FPS duplication under `DXGI` when the desktop does not produce fresh updates. In the current slice, `DXGI` keeps the honest `updates-only` policy, so achieved FPS may be below the target frame rate on static content.

## Explicitly out of product scope
- Screenshot capture. SimpleRecorder is a recording-only product; HUD, tray, managed contracts, and managed/native adapter must not expose screenshot commands.

## Acceptance bar
- Restore dependencies from the solution root.
- Build `Debug|x64` with Visual Studio MSBuild.
- Build `Release|x64` with Visual Studio MSBuild.
- Open the app without crashing.
- Show the floating HUD and tray.
- Persist settings across relaunch.
- Record, pause, resume, and stop through the native path when the native DLL is available.
- Keep `Presentation` free from direct Win32, media, and native references.

## Implementation posture
When unsure, prefer:
- honest docs over stale phase labels
- stable boundaries over convenience shortcuts
- incremental improvements over scope resets
- verified behavior over scaffold-only assumptions
