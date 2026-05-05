# SimpleRecorder

SimpleRecorder is a packaged WinUI 3 desktop recorder for Windows. The app centers on a compact floating HUD, tray integration, persisted settings, source selection, and a native recording engine behind a versioned C ABI.

## Current Architecture
- `SimpleRecorder.App`: WinUI entry point, window lifetime, and dependency composition.
- `SimpleRecorder.Presentation`: HUD, settings UI, viewmodels, reducer/state, theme, and motion.
- `SimpleRecorder.Contracts`: canonical enums, DTOs, models, and service interfaces.
- `SimpleRecorder.Infrastructure`: settings persistence, tray, source discovery, region overlay, and managed native adapter.
- `SimpleRecorder.Engine.Native`: x64 DLL, ABI exports, WGC/DXGI/GDI capture, D3D11 frame processing, Media Foundation encoding, and telemetry.

The boundaries are deliberate: `Presentation` never references Win32, D3D11, Media Foundation, WASAPI, or `sr_*` ABI structs. `Infrastructure` is the only managed layer that maps contracts to native POD structs.

## Recording Pipeline
- Capture prefers `Windows.Graphics.Capture` for capturable windows and single-display captures.
- `DXGI Desktop Duplication` is the explicit fallback for supported desktop capture.
- `GDI` is reserved for compatibility, including unsupported spanning-region cases.
- GPU capture uses D3D11 crop/scale/color conversion from BGRA full-range desktop frames to BT.709 limited-range NV12.
- Encoding writes live H.264/MP4 through Media Foundation.
- Hardware encode is hardware-first, not blindly assumed: the engine probes H.264/NV12 hardware MFTs, negotiates the D3D11 Sink Writer path, and reports verified hardware only when it can justify it.

## Outputs And Telemetry
Recordings are written as `.mp4` files next to `.srrec` session folders. Each session folder contains `manifest.json` schema version 7 with:
- requested/effective capture backend and fallback details,
- encoder preference, codec, pixel format, vendor/name, adapter name/LUID, and selection/fallback reason,
- quality policy, bitrate, GOP, CABAC, color policy,
- FPS, drops, queue depth, capture/convert/encode latency, and output size.

## Current Scope
In scope: packaged WinUI shell, HUD, tray, persisted settings, display/window/region recording, GPU-first native capture, H.264/MP4 export, hardware-first Media Foundation encode policy, and post-stop telemetry.

Not implemented yet: real microphone capture, real loopback/system audio capture, preview rendering, direct NVENC/AMF/oneVPL backends, HDR/tone mapping, signing, and release automation.

Out of scope: screenshot capture. SimpleRecorder is recording-only.

## Build
Prerequisites: Windows 10/11, .NET 8 SDK, Visual Studio with Windows App SDK tooling, Desktop development with C++, and Windows SDK.

```powershell
.\build\setup-dev-env.ps1
.\build\restore.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Release /p:Platform=x64
```

Use Visual Studio MSBuild for the full solution. `dotnet build` can build managed projects, but it does not reliably build the native `.vcxproj`.

## Run
Open `SimpleRecorder.sln`, set `SimpleRecorder.App` as the startup project, select `Debug | x64`, and run.

For one-step local launch:

```powershell
.\build\run-dev.ps1
```

If unpackaged launch fails with `REGDB_E_CLASSNOTREG` during Windows App Runtime initialization, install/register the matching Windows App Runtime or run from a properly configured Visual Studio packaged app environment.
