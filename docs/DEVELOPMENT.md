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
dotnet build .\SimpleRecorder.sln -c Debug -p:Platform=x64 -m:1
```

## Run in Visual Studio

1. Open `SimpleRecorder.sln`.
2. Set `SimpleRecorder.App` as the startup project.
3. Select `Debug | x64`.
4. Press `F5`.

## Run from the Command Line

```powershell
.\src\SimpleRecorder.App\bin\x64\Debug\net8.0-windows10.0.19041.0\SimpleRecorder.App.exe
```

## One-Step Local Run

```powershell
.\build\run-dev.ps1
```

## Native Engine Status

`SimpleRecorder.Engine.Native` is scaffolded but not yet part of the solution build path. The current app runs through the managed stub backend until the native engine is fully integrated.

## Local Files

- Persisted settings: `%LocalAppData%\SimpleRecorder\settings.json`
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

The repository includes a Windows GitHub Actions workflow that restores and builds the solution in `Debug | x64`.
