# Development

## Requirements
- Windows 10/11.
- .NET 8 SDK.
- Visual Studio with Windows App SDK tooling.
- Desktop development with C++.
- Windows SDK.
- Windows App Runtime matching the configured Windows App SDK when launching unpackaged output.

## Restore And Build
```powershell
.\build\setup-dev-env.ps1
.\build\restore.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Release /p:Platform=x64
```

Use Visual Studio MSBuild for full solution builds. `dotnet build` is useful for managed-only checks, but it does not reliably build `SimpleRecorder.Engine.Native`.

## Run
From Visual Studio:
1. Open `SimpleRecorder.sln`.
2. Set `SimpleRecorder.App` as startup project.
3. Select `Debug | x64`.
4. Run.

From PowerShell:

```powershell
.\build\run-dev.ps1
```

If launch fails with `REGDB_E_CLASSNOTREG` from `DeploymentManagerAutoInitializer`, the Windows App Runtime is not registered for the current launch mode.

## Local Data
- Packaged settings: `%LocalAppData%\Packages\SimpleRecorder.App\LocalState\settings.json`
- Legacy settings migration source: `%LocalAppData%\SimpleRecorder\settings.json`
- Default recordings: `%UserProfile%\Videos\SimpleRecorder`
- Session metadata: `%UserProfile%\Videos\SimpleRecorder\*.srrec\manifest.json`
- Audio assets: `Audio/`

## Verification By Change Type
- Docs only: review Markdown and run `git diff --check`.
- Contracts, Infrastructure, or Presentation: build `Debug | x64`.
- Native engine, ABI, manifest, or adapter: build `Debug | x64` and `Release | x64`.
- Startup, HUD, tray, or settings: launch the app.
- Recording pipeline: record, pause, resume, stop, then inspect `.mp4` and `.srrec/manifest.json`.

## Native Engine Notes
`SimpleRecorder.Engine.Native` compiles `src/engine.cpp` as the ABI/export translation unit. Implementation code lives in responsibility-sized `src/engine/*.inl` partitions included by `engine.cpp`.

Keep WGC/DXGI/GDI capture, D3D11 frame processing, Media Foundation encoding, hardware attribution, and manifest telemetry inside the native engine. Keep C ABI structs POD-friendly and versioned.
