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
- Audio assets: `Audio/`

## Verification
- Docs only: review Markdown and run `git diff --check`.
- Managed/UI changes: build `Debug | x64`.
- Native engine, ABI, manifest, or adapter changes: build `Debug | x64` and `Release | x64`.
- Startup, HUD, tray, settings, or recording changes: launch the app if local tooling is available.
- Recording pipeline changes: record, pause, resume, stop, then inspect the `.mp4` and `.srrec/manifest.json`.
