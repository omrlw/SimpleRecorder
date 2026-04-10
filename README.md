# SimpleRecorder

SimpleRecorder is a Windows-first screen recorder designed for fast capture, a clean HUD, and a GPU-first architecture.

This repository currently contains the Phase 1 foundation:

- WinUI 3 HUD shell
- system tray integration
- persisted settings
- managed recorder stub wired through real contracts
- native engine scaffold for future capture, audio, and encode work

## Tech Stack

- UI: C# + WinUI 3 + Windows App SDK
- Application architecture: modular monolith
- Contracts: shared .NET interfaces and models
- Infrastructure: settings, tray, native adapter
- Native engine path: C++/WinRT + Direct3D 11 + Windows.Graphics.Capture + Media Foundation + WASAPI

## Requirements

- Windows 11 recommended
- .NET 8 SDK
- Visual Studio 2022/2026 with:
  - WinUI / Windows App SDK tooling
  - Desktop C++ workload
  - Windows 11 SDK

## Quick Start

### Visual Studio

1. Open `SimpleRecorder.sln`.
2. Set `SimpleRecorder.App` as the startup project.
3. Select `Debug | x64`.
4. Press `F5`.

### Command Line

```powershell
.\build\restore.ps1
dotnet build .\SimpleRecorder.sln -c Debug -p:Platform=x64 -m:1
.\src\SimpleRecorder.App\bin\x64\Debug\net8.0-windows10.0.19041.0\SimpleRecorder.App.exe
```

Or use the development shortcut:

```powershell
.\build\run-dev.ps1
```

## Repository Layout

- `src/SimpleRecorder.App`: WinUI application entrypoint and app lifetime
- `src/SimpleRecorder.Presentation`: HUD views, styles, state, and viewmodels
- `src/SimpleRecorder.Contracts`: shared contracts and recorder/settings models
- `src/SimpleRecorder.Infrastructure`: tray, settings persistence, audio device catalog, and native bridge
- `src/SimpleRecorder.Engine.Native`: native engine scaffold for future capture and encode work
- `build/`: restore and environment bootstrap scripts
- `docs/`: architecture and development documentation

## Current Status

Phase 1 is intentionally focused on product shell quality and architecture readiness.

Implemented:

- HUD window and state transitions
- settings persistence
- tray icon with command routing
- recorder controller abstraction with stubbed backend

Not implemented yet:

- real display/window/region capture
- real screenshot pipeline
- hardware encode
- loopback and microphone capture

## Important Notes

- The app runs unpackaged in `Debug` for fast local iteration.
- The native project exists on disk but is not yet part of the managed solution build pipeline.
- Settings are persisted to `%LocalAppData%\SimpleRecorder\settings.json`.

## Documentation

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)

## Publishing This Repository

Before the first commit, configure your Git identity:

```powershell
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

Then create the first local commit and connect a remote:

```powershell
git init -b main
git add .
git commit -m "chore: bootstrap SimpleRecorder repository"
git remote add origin https://github.com/<your-account>/SimpleRecorder.git
git push -u origin main
```
