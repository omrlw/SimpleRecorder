# Development

## Requirements
- Windows 10/11.
- .NET 8 SDK.
- Visual Studio with Windows App SDK tooling.
- Desktop development with C++.
- Windows SDK.
- Matching Windows App Runtime when launching unpackaged output.

## Restore
```powershell
.\build\setup-dev-env.ps1
.\build\restore.ps1
```

## Build
Use Visual Studio MSBuild for the full solution because the repo contains `SimpleRecorder.Engine.Native.vcxproj`.

```powershell
msbuild .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
msbuild .\SimpleRecorder.sln /restore /p:Configuration=Release /p:Platform=x64
```

`dotnet build` is useful only for managed-only checks.

## Run
```powershell
.\build\run-dev.ps1
```

Or open `SimpleRecorder.sln`, set `SimpleRecorder.App` as the startup project, select `Debug | x64`, and run from Visual Studio.

If launch fails with `REGDB_E_CLASSNOTREG` from Windows App Runtime initialization, install or register the matching Windows App Runtime for the selected launch mode.

## Local Data
- Packaged settings: `%LocalAppData%\Packages\SimpleRecorder.App\LocalState\settings.json`
- Legacy settings migration source: `%LocalAppData%\SimpleRecorder\settings.json`
- Default recordings: `%UserProfile%\Videos\SimpleRecorder`
- Session metadata: `%UserProfile%\Videos\SimpleRecorder\*.srrec\manifest.json`
- Compatibility reports: `%UserProfile%\Videos\SimpleRecorder\compatibility\simple-recorder-compatibility-*.json`
- Audio assets: `Audio/`

## Compatibility Reports
Use the HUD workbench settings panel, then select **Compatibility report** under Output and telemetry. The report is a native probe, not a recording test. It is intended for AMD/NVIDIA/Intel tester machines and captures:
- DXGI adapter inventory and selected D3D adapter.
- D3D11 BGRA input and NV12 output support for the video processor.
- WGC support and DXGI duplication startup status.
- Media Foundation hardware H.264/NV12 encoder discovery.
- H.264/NV12 negotiation results for common profiles from `1080p30` through `2160p60` and `1080p120`.

If the native DLL is missing or older than the managed app expects, the managed layer still writes a report with the load/export failure so tester feedback is actionable.

## Verification
- Docs only: review Markdown and run `git diff --check`.
- Managed/UI changes: build `Debug | x64`.
- Native engine, ABI, manifest, or adapter changes: build `Debug | x64` and `Release | x64`.
- Startup, HUD, tray, settings, or recording changes: launch the app if local tooling is available.
- Recording pipeline changes: record, pause, resume, stop, then inspect the `.mp4` and `.srrec/manifest.json`.
- Audio changes: verify all four audio modes, confirm microphone permission/device availability, and inspect manifest `audioMode`, `audioStatus`, `audioSamplesWritten`, `audioUnderflows`, and `audioDriftMs`.
- Compatibility report changes: generate a report and verify `hardwareAdapterDetected`, `d3dContextCreated`, `wgcSupported`, `dxgiDuplicationAvailable`, `h264Nv12HardwareEncoderFound`, and `h264NegotiationProfiles`.
- FPS/capture changes: verify `captureBackend`, `targetFrameRate`, `effectiveFrameRate`, `captureFrameRate`, `duplicatedFrameRatio`, `backpressureDropCount`, `droppedFrameCount`, `pacingOverrunCount`, `sourceRect`, `outputRect`, and `cropResizeMismatchReason`.
- Display/region recordings should normally use `dxgi-desktop-duplication`; window recordings should use `windows-graphics-capture`.
