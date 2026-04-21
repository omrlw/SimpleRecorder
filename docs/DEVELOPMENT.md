# Development Guide

## Prerequisites

- Windows 11 recommended
- .NET 8 SDK
- Visual Studio 2022/2026
- Windows App SDK tooling
- Desktop C++ workload
- Windows 11 SDK

## Restore

```powershell
.\build\restore.ps1
```

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
```

Use Visual Studio MSBuild for the full solution. `dotnet build` does not build the native `.vcxproj`.

## Run in Visual Studio

1. Open `SimpleRecorder.sln`.
2. Set `SimpleRecorder.App` as the startup project.
3. Select `Debug | x64`.
4. Press `F5`.

## Run from the Command Line

```powershell
.\src\SimpleRecorder.App\bin\x64\Debug\net8.0-windows10.0.26100.0\SimpleRecorder.App.exe
```

## One-Step Local Run

```powershell
.\build\run-dev.ps1
```

## Native Engine Status

`SimpleRecorder.Engine.Native` is part of the solution build path and backs the current recording/export slice.
When the native DLL is unavailable, the app can still fall back to the managed stub backend for local development, but that is no longer the primary path.

## Local Files

- Persisted settings: `%LocalAppData%\Packages\SimpleRecorder.App\LocalState\settings.json`
- Legacy settings migration source: `%LocalAppData%\SimpleRecorder\settings.json`
- Audio assets: `Audio/`

## Publish to GitHub

If the repository does not exist yet on GitHub:

1. Create an empty repository in the GitHub web UI.
2. Copy its HTTPS or SSH URL.
3. Run:

```powershell
git init -b main
git add .
git commit -m "chore: bootstrap SimpleRecorder repository"
git remote add origin https://github.com/<your-account>/SimpleRecorder.git
git push -u origin main
```

If Git asks for your identity first:

```powershell
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

## CI

The repository includes a Windows GitHub Actions workflow that restores and builds the solution in `Debug | x64` with Visual Studio MSBuild.
