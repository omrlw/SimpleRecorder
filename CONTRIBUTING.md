# Contributing

## Development Setup

1. Install the prerequisites listed in [README.md](README.md).
2. Restore packages:

```powershell
.\build\restore.ps1
```

3. Build the solution:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
```

Use Visual Studio MSBuild for the full solution. `dotnet build` does not cover the native `.vcxproj`.

4. Run the app from Visual Studio with `Debug | x64` and `SimpleRecorder.App` as the startup project.

## Branching

Use short, purpose-driven branch names:

- `feature/hud-polish`
- `fix/tray-command-routing`
- `chore/repo-hygiene`

## Commit Style

Use Conventional Commit prefixes where possible:

- `feat: add region selection overlay shell`
- `fix: preserve tray icon on app restore`
- `docs: document local setup`
- `chore: update package versions`

## Pull Requests

Before opening a pull request:

1. Build the solution in `Debug | x64`.
2. Smoke test the app launch.
3. Update documentation when setup, behavior, or architecture changes.
4. Keep changes scoped to one concern.

## Current Guardrails

- Preserve the modular monolith structure.
- Keep `SimpleRecorder.Contracts` as the single managed contract boundary.
- Do not couple `Presentation` directly to Win32, D3D11, Media Foundation, or WASAPI.
- Keep the HUD lightweight and avoid heavy runtime animation.
