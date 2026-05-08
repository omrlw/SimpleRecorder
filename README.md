# SimpleRecorder

SimpleRecorder is a minimal Windows desktop recorder with a compact WinUI 3 HUD and a native, performance-focused recording engine. The product should feel simple from the outside and technical inside: fast startup, low overhead, high-quality H.264 MP4 output, no watermark, and no artificial recording limit.

## Product Focus
- Record display, window, or region sources.
- Keep the UI compact, direct, and based on the active `SimpleRecorder.pen` design source.
- Require a real GPU/iGPU capture and verified hardware H.264 encode path when Windows exposes compatible hardware. CPU/GDI compatibility is only used when no compatible hardware graphics adapter is detected.
- Use H.264 as the product codec for maximum compatibility.
- Support professional screen quality targets: `480p`, `720p`, `1080p`, `1440p`, `4K`, and source/monitor-sized output capped at 4K height when the selected monitor and encoder limits allow it.
- Treat the `120 fps` option as a professional high-refresh target capped at `120` FPS, with a constant output cadence and repeated frames only when capture cannot deliver a fresh frame for a timeline slot.
- Support desktop audio, microphone audio, both, or neither as the product audio model.
- Do not add camera recording, screenshots, watermarks, or recording time limits.

## Current State
The current vertical slice already has the packaged WinUI shell, floating HUD, tray integration, JSON settings, source selection, and a native video recording/export path behind a versioned C ABI.

Implemented now:
- Display, window, and region video recording.
- H.264/MP4 output through Media Foundation.
- GPU-first capture/processing with compatibility fallbacks.
- Verified hardware encoder selection with CPU/software fallback blocked whenever GPU hardware is detected.
- Settings persistence and post-stop `.srrec/manifest.json` metadata.

Not complete yet:
- Real desktop audio and microphone capture.
- Preview rendering.
- Signing, packaging polish, and release automation.

## Architecture
- `SimpleRecorder.App`: packaged WinUI startup and dependency composition.
- `SimpleRecorder.Presentation`: HUD, settings UI, viewmodels, state, theme, and motion.
- `SimpleRecorder.Contracts`: shared enums, DTOs, models, and service interfaces.
- `SimpleRecorder.Infrastructure`: settings, tray, source selection, audio device discovery, and native adapter.
- `SimpleRecorder.Engine.Native`: native x64 recorder, capture backends, GPU processing, H.264 encoding, ABI, and telemetry.

`Presentation` must not reference Win32, WGC, DXGI, D3D11, Media Foundation, WASAPI, or native `sr_*` structs. Managed native interop belongs in `Infrastructure`; native media work belongs in `Engine.Native`.

## Build
Requirements:
- Windows 10/11.
- .NET 8 SDK.
- Visual Studio with Windows App SDK tooling.
- Desktop development with C++.
- Windows SDK.

```powershell
.\build\setup-dev-env.ps1
.\build\restore.ps1
msbuild .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
msbuild .\SimpleRecorder.sln /restore /p:Configuration=Release /p:Platform=x64
```

Use Visual Studio MSBuild for the full solution. `dotnet build` is only reliable for managed-only checks because the solution includes a native `.vcxproj`.

## Run
```powershell
.\build\run-dev.ps1
```

Or open `SimpleRecorder.sln`, set `SimpleRecorder.App` as the startup project, select `Debug | x64`, and run from Visual Studio.
